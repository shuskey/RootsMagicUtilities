#include "rootsmagicsync.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <string>

RootsMagicSync::RootsMagicSync() 
    : m_rootsMagicDb(nullptr), m_digiKamDb(nullptr), 
      m_tagsCreated(0), m_tagsUpdated(0), m_tagsOrphaned(0), m_tagsRescued(0)
{
}

RootsMagicSync::~RootsMagicSync()
{
    if (m_rootsMagicDb) {
        sqlite3_close(m_rootsMagicDb);
    }
    if (m_digiKamDb) {
        sqlite3_close(m_digiKamDb);
    }
}

bool RootsMagicSync::connectToRootsMagicDatabase(const std::string& rmDbPath)
{
    int rc = sqlite3_open_v2(rmDbPath.c_str(), &m_rootsMagicDb, SQLITE_OPEN_READONLY, nullptr);
    if (rc) {
        std::cerr << "Failed to connect to RootsMagic database: " << sqlite3_errmsg(m_rootsMagicDb) << std::endl;
        return false;
    }
    
    // Register a dummy RMNOCASE collation to handle RootsMagic-specific collation
    rc = sqlite3_create_collation(m_rootsMagicDb, "RMNOCASE", SQLITE_UTF8, nullptr, 
                                 [](void*, int len1, const void* data1, int len2, const void* data2) -> int {
                                     // Simple case-insensitive comparison
                                     std::string s1((const char*)data1, len1);
                                     std::string s2((const char*)data2, len2);
                                     std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
                                     std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
                                     return s1.compare(s2);
                                 });
    
    if (rc != SQLITE_OK) {
        std::cerr << "Warning: Failed to register RMNOCASE collation: " << sqlite3_errmsg(m_rootsMagicDb) << std::endl;
    }
    
    std::cout << "Connected to RootsMagic database: " << rmDbPath << std::endl;
    return true;
}

