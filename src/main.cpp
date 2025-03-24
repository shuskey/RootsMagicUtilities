#include "rootsmagicimporter.h"
#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " -d <database_path> [-o <output_path>] [-p <parent_tag>]\n"
              << "Options:\n"
              << "  -d, --database    Path to RootsMagic database file\n"
              << "  -o, --output      Output SQL file path (default: tags.sql)\n"
              << "  -p, --parent-tag  Parent tag name for imported RootsMagic names (default: RootsMagic)\n";
}

int main(int argc, char *argv[])
{
    std::string dbPath;
    std::string outputPath = "tags.sql";
    std::string parentTag = "RootsMagic";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-d" || arg == "--database") && i + 1 < argc) {
            dbPath = argv[++i];
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputPath = argv[++i];
        }
        else if ((arg == "-p" || arg == "--parent-tag") && i + 1 < argc) {
            parentTag = argv[++i];
        }
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (dbPath.empty()) {
        std::cerr << "Error: Database path is required\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Create and use the importer
    RootsMagicImporter importer;
    
    if (!importer.connectToDatabase(dbPath)) {
        return 1;
    }

    if (!importer.exportNamesToSql(outputPath, parentTag)) {
        return 1;
    }

    std::cout << "Import completed successfully\n";
    return 0;
} 