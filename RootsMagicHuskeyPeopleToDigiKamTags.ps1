# Construct the path to rootsmagic_utils.exe
$utilsPath = ".\rootsmagic_utils.exe"

# Construct the path to the RootsMagic database
$rmDbPath = "..\Documents\Family History\Huskey2024.rmtree"

# Check if the utility exists
if (-not (Test-Path $utilsPath)) {
    Write-Error "rootsmagic_utils.exe not found at: $utilsPath"
    exit 1
}

# Check if the database exists
if (-not (Test-Path $rmDbPath)) {
    Write-Error "RootsMagic database not found at: $rmDbPath"
    exit 1
}

# Execute the utility
Write-Host "Executing rootsmagic_utils.exe with database: $rmDbPath"
& $utilsPath -d $rmDbPath

# Check the exit code
if ($LASTEXITCODE -ne 0) {
    Write-Error "rootsmagic_utils.exe failed with exit code: $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "Export to Tags.sql completed successfully"

# Prompt for backup
Write-Host "Please ensure that the DigiKam App is closed, would you like me to create a backup of the digikam database before proceeding? (Y/N)"
$response = Read-Host

if ($response -eq 'Y') {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupPath = "digikam4.db_backup_$timestamp"
    
    if (Test-Path "digikam4.db") {
        Copy-Item "digikam4.db" $backupPath
        Write-Host "Backup created successfully: $backupPath"
    } else {
        Write-Error "digikam4.db not found in current directory"
        exit 1
    }
}

# Execute SQLite command
Write-Host "Executing SQLite import..."
sqlite3 digikam4.db ".read tags.sql"

if ($LASTEXITCODE -ne 0) {
    Write-Error "SQLite import failed with exit code: $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "Import completed successfully, RootsMagic Names are now in the DigiKam Tags table"