bool RootsMagicSync::connectToDigiKamDatabase(const std::string& dkDbPath)
{
    int rc = sqlite3_open(dkDbPath.c_str(), &m_digiKamDb);
    if (rc) {
        std::cerr << "Failed to connect to DigiKam database: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    std::cout << "Connected to DigiKam database: " << dkDbPath << std::endl;
    return true;
}

bool RootsMagicSync::synchronizeTags(const std::string& parentTagName, const std::string& lostFoundTagName)
{
    if (!m_rootsMagicDb || !m_digiKamDb) {
        std::cerr << "Both databases must be connected before synchronization" << std::endl;
        return false;
    }

    std::cout << "Starting RootsMagic to DigiKam tag synchronization..." << std::endl;

    // Phase 1: Load data and perform migrations outside of transaction
    std::cout << "Loading RootsMagic people..." << std::endl;
    auto rmPeople = loadRootsMagicPeople();
    std::cout << "Found " << rmPeople.size() << " people in RootsMagic" << std::endl;

    std::cout << "Loading family data..." << std::endl;
    auto families = loadFamilyData();
    std::cout << "Found " << families.size() << " families in RootsMagic" << std::endl;

    std::cout << "Loading existing DigiKam tags..." << std::endl;
    auto existingTags = loadExistingDigiKamTags(parentTagName);
    std::cout << "Found " << existingTags.size() << " existing RootsMagic tags in DigiKam" << std::endl;

    // Begin transaction
    if (!executeQuery(m_digiKamDb, "BEGIN TRANSACTION;")) {
        return false;
    }

    try {
        std::cout << "Loading tags from Lost & Found..." << std::endl;
        auto lostFoundTags = loadExistingDigiKamTags(lostFoundTagName);
        std::cout << "Found " << lostFoundTags.size() << " tags in Lost & Found" << std::endl;
        
        // Check for duplicates across both trees and clean them up
        std::vector<int> duplicateTagsToRemove;
        for (const auto& [ownerId, lostTag] : lostFoundTags) {
            if (existingTags.find(ownerId) != existingTags.end()) {
                // This ownerId exists in both trees - mark Lost & Found version for removal
                duplicateTagsToRemove.push_back(lostTag.tagId);
                std::cout << "Found duplicate tag in both trees: " << lostTag.name << " (OwnerID: " << ownerId << ")" << std::endl;
            }
        }
        
        if (!duplicateTagsToRemove.empty()) {
            std::cout << "Removing " << duplicateTagsToRemove.size() << " duplicate tags from Lost & Found..." << std::endl;
            if (removeDuplicateTags(duplicateTagsToRemove)) {
                // Reload Lost & Found tags after cleanup
                lostFoundTags = loadExistingDigiKamTags(lostFoundTagName);
                std::cout << "After cleanup: Found " << lostFoundTags.size() << " tags in Lost & Found" << std::endl;
            }
        }

        // Ensure parent tags exist
        if (!ensureParentTagExists(parentTagName)) {
            throw std::runtime_error("Failed to create parent tag: " + parentTagName);
        }
        if (!ensureParentTagExists(lostFoundTagName)) {
            throw std::runtime_error("Failed to create Lost & Found tag: " + lostFoundTagName);
        }

        // Phase 3: Synchronize
        std::cout << "Synchronizing tags..." << std::endl;
        
        // First, handle family-based parenting for existing tags
        std::cout << "Checking for existing tags that need family parenting..." << std::endl;
        for (const auto& person : rmPeople) {
            if (person.familyId > 0) {
                auto familyIt = families.find(person.familyId);
                if (familyIt != families.end()) {
                    const FamilyRecord& family = familyIt->second;
                    
                    // Create the family tag if it doesn't exist
                    if (!createFamilyTag(family, parentTagName)) {
                        std::cerr << "Failed to create family tag for: " << family.familyTagName << std::endl;
                        continue;
                    }
                    
                    // Check if this person's tag exists and needs to be moved to the family
                    auto existingTagIt = existingTags.find(person.ownerId);
                    if (existingTagIt != existingTags.end()) {
                        // Check if the tag is currently under the RootsMagic parent
                        std::string checkParentSql = R"(
                            SELECT t.pid FROM Tags t 
                            WHERE t.id = ? AND t.pid = (SELECT id FROM Tags WHERE name = ?)
                        )";
                        
                        sqlite3_stmt* stmt;
                        int rc = sqlite3_prepare_v2(m_digiKamDb, checkParentSql.c_str(), -1, &stmt, nullptr);
                        if (rc == SQLITE_OK) {
                            sqlite3_bind_int(stmt, 1, existingTagIt->second.tagId);
                            sqlite3_bind_text(stmt, 2, parentTagName.c_str(), -1, SQLITE_STATIC);
                            
                            if (sqlite3_step(stmt) == SQLITE_ROW) {
                                // Tag is under RootsMagic parent, move it to family parent
                                std::string updateParentSql = R"(
                                    UPDATE Tags SET pid = (SELECT id FROM Tags WHERE name = ?) 
                                    WHERE id = ?
                                )";
                                
                                sqlite3_stmt* updateStmt;
                                int updateRc = sqlite3_prepare_v2(m_digiKamDb, updateParentSql.c_str(), -1, &updateStmt, nullptr);
                                if (updateRc == SQLITE_OK) {
                                    sqlite3_bind_text(updateStmt, 1, family.familyTagName.c_str(), -1, SQLITE_STATIC);
                                    sqlite3_bind_int(updateStmt, 2, existingTagIt->second.tagId);
                                    
                                    if (sqlite3_step(updateStmt) == SQLITE_DONE) {
                                        std::cout << "Moved '" << person.formattedName << "' to family '" << family.familyTagName << "'" << std::endl;
                                    }
                                    sqlite3_finalize(updateStmt);
                                }
                            }
                            sqlite3_finalize(stmt);
                        }
                    }
                }
            }
        }
        
        // Track which existing tags are still valid
        std::vector<int> validTagIds;
        int newPeopleCount = 0;
        
        std::cout << "Synchronizing " << rmPeople.size() << " people..." << std::endl;
        int syncProgress = 0;
        int lastSyncProgressPercent = 0;
        
        for (const auto& person : rmPeople) {
            auto it = existingTags.find(person.ownerId);
            if (it != existingTags.end()) {
                // Tag exists - check if name needs updating
                validTagIds.push_back(it->second.tagId);
                if (it->second.name != person.formattedName) {
                    if (updatePersonTag(it->second.tagId, person)) {
                        m_tagsUpdated++;
                        std::cout << "Updated: '" << it->second.name << "' -> '" << person.formattedName << "' (OwnerID: " << person.ownerId << ")" << std::endl;
                    }
                }
            } else {
                // New person - create tag
                newPeopleCount++;
                if (createPersonTag(person, parentTagName, families)) {
                    m_tagsCreated++;
                    std::cout << "Created: " << person.formattedName << " (OwnerID: " << person.ownerId << ")" << std::endl;
                } else {
                    // Try to rescue from Lost & Found
                    if (rescueTagFromLostFound(person, parentTagName, lostFoundTags)) {
                        m_tagsRescued++;
                        std::cout << "Rescued: " << person.formattedName << " (OwnerID: " << person.ownerId << ")" << std::endl;
                    } else {
                        std::cerr << "Failed to create or rescue tag for: " << person.formattedName << " (OwnerID: " << person.ownerId << ")" << std::endl;
                    }
                }
            }
            
            // Progress tracking for synchronization
            syncProgress++;
            int currentSyncProgressPercent = (syncProgress * 100) / rmPeople.size();
            if (currentSyncProgressPercent > lastSyncProgressPercent) {
                std::cout << "Sync Progress: " << currentSyncProgressPercent << "% (" << syncProgress << "/" << rmPeople.size() << " people)" << std::endl;
                lastSyncProgressPercent = currentSyncProgressPercent;
            }
        }
        
        // Post-rescue cleanup: Check for any new duplicates created by rescue operations
        if (m_tagsRescued > 0) {
            std::cout << "Checking for duplicates after rescue operations..." << std::endl;
            
            // Reload both tag trees to get current state
            auto updatedExistingTags = loadExistingDigiKamTags(parentTagName);
            auto updatedLostFoundTags = loadExistingDigiKamTags(lostFoundTagName);
            
            // Find any duplicates that now exist in both trees
            std::vector<int> postRescueDuplicates;
            for (const auto& [ownerId, lostTag] : updatedLostFoundTags) {
                if (updatedExistingTags.find(ownerId) != updatedExistingTags.end()) {
                    // This ownerId exists in both trees after rescue - remove from Lost & Found
                    postRescueDuplicates.push_back(lostTag.tagId);
                    std::cout << "Found post-rescue duplicate: " << lostTag.name << " (OwnerID: " << ownerId << ")" << std::endl;
                }
            }
            
            if (!postRescueDuplicates.empty()) {
                std::cout << "Removing " << postRescueDuplicates.size() << " post-rescue duplicates from Lost & Found..." << std::endl;
                if (removeDuplicateTags(postRescueDuplicates)) {
                    // Update our local copy of Lost & Found tags
                    lostFoundTags = loadExistingDigiKamTags(lostFoundTagName);
                    std::cout << "Post-rescue cleanup completed successfully" << std::endl;
                }
            }
        }
        
        std::cout << "Found " << newPeopleCount << " new people to process" << std::endl;
        std::cout << "DEBUG: existingTags.size() = " << existingTags.size() << std::endl;

        // Phase 4: Handle orphaned tags
        std::vector<int> orphanedTagIds;
        for (const auto& [ownerId, tag] : existingTags) {
            if (std::find(validTagIds.begin(), validTagIds.end(), tag.tagId) == validTagIds.end()) {
                orphanedTagIds.push_back(tag.tagId);
            }
        }

        if (!orphanedTagIds.empty()) {
            std::cout << "Moving " << orphanedTagIds.size() << " orphaned tags to Lost & Found..." << std::endl;
            if (moveOrphanedTagsToLostFound(orphanedTagIds, lostFoundTagName, existingTags)) {
                m_tagsOrphaned = orphanedTagIds.size();
            }
        }

        // Commit transaction
        if (!executeQuery(m_digiKamDb, "COMMIT;")) {
            throw std::runtime_error("Failed to commit transaction");
        }

        // Get final counts for summary
        auto finalRootsMagicTags = loadExistingDigiKamTags(parentTagName);
        auto finalLostFoundTags = loadExistingDigiKamTags(lostFoundTagName);

        // Print summary
        std::cout << "\nSynchronization completed successfully:" << std::endl;
        std::cout << "  Tags created: " << m_tagsCreated << std::endl;
        std::cout << "  Tags rescued from Lost & Found: " << m_tagsRescued << std::endl;
        std::cout << "  Tags updated: " << m_tagsUpdated << std::endl;
        std::cout << "  Tags moved to Lost & Found: " << m_tagsOrphaned << std::endl;
        std::cout << "\nFinal Summary:" << std::endl;
        std::cout << "  Names synchronized from RootsMagic: " << rmPeople.size() << std::endl;
        std::cout << "  Tags in DigiKam RootsMagic tree: " << finalRootsMagicTags.size() << std::endl;
        std::cout << "  Tags in DigiKam Lost & Found tree: " << finalLostFoundTags.size() << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error during synchronization: " << e.what() << std::endl;
        executeQuery(m_digiKamDb, "ROLLBACK;");
        return false;
    }
}

