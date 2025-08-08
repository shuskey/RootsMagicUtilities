#include "rootsmagicsync.h"
#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cout << "RootsMagic to DigiKam Tag Synchronization Tool\n"
              << "Usage: " << programName << " -r <rootsmagic_db> -d <digikam_db> [options]\n"
              << "Options:\n"
              << "  -r, --rootsmagic     Path to RootsMagic database file (.rmgc or .rmtree)\n"
              << "  -d, --digikam        Path to DigiKam database file (digikam4.db)\n"
              << "  -p, --parent-tag     Parent tag name for RootsMagic tags (default: RootsMagic)\n"
              << "  -l, --lost-found     Lost & Found tag name for orphaned tags (default: Lost & Found)\n"
              << "  -h, --help           Show this help message\n\n"
              << "Examples:\n"
              << "  " << programName << " -r \"C:\\Family\\Kennedy.rmtree\" -d \"C:\\Users\\User\\AppData\\Local\\digikam\\digikam4.db\"\n"
              << "  " << programName << " -r family.rmgc -d digikam4.db -p \"Family Tree\" -l \"Orphaned Tags\"\n\n"
              << "IMPORTANT:\n"
              << "  - Close DigiKam completely before running this tool\n"
              << "  - This tool will create backup tables and can restore on error\n"
              << "  - Existing photo tag associations will be preserved\n"
              << "  - Tags are synchronized based on RootsMagic OwnerID, not names\n";
}

int main(int argc, char* argv[])
{
    std::string rootsMagicDbPath;
    std::string digiKamDbPath;
    std::string parentTag = "RootsMagic";
    std::string lostFoundTag = "Lost & Found";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-r" || arg == "--rootsmagic") && i + 1 < argc) {
            rootsMagicDbPath = argv[++i];
        }
        else if ((arg == "-d" || arg == "--digikam") && i + 1 < argc) {
            digiKamDbPath = argv[++i];
        }
        else if ((arg == "-p" || arg == "--parent-tag") && i + 1 < argc) {
            parentTag = argv[++i];
        }
        else if ((arg == "-l" || arg == "--lost-found") && i + 1 < argc) {
            lostFoundTag = argv[++i];
        }
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate required arguments
    if (rootsMagicDbPath.empty()) {
        std::cerr << "Error: RootsMagic database path is required (-r)\n\n";
        printUsage(argv[0]);
        return 1;
    }

    if (digiKamDbPath.empty()) {
        std::cerr << "Error: DigiKam database path is required (-d)\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Display configuration
    std::cout << "RootsMagic to DigiKam Tag Synchronization\n";
    std::cout << "========================================\n";
    std::cout << "RootsMagic Database: " << rootsMagicDbPath << "\n";
    std::cout << "DigiKam Database:    " << digiKamDbPath << "\n";
    std::cout << "Parent Tag:          " << parentTag << "\n";
    std::cout << "Lost & Found Tag:    " << lostFoundTag << "\n\n";

    // Create and configure the sync tool
    RootsMagicSync sync;

    // Connect to databases
    if (!sync.connectToRootsMagicDatabase(rootsMagicDbPath)) {
        std::cerr << "Failed to connect to RootsMagic database" << std::endl;
        return 1;
    }

    if (!sync.connectToDigiKamDatabase(digiKamDbPath)) {
        std::cerr << "Failed to connect to DigiKam database" << std::endl;
        return 1;
    }

    // Perform synchronization
    if (!sync.synchronizeTags(parentTag, lostFoundTag)) {
        std::cerr << "Synchronization failed" << std::endl;
        return 1;
    }

    std::cout << "\nSynchronization completed successfully!" << std::endl;
    std::cout << "You can now start DigiKam to see the updated tags." << std::endl;
    
    return 0;
}
