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

    // Phase 1: Create backup
    std::cout << "Creating backup tables..." << std::endl;
    if (!createBackupTables()) {
        std::cerr << "Failed to create backup tables" << std::endl;
        return false;
    }

    // Begin transaction
    if (!executeQuery(m_digiKamDb, "BEGIN TRANSACTION;")) {
        return false;
    }

    try {
        // Phase 2: Load data
        std::cout << "Loading RootsMagic people..." << std::endl;
        auto rmPeople = loadRootsMagicPeople();
        std::cout << "Found " << rmPeople.size() << " people in RootsMagic" << std::endl;

        std::cout << "Loading existing DigiKam tags..." << std::endl;
        auto existingTags = loadExistingDigiKamTags(parentTagName);
        std::cout << "Found " << existingTags.size() << " existing RootsMagic tags in DigiKam" << std::endl;
        
        // If we found very few existing tags, try to migrate legacy tags
        if (existingTags.size() < rmPeople.size() / 2) {
            std::cout << "Detected possible legacy tags without owner_id properties. Attempting migration..." << std::endl;
            if (migrateLegacyTags(parentTagName, rmPeople)) {
                // Reload existing tags after migration
                existingTags = loadExistingDigiKamTags(parentTagName);
                std::cout << "After migration: Found " << existingTags.size() << " existing RootsMagic tags in DigiKam" << std::endl;
            }
        }
        
        // Migrate existing tags to include OwnerID in their names if they don't already have it
        std::cout << "Checking for tags that need OwnerID added to names..." << std::endl;
        if (migrateTagNamesToIncludeOwnerID(parentTagName, rmPeople)) {
            // Reload existing tags after OwnerID name migration
            existingTags = loadExistingDigiKamTags(parentTagName);
            std::cout << "After OwnerID migration: Found " << existingTags.size() << " existing RootsMagic tags in DigiKam" << std::endl;
        }
        
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
        
        // Track which existing tags are still valid
        std::vector<int> validTagIds;
        int newPeopleCount = 0;
        
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
                if (createPersonTag(person, parentTagName)) {
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
        std::cout << "Restoring from backup..." << std::endl;
        restoreFromBackup();
        return false;
    }
}

bool RootsMagicSync::createBackupTables()
{
    // Create backup of Tags that belong to RootsMagic hierarchy
    std::string backupTagsSql = R"(
        CREATE TABLE IF NOT EXISTS Tags_Backup AS 
        SELECT t.* FROM Tags t 
        WHERE t.pid IN (SELECT id FROM Tags WHERE name IN ('RootsMagic', 'Lost & Found'))
           OR t.name IN ('RootsMagic', 'Lost & Found');
    )";

    std::string backupPropertiesSql = R"(
        CREATE TABLE IF NOT EXISTS TagProperties_Backup AS 
        SELECT tp.* FROM TagProperties tp 
        WHERE tp.tagid IN (
            SELECT t.id FROM Tags t 
            WHERE t.pid IN (SELECT id FROM Tags WHERE name IN ('RootsMagic', 'Lost & Found'))
               OR t.name IN ('RootsMagic', 'Lost & Found')
        );
    )";

    return executeQuery(m_digiKamDb, "DROP TABLE IF EXISTS Tags_Backup;") &&
           executeQuery(m_digiKamDb, "DROP TABLE IF EXISTS TagProperties_Backup;") &&
           executeQuery(m_digiKamDb, backupTagsSql) &&
           executeQuery(m_digiKamDb, backupPropertiesSql);
}