std::vector<PersonRecord> RootsMagicSync::loadRootsMagicPeople()
{
    std::vector<PersonRecord> people;
    
    std::cout << "Loading people and family relationships..." << std::endl;
    
    // Fixed query: Handle people in multiple families by selecting primary family only
    // This prevents duplicate PersonRecord creation for people in multiple families
    const char* sql = R"(
        SELECT 
            n.OwnerID, n.Surname, n.Given, n.BirthYear, n.DeathYear,
            COALESCE(c.PrimaryFamilyID, 0) as FamilyID
        FROM NameTable n
        LEFT JOIN (
            SELECT 
                ChildID,
                MIN(FamilyID) as PrimaryFamilyID
            FROM ChildTable
            GROUP BY ChildID
        ) c ON n.OwnerID = c.ChildID
        WHERE n.IsPrimary = 1
        ORDER BY n.OwnerID
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_rootsMagicDb, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to query RootsMagic NameTable: " << sqlite3_errmsg(m_rootsMagicDb) << std::endl;
        return people;
    }

    // First, count total rows for progress tracking
    int totalRows = 0;
    sqlite3_stmt* countStmt;
    const char* countSql = "SELECT COUNT(*) FROM NameTable WHERE IsPrimary = 1";
    int countRc = sqlite3_prepare_v2(m_rootsMagicDb, countSql, -1, &countStmt, nullptr);
    if (countRc == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            totalRows = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }
    
    std::cout << "Found " << totalRows << " people to process..." << std::endl;
    
    int processedRows = 0;
    int lastProgressPercent = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PersonRecord person;
        person.ownerId = sqlite3_column_int(stmt, 0);
        person.surname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        person.given = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        person.birthYear = sqlite3_column_int(stmt, 3);
        person.deathYear = sqlite3_column_int(stmt, 4);
        
        // Get family ID (may be NULL if person has no family)
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            person.familyId = sqlite3_column_int(stmt, 5);
        } else {
            person.familyId = 0; // No family
        }
        
        // Trim whitespace
        person.surname = person.surname.substr(0, person.surname.find_last_not_of(" \t") + 1);
        person.given = person.given.substr(0, person.given.find_last_not_of(" \t") + 1);
        
        person.formattedName = formatPersonName(person);
        people.push_back(person);
        
        // Progress tracking
        processedRows++;
        int currentProgressPercent = (processedRows * 100) / totalRows;
        if (currentProgressPercent > lastProgressPercent) {
            std::cout << "Progress: " << currentProgressPercent << "% (" << processedRows << "/" << totalRows << " people)" << std::endl;
            lastProgressPercent = currentProgressPercent;
        }
    }

    sqlite3_finalize(stmt);
    
    std::cout << "Successfully loaded " << people.size() << " people with family relationships." << std::endl;
    return people;
}

