#pragma once

#include <string>
#include <fstream>
#include "sqlite3.h"

class RootsMagicImporter {
public:
    RootsMagicImporter();
    ~RootsMagicImporter();

    // Connect to RootsMagic database
    bool connectToDatabase(const std::string& dbPath);

    // Export names to a SQL script that can be imported into digiKam
    bool exportNamesToSql(const std::string& outputPath, const std::string& parentTagName = "RootsMagic");

private:
    // Generate SQL for creating a parent tag
    std::string generateParentTagSql(const std::string& tagName);

    // Generate SQL for creating a person tag
    std::string generatePersonTagSql(const std::string& fullName, int ownerId, const std::string& parentTagName);

    // Helper to escape SQL strings
    std::string escapeSqlString(const std::string& str);

    sqlite3* m_database;
    std::ofstream m_outputFile;
}; 