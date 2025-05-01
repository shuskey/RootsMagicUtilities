# Script to build and deploy RootsMagic utility
$ErrorActionPreference = "Stop"

# Change to the build directory
$buildDir = Join-Path $PSScriptRoot "build"
if (-not (Test-Path $buildDir)) {
    Write-Host "Creating build directory..."
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Write-Host "Changing to build directory..."
Push-Location $buildDir

# Configure CMake if needed
if (-not (Test-Path "CMakeCache.txt")) {
    Write-Host "Configuring CMake..."
    cmake ..
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        throw "CMake configuration failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Building RootsMagic utility..."

# Run CMake build
try {
    cmake --build . --config Debug
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE"
    }
}
catch {
    Pop-Location
    Write-Host "Error during build: $_" -ForegroundColor Red
    exit 1
}

# Source and destination paths
$sourceExe = ".\src\Debug\rootsmagic_utils.exe"
$destPath = "C:\Users\shuskey\Github\Timeline-Traveler\Assets\Resources\SampleData\DigiKam"
$altDestPath = "C:\Users\shuskey\OneDrive\Pictures"

# Verify source exists
if (-not (Test-Path $sourceExe)) {
    Pop-Location
    Write-Host "Error: Built executable not found at $sourceExe" -ForegroundColor Red
    exit 1
}

# Verify destination paths exist
if (-not (Test-Path $destPath)) {
    Pop-Location
    Write-Host "Error: Destination path does not exist: ${destPath}" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $altDestPath)) {
    Pop-Location
    Write-Host "Error: Alternate destination path does not exist: ${altDestPath}" -ForegroundColor Red
    exit 1
}

# Copy the executable to both locations
Write-Host "Copying executable to $destPath..."
try {
    Copy-Item -Path $sourceExe -Destination $destPath -Force
    Write-Host "Successfully deployed rootsmagic_utils.exe to ${destPath}" -ForegroundColor Green
}
catch {
    Pop-Location
    Write-Host "Error copying executable to ${destPath}: $_" -ForegroundColor Red
    exit 1
}

Write-Host "Copying executable to $altDestPath..."
try {
    Copy-Item -Path $sourceExe -Destination $altDestPath -Force
    Write-Host "Successfully deployed rootsmagic_utils.exe to ${altDestPath}" -ForegroundColor Green
}
catch {
    Pop-Location
    Write-Host "Error copying executable to ${altDestPath}: $_" -ForegroundColor Red
    exit 1
}

# Return to original directory
Pop-Location

Write-Host ""
Write-Host "Build and deploy completed successfully!" -ForegroundColor Green
Write-Host "Executable locations:"
Write-Host "- ${destPath}\rootsmagic_utils.exe"
Write-Host "- ${altDestPath}\rootsmagic_utils.exe" 