std::unordered_map<int, FamilyRecord> RootsMagicSync::loadFamilyData()
{
    std::unordered_map<int, FamilyRecord> families;
    
    std::cout << "Loading family data..." << std::endl;
    
    // Query family data from FamilyTable and get parent names from NameTable
    const char* sql = R"(
        SELECT f.FamilyID, f.FatherID, f.MotherID,
               fn1.Given as FatherGiven, fn1.Surname as FatherSurname,
               fn2.Given as MotherGiven, fn2.Surname as MotherSurname
        FROM FamilyTable f
        LEFT JOIN NameTable fn1 ON f.FatherID = fn1.OwnerID AND fn1.IsPrimary = 1
        LEFT JOIN NameTable fn2 ON f.MotherID = fn2.OwnerID AND fn2.IsPrimary = 1
        ORDER BY f.FamilyID
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_rootsMagicDb, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to query RootsMagic FamilyTable: " << sqlite3_errmsg(m_rootsMagicDb) << std::endl;
        return families;
    }
    
    // Count total families for progress tracking
    int totalFamilies = 0;
    sqlite3_stmt* countStmt;
    const char* countSql = "SELECT COUNT(*) FROM FamilyTable";
    int countRc = sqlite3_prepare_v2(m_rootsMagicDb, countSql, -1, &countStmt, nullptr);
    if (countRc == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            totalFamilies = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }
    
    std::cout << "Found " << totalFamilies << " families to process..." << std::endl;
    
    int processedFamilies = 0;
    int lastProgressPercent = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FamilyRecord family;
        family.familyId = sqlite3_column_int(stmt, 0);
        family.fatherOwnerId = sqlite3_column_int(stmt, 1);
        family.motherOwnerId = sqlite3_column_int(stmt, 2);
        
        // Get father's name (may be NULL)
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            family.fatherGiven = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            family.fatherSurname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        } else {
            family.fatherGiven = "";
            family.fatherSurname = "";
        }
        
        // Get mother's name (may be NULL)
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            family.motherGiven = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            family.motherSurname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        } else {
            family.motherGiven = "";
            family.motherSurname = "";
        }
        
        // Trim whitespace
        family.fatherGiven = family.fatherGiven.substr(0, family.fatherGiven.find_last_not_of(" \t") + 1);
        family.fatherSurname = family.fatherSurname.substr(0, family.fatherSurname.find_last_not_of(" \t") + 1);
        family.motherGiven = family.motherGiven.substr(0, family.motherGiven.find_last_not_of(" \t") + 1);
        family.motherSurname = family.motherSurname.substr(0, family.motherSurname.find_last_not_of(" \t") + 1);
        
        family.familyTagName = formatFamilyTagName(family);
        families[family.familyId] = family;
        
        // Progress tracking
        processedFamilies++;
        int currentProgressPercent = (processedFamilies * 100) / totalFamilies;
        if (currentProgressPercent > lastProgressPercent) {
            std::cout << "Family Progress: " << currentProgressPercent << "% (" << processedFamilies << "/" << totalFamilies << " families)" << std::endl;
            lastProgressPercent = currentProgressPercent;
        }
    }

    sqlite3_finalize(stmt);
    std::cout << "Successfully loaded " << families.size() << " families." << std::endl;
    return families;
}

