#include "rootsmagicimporter.h"
#include <iostream>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <cctype>

// Case-insensitive string comparison function for SQLite
static int rmnocaseCompare(void*, int len1, const void* str1, int len2, const void* str2) {
    const char* s1 = static_cast<const char*>(str1);
    const char* s2 = static_cast<const char*>(str2);
    
    for (int i = 0; i < len1 && i < len2; i++) {
        char c1 = std::tolower(s1[i]);
        char c2 = std::tolower(s2[i]);
        if (c1 != c2) {
            return c1 - c2;
        }
    }
    return len1 - len2;
}

RootsMagicImporter::RootsMagicImporter() : m_database(nullptr)
{
}

RootsMagicImporter::~RootsMagicImporter()
{
    if (m_database) {
        sqlite3_close(m_database);
    }
    if (m_outputFile.is_open()) {
        m_outputFile.close();
    }
}

bool RootsMagicImporter::connectToDatabase(const std::string& dbPath)
{
    int rc = sqlite3_open(dbPath.c_str(), &m_database);
    if (rc) {
        std::cerr << "Failed to connect to RootsMagic database: " << sqlite3_errmsg(m_database) << std::endl;
        return false;
    }

    // Register the RMNOCASE collation
    rc = sqlite3_create_collation(m_database, "RMNOCASE", SQLITE_UTF8, nullptr, rmnocaseCompare);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create RMNOCASE collation: " << sqlite3_errmsg(m_database) << std::endl;
        return false;
    }

    return true;
}

std::string RootsMagicImporter::generateParentTagSql(const std::string& tagName)
{
    std::string escapedName = escapeSqlString(tagName);
    return "INSERT OR IGNORE INTO Tags (name, pid, icon, iconkde) "
           "VALUES ('" + escapedName + "', 0, NULL, NULL);\n";
}

std::string RootsMagicImporter::generatePersonTagSql(const std::string& fullName, int ownerId, const std::string& parentTagName)
{
    std::string escapedName = escapeSqlString(fullName);
    std::string escapedParentName = escapeSqlString(parentTagName);
    std::stringstream sql;
    
    sql << "-- Create tag for: " << fullName << "\n";
    sql << "INSERT OR IGNORE INTO Tags (name, pid, icon, iconkde) "
        << "SELECT '" << escapedName << "', id, NULL, 'user' FROM Tags WHERE name='" << escapedParentName << "';\n";

    // Insert owner_id property only if it doesn't exist
    sql << "INSERT OR IGNORE INTO TagProperties (tagid, property, value) "
        << "SELECT t.id, 'rootsmagic_owner_id', '" << ownerId << "' "
        << "FROM Tags t "
        << "WHERE t.name='" << escapedName << "' "
        << "AND NOT EXISTS (SELECT 1 FROM TagProperties tp "
        << "               WHERE tp.tagid = t.id "
        << "               AND tp.property = 'rootsmagic_owner_id');\n";

    // Insert person property only if it doesn't exist
    sql << "INSERT OR IGNORE INTO TagProperties (tagid, property, value) "
        << "SELECT t.id, 'person', '" << escapedName << "' "
        << "FROM Tags t "
        << "WHERE t.name='" << escapedName << "' "
        << "AND NOT EXISTS (SELECT 1 FROM TagProperties tp "
        << "               WHERE tp.tagid = t.id "
        << "               AND tp.property = 'person');\n";

    return sql.str();
}

std::string RootsMagicImporter::escapeSqlString(const std::string& str)
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

bool RootsMagicImporter::exportNamesToSql(const std::string& outputPath, const std::string& parentTagName)
{
    if (!m_database) {
        std::cerr << "Database is not open" << std::endl;
        return false;
    }

    // Open output file
    m_outputFile.open(outputPath);
    if (!m_outputFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return false;
    }

    // Write header with instructions
    m_outputFile << "-- RootsMagic to digiKam tag import\n";
    time_t now = time(nullptr);
    char timeStr[26];
    ctime_s(timeStr, sizeof(timeStr), &now);
    m_outputFile << "-- Generated: " << timeStr << "\n";
    m_outputFile << "--\n";
    m_outputFile << "-- INSTRUCTIONS FOR IMPORTING TAGS INTO DIGIKAM:\n";
    m_outputFile << "--\n";
    m_outputFile << "-- 1. Close digiKam completely\n";
    m_outputFile << "-- 2. Locate your digiKam database file:\n";
    m_outputFile << "--    - Windows: Usually in %LOCALAPPDATA%/digikam/digikam4.db\n";
    m_outputFile << "--    - Linux: Usually in ~/.local/share/digikam/digikam4.db\n";
    m_outputFile << "--    - macOS: Usually in ~/Library/Application Support/digikam/digikam4.db\n";
    m_outputFile << "-- 3. Make a backup of your digikam4.db file!\n";
    m_outputFile << "-- 4. Use the sqlite3 command line tool to import this file:\n";
    m_outputFile << "--    sqlite3 path/to/digikam4.db \".read " << outputPath << "\"\n";
    m_outputFile << "-- 5. Start digiKam and verify the tags were imported correctly\n";
    m_outputFile << "--\n";
    m_outputFile << "-- Note: If you get any errors during import, restore from your backup\n";
    m_outputFile << "--       and check for any special characters in tag names.\n";
    m_outputFile << "-- Note: All people will be imported as tags under a 'RootsMagic' parent tag\n";
    m_outputFile << "--       (unless a different parent tag name was specified).\n";
    m_outputFile << "-- Note: A helper script may have been deployed to your digikam folder that contains something like:\n";
    m_outputFile << "-- .\\rootsmagic_utils.exe -d '..\\RootMagic\\Kennedy.rmtree'\n";
    m_outputFile << "-- sqlite3 \".\\digikam4.db\" \".read tags.sql\"\n";
    m_outputFile << "--\n\n";

    m_outputFile << "BEGIN TRANSACTION;\n\n";

    // Create parent tag
    m_outputFile << generateParentTagSql(parentTagName) << "\n";

    // Query RootsMagic names table without using RMNOCASE collation
    const char* sql = "SELECT DISTINCT OwnerID, Surname, Given, BirthYear, DeathYear FROM NameTable";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_database, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to query NameTable: " << sqlite3_errmsg(m_database) << std::endl;
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int ownerId = sqlite3_column_int(stmt, 0);
        std::string surname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string given = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int birthYear = sqlite3_column_int(stmt, 3);
        int deathYear = sqlite3_column_int(stmt, 4);
        
        // Trim whitespace
        surname = surname.substr(0, surname.find_last_not_of(" \t") + 1);
        given = given.substr(0, given.find_last_not_of(" \t") + 1);
        
        // Format birth and death years
        std::string birthYearStr = (birthYear == 0) ? "unknown" : std::to_string(birthYear);
        std::string deathYearStr = (deathYear == 0) ? "unknown" : std::to_string(deathYear);
        
        // Assemble the tag name in the format: "Given Surname BirthYear-DeathYear"
        std::string rootsMagicTagName = given + " " + surname + " " + birthYearStr + "-" + deathYearStr;

        m_outputFile << generatePersonTagSql(rootsMagicTagName, ownerId, parentTagName) << "\n";
    }

    sqlite3_finalize(stmt);

    // Write footer
    m_outputFile << "COMMIT;\n";
    m_outputFile.close();

    return true;
} 