bool RootsMagicSync::restoreFromBackup()
{
    // This is a simplified restore - in production you'd want more sophisticated recovery
    std::string deleteSql = R"(
        DELETE FROM TagProperties WHERE tagid IN (
            SELECT t.id FROM Tags t 
            WHERE t.pid IN (SELECT id FROM Tags WHERE name IN ('RootsMagic', 'Lost & Found'))
               OR t.name IN ('RootsMagic', 'Lost & Found')
        );
    )";

    std::string deleteTagsSql = R"(
        DELETE FROM Tags WHERE pid IN (SELECT id FROM Tags WHERE name IN ('RootsMagic', 'Lost & Found'))
                           OR name IN ('RootsMagic', 'Lost & Found');
    )";

    return executeQuery(m_digiKamDb, deleteSql) &&
           executeQuery(m_digiKamDb, deleteTagsSql) &&
           executeQuery(m_digiKamDb, "INSERT INTO Tags SELECT * FROM Tags_Backup;") &&
           executeQuery(m_digiKamDb, "INSERT INTO TagProperties SELECT * FROM TagProperties_Backup;");
}

std::vector<PersonRecord> RootsMagicSync::loadRootsMagicPeople()
{
    std::vector<PersonRecord> people;
    
    // Try simple query first to avoid RootsMagic-specific collation issues
    const char* sql = "SELECT DISTINCT OwnerID, Surname, Given, BirthYear, DeathYear FROM NameTable";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_rootsMagicDb, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to query RootsMagic NameTable: " << sqlite3_errmsg(m_rootsMagicDb) << std::endl;
        return people;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PersonRecord person;
        person.ownerId = sqlite3_column_int(stmt, 0);
        person.surname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        person.given = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        person.birthYear = sqlite3_column_int(stmt, 3);
        person.deathYear = sqlite3_column_int(stmt, 4);
        
        // Trim whitespace
        person.surname = person.surname.substr(0, person.surname.find_last_not_of(" \t") + 1);
        person.given = person.given.substr(0, person.given.find_last_not_of(" \t") + 1);
        
        person.formattedName = formatPersonName(person);
        people.push_back(person);
    }

    sqlite3_finalize(stmt);
    return people;
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

bool RootsMagicSync::createPersonTag(const PersonRecord& person, const std::string& parentTagName)
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
    
    // Create the tag
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
    sqlite3_bind_text(stmt, 2, parentTagName.c_str(), -1, SQLITE_STATIC);
    
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

bool RootsMagicSync::migrateLegacyTags(const std::string& parentTagName, const std::vector<PersonRecord>& rmPeople)
{
    std::cout << "Migrating legacy tags by adding missing rootsmagic_owner_id properties..." << std::endl;
    
    // Load all tags under RootsMagic parent that don't have rootsmagic_owner_id property
    std::string sql = R"(
        SELECT t.id, t.name FROM Tags t 
        WHERE t.pid = (SELECT id FROM Tags WHERE name = ?)
        AND NOT EXISTS (
            SELECT 1 FROM TagProperties tp 
            WHERE tp.tagid = t.id AND tp.property = 'rootsmagic_owner_id'
        )
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_digiKamDb, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare legacy tag query: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, parentTagName.c_str(), -1, SQLITE_STATIC);
    
    std::vector<std::pair<int, std::string>> legacyTags;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int tagId = sqlite3_column_int(stmt, 0);
        std::string tagName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        legacyTags.push_back({tagId, tagName});
    }
    sqlite3_finalize(stmt);
    
    std::cout << "Found " << legacyTags.size() << " legacy tags to migrate" << std::endl;
    
    int migratedCount = 0;
    for (const auto& [tagId, tagName] : legacyTags) {
        // Try to match this tag name with a RootsMagic person
        for (const auto& person : rmPeople) {
            if (person.formattedName == tagName) {
                // Add the missing rootsmagic_owner_id property
                std::string addOwnerIdSql = "INSERT INTO TagProperties (tagid, property, value) VALUES (?, 'rootsmagic_owner_id', ?)";
                rc = sqlite3_prepare_v2(m_digiKamDb, addOwnerIdSql.c_str(), -1, &stmt, nullptr);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, tagId);
                    sqlite3_bind_int(stmt, 2, person.ownerId);
                    rc = sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    
                    if (rc == SQLITE_DONE) {
                        migratedCount++;
                        if (migratedCount <= 5) { // Show first 5 for feedback
                            std::cout << "Migrated: " << tagName << " (OwnerID: " << person.ownerId << ")" << std::endl;
                        }
                    }
                }
                break; // Found match, move to next tag
            }
        }
    }
    
    std::cout << "Successfully migrated " << migratedCount << " legacy tags" << std::endl;
    return migratedCount > 0;
}

