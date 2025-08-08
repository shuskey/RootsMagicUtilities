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
    // Backup functions
    bool createBackupTables();
    bool restoreFromBackup();

    // Data loading functions
    std::vector<PersonRecord> loadRootsMagicPeople();
    std::unordered_map<int, DigiKamTag> loadExistingDigiKamTags(const std::string& parentTagName);

    // Sync operations
    bool ensureParentTagExists(const std::string& tagName);
    bool createPersonTag(const PersonRecord& person, const std::string& parentTagName);
    bool updatePersonTag(int tagId, const PersonRecord& person);
    bool moveOrphanedTagsToLostFound(const std::vector<int>& orphanedTagIds, 
                                    const std::string& lostFoundTagName,
                                    const std::unordered_map<int, DigiKamTag>& existingTags);
    bool rescueTagFromLostFound(const PersonRecord& person, const std::string& parentTagName,
                               const std::unordered_map<int, DigiKamTag>& lostFoundTags);
    bool migrateLegacyTags(const std::string& parentTagName, const std::vector<PersonRecord>& rmPeople);
    bool removeDuplicateTags(const std::vector<int>& tagIds);

    // Utility functions
    std::string formatPersonName(const PersonRecord& person);
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
