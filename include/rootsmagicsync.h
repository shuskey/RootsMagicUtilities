#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "sqlite3.h"

struct PersonRecord {
    int ownerId;
    std::string surname;
    std::string given;
    int birthYear;
    int deathYear;
    std::string formattedName;
    int familyId;  // New field to track family ID
};

struct FamilyRecord {
    int familyId;
    int fatherOwnerId;
    int motherOwnerId;
    std::string fatherGiven;
    std::string fatherSurname;
    std::string motherGiven;
    std::string motherSurname;
    std::string familyTagName;
};

struct DigiKamTag {
    int tagId;
    std::string name;
    int ownerId;
    bool isOrphaned;
};

class RootsMagicSync {
public:
    RootsMagicSync();
    ~RootsMagicSync();

    // Connect to both databases
    bool connectToRootsMagicDatabase(const std::string& rmDbPath);
    bool connectToDigiKamDatabase(const std::string& dkDbPath);

    // Main synchronization function
    bool synchronizeTags(const std::string& parentTagName = "RootsMagic", 
                        const std::string& lostFoundTagName = "Lost & Found");

private:
    // Data loading functions
    std::vector<PersonRecord> loadRootsMagicPeople();
    std::unordered_map<int, FamilyRecord> loadFamilyData();
    std::unordered_map<int, DigiKamTag> loadExistingDigiKamTags(const std::string& parentTagName);

    // Sync operations
    bool ensureParentTagExists(const std::string& tagName);
    bool createFamilyTag(const FamilyRecord& family, const std::string& parentTagName);
    bool createPersonTag(const PersonRecord& person, const std::string& parentTagName, 
                        const std::unordered_map<int, FamilyRecord>& families);
    bool updatePersonTag(int tagId, const PersonRecord& person);
    bool moveOrphanedTagsToLostFound(const std::vector<int>& orphanedTagIds, 
                                    const std::string& lostFoundTagName,
                                    const std::unordered_map<int, DigiKamTag>& existingTags);
    bool rescueTagFromLostFound(const PersonRecord& person, const std::string& parentTagName,
                               const std::unordered_map<int, DigiKamTag>& lostFoundTags);

    bool removeDuplicateTags(const std::vector<int>& tagIds);

    // Utility functions
    std::string formatPersonName(const PersonRecord& person);
    std::string formatFamilyTagName(const FamilyRecord& family);
    std::string escapeSqlString(const std::string& str);
    bool executeQuery(sqlite3* db, const std::string& query);
    
    // Database connections
    sqlite3* m_rootsMagicDb;
    sqlite3* m_digiKamDb;
    
    // Statistics
    int m_tagsCreated;
    int m_tagsUpdated;
    int m_tagsOrphaned;
    int m_tagsRescued;
};