std::unordered_map<int, DigiKamTag> RootsMagicSync::loadExistingDigiKamTags(const std::string& parentTagName)
{
    std::unordered_map<int, DigiKamTag> tags;
    
    std::string sql = R"(
        SELECT t.id, t.name, CAST(tp.value AS INTEGER) as owner_id 
        FROM Tags t 
        JOIN TagProperties tp ON t.id = tp.tagid 
        WHERE t.pid = (SELECT id FROM Tags WHERE name = ?)
        AND tp.property = 'rootsmagic_owner_id'
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_digiKamDb, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to query existing DigiKam tags: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return tags;
    }

    sqlite3_bind_text(stmt, 1, parentTagName.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DigiKamTag tag;
        tag.tagId = sqlite3_column_int(stmt, 0);
        tag.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        tag.ownerId = sqlite3_column_int(stmt, 2);
        tag.isOrphaned = false;
        
        tags[tag.ownerId] = tag;
    }

    sqlite3_finalize(stmt);
    return tags;
}

bool RootsMagicSync::ensureParentTagExists(const std::string& tagName)
{
    std::string checkSql = "SELECT COUNT(*) FROM Tags WHERE name = ?";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(m_digiKamDb, checkSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_STATIC);
    
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    
    if (!exists) {
        std::string createSql = "INSERT INTO Tags (name, pid, icon, iconkde) VALUES (?, 0, NULL, NULL)";
        rc = sqlite3_prepare_v2(m_digiKamDb, createSql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        return rc == SQLITE_DONE;
    }
    
    return true;
}

bool RootsMagicSync::createPersonTag(const PersonRecord& person, const std::string& parentTagName, 
                                    const std::unordered_map<int, FamilyRecord>& families)
{
    // First check if tag already exists under RootsMagic parent with the same OwnerID
    std::string checkRootsMagicSql = R"(
        SELECT t.id FROM Tags t 
        JOIN TagProperties tp ON t.id = tp.tagid 
        WHERE t.name = ? AND t.pid = (SELECT id FROM Tags WHERE name = ?)
        AND tp.property = 'rootsmagic_owner_id' AND CAST(tp.value AS INTEGER) = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_digiKamDb, checkRootsMagicSql.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, person.formattedName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, parentTagName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, person.ownerId);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Tag already exists under RootsMagic parent with same OwnerID - this is OK
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    
    // Check if tag exists in Lost & Found with the same OwnerID - if so, return false to trigger rescue
    std::string checkLostFoundSql = R"(
        SELECT t.id FROM Tags t 
        JOIN TagProperties tp ON t.id = tp.tagid 
        WHERE t.pid = (SELECT id FROM Tags WHERE name = 'Lost & Found')
        AND tp.property = 'rootsmagic_owner_id' AND CAST(tp.value AS INTEGER) = ?
    )";
    
    rc = sqlite3_prepare_v2(m_digiKamDb, checkLostFoundSql.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, person.ownerId);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Tag exists in Lost & Found with same OwnerID - return false to trigger rescue
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
    }
    
    // Determine the appropriate parent tag for this person
    std::string actualParentTagName = parentTagName;
    
    // Check if this person has a family
    if (person.familyId > 0) {
        auto familyIt = families.find(person.familyId);
        if (familyIt != families.end()) {
            const FamilyRecord& family = familyIt->second;
            
            // Create or ensure the family tag exists
            if (!createFamilyTag(family, parentTagName)) {
                std::cerr << "Failed to create family tag for: " << family.familyTagName << std::endl;
                return false;
            }
            
            // Use the family tag as the parent
            actualParentTagName = family.familyTagName;
        }
    }
    
    // Create the person tag under the appropriate parent
    std::string createTagSql = R"(
        INSERT INTO Tags (name, pid, icon, iconkde) 
        SELECT ?, id, NULL, 'user' FROM Tags WHERE name = ?
    )";
    
    rc = sqlite3_prepare_v2(m_digiKamDb, createTagSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare createPersonTag SQL: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, person.formattedName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, actualParentTagName.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        // Check if this is a UNIQUE constraint violation
        std::string error = sqlite3_errmsg(m_digiKamDb);
        if (error.find("UNIQUE constraint failed") != std::string::npos) {
            // This means the tag exists somewhere else (probably Lost & Found)
            return false; // Let the caller try rescue logic
        }
        std::cerr << "Failed to execute createPersonTag SQL: " << error << std::endl;
        return false;
    }
    
    // Add properties
    int tagId = sqlite3_last_insert_rowid(m_digiKamDb);
    
    std::string addOwnerIdSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'rootsmagic_owner_id', ?)";
    rc = sqlite3_prepare_v2(m_digiKamDb, addOwnerIdSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare addOwnerIdSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, tagId);
    sqlite3_bind_int(stmt, 2, person.ownerId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute addOwnerIdSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    std::string addPersonSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'person', ?)";
    rc = sqlite3_prepare_v2(m_digiKamDb, addPersonSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare addPersonSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, tagId);
    sqlite3_bind_text(stmt, 2, person.formattedName.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute addPersonSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    return true;
}

