# Script to add sqlite_tools to system PATH
$ErrorActionPreference = "Stop"

# Get the full path to sqlite_tools directory
$toolsDir = Join-Path $PSScriptRoot "sqlite_tools"
$toolsDir = (Get-Item $toolsDir).FullName

# Check if sqlite3.exe exists
if (-not (Test-Path (Join-Path $toolsDir "sqlite3.exe"))) {
    Write-Host "Error: sqlite3.exe not found in $toolsDir"
    Write-Host "Please run download_sqlite.ps1 first to download SQLite tools"
    exit 1
}

# Get the current user's PATH
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")

# Check if the directory is already in PATH
if ($userPath -split ";" -contains $toolsDir) {
    Write-Host "sqlite_tools directory is already in your PATH"
    exit 0
}

# Add the directory to PATH
$newPath = $userPath + ";" + $toolsDir
[Environment]::SetEnvironmentVariable("PATH", $newPath, "User")

Write-Host "Successfully added sqlite_tools to your PATH"
Write-Host "Please restart your terminal/PowerShell for the changes to take effect"
Write-Host ""
Write-Host "You can now run sqlite3 from any directory"
Write-Host "Example: sqlite3 --version" 