bool RootsMagicSync::migrateTagNamesToIncludeOwnerID(const std::string& parentTagName, const std::vector<PersonRecord>& rmPeople)
{
    std::cout << "Migrating existing tags to include OwnerID in names..." << std::endl;
    
    // Load all tags under RootsMagic parent that have rootsmagic_owner_id property but don't have "(OwnerID: " in their name
    std::string sql = R"(
        SELECT t.id, t.name, CAST(tp.value AS INTEGER) as owner_id 
        FROM Tags t 
        JOIN TagProperties tp ON t.id = tp.tagid 
        WHERE t.pid = (SELECT id FROM Tags WHERE name = ?)
        AND tp.property = 'rootsmagic_owner_id'
        AND t.name NOT LIKE '%(OwnerID: %'
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_digiKamDb, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare OwnerID migration query: " << sqlite3_errmsg(m_digiKamDb) << std::endl;
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, parentTagName.c_str(), -1, SQLITE_STATIC);
    
    struct TagToMigrate {
        int tagId;
        std::string currentName;
        int ownerId;
    };
    
    std::vector<TagToMigrate> tagsToMigrate;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TagToMigrate tag;
        tag.tagId = sqlite3_column_int(stmt, 0);
        tag.currentName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        tag.ownerId = sqlite3_column_int(stmt, 2);
        tagsToMigrate.push_back(tag);
    }
    sqlite3_finalize(stmt);
    
    std::cout << "Found " << tagsToMigrate.size() << " tags to migrate to include OwnerID" << std::endl;
    
    int migratedCount = 0;
    for (const auto& tag : tagsToMigrate) {
        // Find the corresponding PersonRecord to get the proper formatted name
        PersonRecord matchedPerson;
        bool foundMatch = false;
        
        for (const auto& person : rmPeople) {
            if (person.ownerId == tag.ownerId) {
                matchedPerson = person;
                foundMatch = true;
                break;
            }
        }
        
        if (!foundMatch) {
            std::cout << "Warning: No matching person found for tag '" << tag.currentName << "' (OwnerID: " << tag.ownerId << ")" << std::endl;
            continue;
        }
        
        // Update the tag name to include OwnerID
        std::string updateSql = "UPDATE Tags SET name = ? WHERE id = ?";
        rc = sqlite3_prepare_v2(m_digiKamDb, updateSql.c_str(), -1, &stmt, nullptr);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, matchedPerson.formattedName.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, tag.tagId);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            
            if (rc == SQLITE_DONE) {
                migratedCount++;
                if (migratedCount <= 5) { // Show first 5 for feedback
                    std::cout << "Migrated: '" << tag.currentName << "' -> '" << matchedPerson.formattedName << "'" << std::endl;
                }
                
                // Also update the person property to match
                std::string updatePersonSql = "UPDATE TagProperties SET value = ? WHERE tagid = ? AND property = 'person'";
                rc = sqlite3_prepare_v2(m_digiKamDb, updatePersonSql.c_str(), -1, &stmt, nullptr);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, matchedPerson.formattedName.c_str(), -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmt, 2, tag.tagId);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            } else {
                std::cerr << "Failed to migrate tag '" << tag.currentName << "': " << sqlite3_errmsg(m_digiKamDb) << std::endl;
            }
        }
    }
    
    std::cout << "Successfully migrated " << migratedCount << " tags to include OwnerID in names" << std::endl;
    return migratedCount > 0 || tagsToMigrate.empty(); // Success if we migrated some or there were none to migrate
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