bool RootsMagicSync::createFamilyTag(const FamilyRecord& family, const std::string& parentTagName)
{
    // Check if family tag already exists
    std::string checkSql = "SELECT COUNT(*) FROM Tags WHERE name = ?";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(m_digiKamDb, checkSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, family.familyTagName.c_str(), -1, SQLITE_STATIC);
    
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    
    if (exists) {
        // Family tag already exists, no need to create it
        return true;
    }
    
    // Create the family tag under the RootsMagic parent
    std::string createSql = R"(
        INSERT INTO Tags (name, pid, icon, iconkde) 
        SELECT ?, id, NULL, 'user' FROM Tags WHERE name = ?
    )";
    
    rc = sqlite3_prepare_v2(m_digiKamDb, createSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare createFamilyTag SQL: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, family.familyTagName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, parentTagName.c_str(), -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute createFamilyTag SQL: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    // Add family properties
    int tagId = sqlite3_last_insert_rowid(m_digiKamDb);
    
    std::string addFamilyIdSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'family_id', ?)";
    rc = sqlite3_prepare_v2(m_digiKamDb, addFamilyIdSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare addFamilyIdSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_int(stmt, 1, tagId);
    sqlite3_bind_int(stmt, 2, family.familyId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute addFamilyIdSql: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    return true;
}

bool RootsMagicSync::updatePersonTag(int tagId, const PersonRecord& person)
{
    // Update tag name
    std::string updateSql = "UPDATE Tags SET name = ? WHERE id = ?";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(m_digiKamDb, updateSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, person.formattedName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, tagId);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) return false;
    
    // Update person property
    std::string updatePersonSql = "UPDATE TagProperties SET value = ? WHERE tagid = ? AND property = 'person'";
    rc = sqlite3_prepare_v2(m_digiKamDb, updatePersonSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_text(stmt, 1, person.formattedName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, tagId);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool RootsMagicSync::moveOrphanedTagsToLostFound(const std::vector<int>& orphanedTagIds, 
                                                const std::string& lostFoundTagName,
                                                const std::unordered_map<int, DigiKamTag>& existingTags)
{
    if (orphanedTagIds.empty()) return true;
    
    std::string updateSql = "UPDATE Tags SET pid = (SELECT id FROM Tags WHERE name = ?) WHERE id = ?";
    sqlite3_stmt* stmt;
    
    for (int tagId : orphanedTagIds) {
        // Find the tag name for logging
        std::string tagName = "Unknown";
        int ownerId = -1;
        for (const auto& [ownerIdKey, tag] : existingTags) {
            if (tag.tagId == tagId) {
                tagName = tag.name;
                ownerId = ownerIdKey;
                break;
            }
        }
        
        int rc = sqlite3_prepare_v2(m_digiKamDb, updateSql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) return false;
        
        sqlite3_bind_text(stmt, 1, lostFoundTagName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, tagId);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc != SQLITE_DONE) return false;
        
        // Log the move
        std::cout << "Moved to Lost & Found: '" << tagName << "' (OwnerID: " << ownerId << ", TagID: " << tagId << ")" << std::endl;
    }
    
    return true;
}

bool RootsMagicSync::rescueTagFromLostFound(const PersonRecord& person, const std::string& parentTagName, 
                                           const std::unordered_map<int, DigiKamTag>& lostFoundTags)
{
    // Check if this person exists in Lost & Found
    auto it = lostFoundTags.find(person.ownerId);
    if (it == lostFoundTags.end()) {
        return false; // Not in Lost & Found
    }
    
    std::cout << "Rescuing from Lost & Found: " << it->second.name << " (OwnerID: " << person.ownerId << ")" << std::endl;
    
    // Move the tag from Lost & Found to RootsMagic parent
    std::string updateSql = "UPDATE Tags SET pid = (SELECT id FROM Tags WHERE name = ?) WHERE id = ?";
    sqlite3_stmt* stmt;
    
    int rc = sqlite3_prepare_v2(m_digiKamDb, updateSql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare rescue SQL: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, parentTagName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, it->second.tagId);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute rescue SQL: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    // Update the tag name if needed
    bool nameWasUpdated = false;
    if (it->second.name != person.formattedName) {
        if (updatePersonTag(it->second.tagId, person)) {
            nameWasUpdated = true;
            std::cout << "Updated rescued tag name: '" << it->second.name << "' -> '" << person.formattedName << "'" << std::endl;
        }
    }
    
    // Ensure the rescued tag has proper RootsMagic properties
    // First, check if rootsmagic_owner_id property already exists
    std::string checkOwnerIdSql = "SELECT COUNT(*) FROM TagProperties WHERE tagid = ? AND property = 'rootsmagic_owner_id'";
    rc = sqlite3_prepare_v2(m_digiKamDb, checkOwnerIdSql.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, it->second.tagId);
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            sqlite3_finalize(stmt);
            
            // Add missing rootsmagic_owner_id property
            std::string addOwnerIdSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'rootsmagic_owner_id', ?)";
            rc = sqlite3_prepare_v2(m_digiKamDb, addOwnerIdSql.c_str(), -1, &stmt, nullptr);
            if (rc == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, it->second.tagId);
                sqlite3_bind_int(stmt, 2, person.ownerId);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        } else {
            sqlite3_finalize(stmt);
        }
    }
    
    // Ensure person property exists and is correct (only if we didn't already update it)
    if (!nameWasUpdated) {
        // Check if person property already exists
        std::string checkPersonSql = "SELECT COUNT(*) FROM TagProperties WHERE tagid = ? AND property = 'person'";
        rc = sqlite3_prepare_v2(m_digiKamDb, checkPersonSql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, it->second.tagId);
            if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
                sqlite3_finalize(stmt);
                
                // Add missing person property
                std::string addPersonSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'person', ?)";
                rc = sqlite3_prepare_v2(m_digiKamDb, addPersonSql.c_str(), -1, &stmt, nullptr);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, it->second.tagId);
                    sqlite3_bind_text(stmt, 2, person.formattedName.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            } else {
                sqlite3_finalize(stmt);
            }
        }
    }
    
    return true;
}



