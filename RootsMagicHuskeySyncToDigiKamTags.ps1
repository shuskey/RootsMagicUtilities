# Construct the path to rootsmagic_sync.exe
$syncPath = ".\rootsmagic_sync.exe"

# Construct the path to the RootsMagic database
$rmDbPath = "..\Documents\Family History\Huskey2024.rmtree"

# Construct the path to the DigiKam database
$dkDbPath = ".\digikam4.db"

# Check if the sync utility exists
if (-not (Test-Path $syncPath)) {
    Write-Error "rootsmagic_sync.exe not found at: $syncPath"
    exit 1
}

# Check if the RootsMagic database exists
if (-not (Test-Path $rmDbPath)) {
    Write-Error "RootsMagic database not found at: $rmDbPath"
    exit 1
}

# Check if the DigiKam database exists
if (-not (Test-Path $dkDbPath)) {
    Write-Error "DigiKam database not found at: $dkDbPath"
    exit 1
}

# Prompt for backup
Write-Host "Please ensure that the DigiKam App is closed before proceeding."
Write-Host "Would you like me to create a backup of the DigiKam database before synchronization? (Y/N)"
$response = Read-Host

if ($response -eq 'Y' -or $response -eq 'y') {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupPath = "digikam4.db_backup_$timestamp"
    
    try {
        Copy-Item $dkDbPath $backupPath
        Write-Host "Backup created successfully: $backupPath" -ForegroundColor Green
    } catch {
        Write-Error "Failed to create backup: $_"
        exit 1
    }
} else {
    Write-Host "Proceeding without backup (not recommended)" -ForegroundColor Yellow
}

# Execute the sync utility
Write-Host ""
Write-Host "Starting RootsMagic to DigiKam synchronization..." -ForegroundColor Cyan
Write-Host "RootsMagic Database: $rmDbPath"
Write-Host "DigiKam Database:    $dkDbPath"
Write-Host ""

try {
    & $syncPath -r $rmDbPath -d $dkDbPath
    
    # Check the exit code
    if ($LASTEXITCODE -ne 0) {
        Write-Error "rootsmagic_sync.exe failed with exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
    
    Write-Host ""
    Write-Host "Synchronization completed successfully!" -ForegroundColor Green
    Write-Host "You can now start DigiKam to see the updated tags." -ForegroundColor Green
    
} catch {
    Write-Error "Error during synchronization: $_"
    exit 1
}

Write-Host ""
Write-Host "Summary:" -ForegroundColor Cyan
Write-Host "- RootsMagic people have been synchronized with DigiKam tags"
Write-Host "- Updated names and dates are reflected in DigiKam"
Write-Host "- Any orphaned tags have been moved to 'Lost & Found'"
Write-Host "- All changes are transaction-safe with automatic rollback on error"
