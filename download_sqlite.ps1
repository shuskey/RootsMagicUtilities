# Script to download SQLite tools for Windows
$ErrorActionPreference = "Stop"

# Create tools directory if it doesn't exist
$toolsDir = "sqlite_tools"
if (-not (Test-Path $toolsDir)) {
    New-Item -ItemType Directory -Path $toolsDir | Out-Null
}

# Download URL for SQLite tools (updated to latest version)
$sqliteUrl = "https://www.sqlite.org/2025/sqlite-tools-win-x64-3490100.zip"
$zipFile = Join-Path $toolsDir "sqlite.zip"

Write-Host "Downloading SQLite tools..."
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $sqliteUrl -OutFile $zipFile
}
catch {
    Write-Host "Error downloading SQLite tools: $_"
    exit 1
}

# Extract the ZIP file
Write-Host "Extracting SQLite tools..."
try {
    Expand-Archive -Path $zipFile -DestinationPath $toolsDir -Force
    # Move files from the nested directory to tools directory
    Get-ChildItem -Path "$toolsDir\sqlite-tools-win32-x86-*\*" | Move-Item -Destination $toolsDir -Force
    # Remove the now-empty directory and zip file
    Remove-Item -Path "$toolsDir\sqlite-tools-win32-x86-*" -Recurse
    Remove-Item -Path $zipFile
}
catch {
    Write-Host "Error extracting SQLite tools: $_"
    exit 1
}

# Verify sqlite3.exe exists
$sqlite3Path = Join-Path $toolsDir "sqlite3.exe"
if (-not (Test-Path $sqlite3Path)) {
    Write-Host "Error: sqlite3.exe not found after extraction"
    exit 1
}

Write-Host "SQLite tools downloaded successfully!"
Write-Host "sqlite3.exe is located at: $sqlite3Path"
Write-Host ""
Write-Host "To use sqlite3, you can either:"
Write-Host "1. Run it directly using: $sqlite3Path"
Write-Host "2. Add the following directory to your PATH: $((Get-Item $toolsDir).FullName)"
Write-Host ""
Write-Host "Example command to import tags:"
Write-Host "$sqlite3Path 'path/to/digikam4.db' '.read path/to/your/generated/sql/file'" 