bool RootsMagicSync::removeDuplicateTags(const std::vector<int>& tagIds)
{
    if (tagIds.empty()) return true;
    
    std::cout << "Permanently removing duplicate tags and their properties..." << std::endl;
    
    for (int tagId : tagIds) {
        // First delete all TagProperties for this tag
        std::string deletePropertiesSql = "DELETE FROM TagProperties WHERE tagid = ?";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(m_digiKamDb, deletePropertiesSql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, tagId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        
        // Then delete the tag itself
        std::string deleteTagSql = "DELETE FROM Tags WHERE id = ?";
        rc = sqlite3_prepare_v2(m_digiKamDb, deleteTagSql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, tagId);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            
            if (rc != SQLITE_DONE) {
                std::cerr << "Failed to delete duplicate tag with ID: " << tagId << std::endl;
                return false;
            }
        }
    }
    
    std::cout << "Successfully removed " << tagIds.size() << " duplicate tags" << std::endl;
    return true;
}

std::string RootsMagicSync::formatPersonName(const PersonRecord& person)
{
    std::string birthYearStr = (person.birthYear == 0) ? "unknown" : std::to_string(person.birthYear);
    std::string deathYearStr = (person.deathYear == 0) ? "unknown" : std::to_string(person.deathYear);
    
    return person.given + " " + person.surname + " " + birthYearStr + "-" + deathYearStr + " (OwnerID: " + std::to_string(person.ownerId) + ")";
}

