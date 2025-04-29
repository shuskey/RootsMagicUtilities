A C++ utility designed to work with RootsMagic genealogy database files, specifically focusing on extracting and processing data for integration with other applications. 
This utility currently exports names from RootsMagic and generates a SQL script to add them as tags in DigiKam, maintaining references back to the original RootsMagic OwnerID 
of each person as a property of that tag. 
This integration enables projects such as Timeline-Traveler to enrich family history with thumbnails and full photos, creating a bridge between RootsMagic genealogical data 
and DigiKam's digital photo collections. 

## How to Use

### Prerequisites
- Windows operating system
- RootsMagic genealogy database file
- DigiKam photo management software

### Installation Steps
1. Download and set up SQLite tools:
   ```powershell
   .\download_sqlite.ps1
   ```
2. (Optional) Add SQLite to your system PATH:
   ```powershell
   .\add_sqlite_to_path.ps1
   ```

### Using the Utility
1. Build the utility:
   ```powershell
   .\build_and_deploy.ps1
   ```

2. Run the utility with your RootsMagic database:
   ```powershell
   rootsmagic_utils.exe -d "path/to/your/rootsmagic.rmgc" [-o "output.sql"] [-p "parent_tag_name"]
   ```
   Parameters:
   - `-d` or `--database`: (Required) Path to your RootsMagic database file
   - `-o` or `--output`: (Optional) Path where the output SQL file should be saved (defaults to "tags.sql")
   - `-p` or `--parent-tag`: (Optional) Parent tag name for the imported RootsMagic names (defaults to "RootsMagic")

### Importing Tags into DigiKam
1. Close DigiKam completely
2. Locate your DigiKam database file (digikam4.db):
   - Windows: Usually in `%LOCALAPPDATA%/digikam/digikam4.db`
   - Linux: Usually in `~/.local/share/digikam/digikam4.db`
   - macOS: Usually in `~/Library/Application Support/digikam/digikam4.db`
3. **IMPORTANT**: Make a backup of your digikam4.db file
4. Import the tags using SQLite:
   ```powershell
   sqlite3 "path/to/digikam4.db" ".read path/to/your/generated/tags.sql"
   ```
5. Start DigiKam and verify the tags were imported correctly

### Troubleshooting
- If you encounter errors during import, restore your database from the backup
- Check for any special characters in tag names that might be causing issues
- Ensure the paths to both your DigiKam database and the SQL script are correct
- The script uses transactions, so the import is all-or-nothing, protecting your database from partial imports
- Tags are created using `INSERT OR IGNORE`, so running the script multiple times won't create duplicate tags

### Switching between DigiKam databases
- To switch between digiKam databases, in DigiKam: Navigate to Settings -> Configure digiKam... -> Database and select the desired database from the dropdown list according to the digiKam manual.