std::string RootsMagicSync::formatFamilyTagName(const FamilyRecord& family)
{
    std::string fatherFullName;
    std::string motherFullName;
    
    // Format father's full name with OwnerID
    if (family.fatherGiven.empty() && family.fatherSurname.empty() || family.fatherOwnerId == 0) {
        fatherFullName = "unknown";
    } else {
        fatherFullName = family.fatherGiven + " " + family.fatherSurname + " (OwnerID: " + std::to_string(family.fatherOwnerId) + ")";
    }
    
    // Format mother's full name with OwnerID
    if (family.motherGiven.empty() && family.motherSurname.empty() || family.motherOwnerId == 0) {
        motherFullName = "unknown";
    } else {
        motherFullName = family.motherGiven + " " + family.motherSurname + " (OwnerID: " + std::to_string(family.motherOwnerId) + ")";
    }
    
    return fatherFullName + " and " + motherFullName + " Family (FamilyID: " + std::to_string(family.familyId) + ")";
}

std::string RootsMagicSync::escapeSqlString(const std::string& str)
{
    std::string result;
    result.reserve(str.length());
    
    for (char c : str) {
        if (c == '\'') {
            result += "''";  // Escape single quotes by doubling them
        } else {
            result += c;
        }
    }
    
    return result;
}

bool RootsMagicSync::executeQuery(sqlite3* db, const std::string& query)
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

