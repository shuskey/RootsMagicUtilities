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

# Verify source exists
if (-not (Test-Path $sourceExe)) {
    Pop-Location
    Write-Host "Error: Built executable not found at $sourceExe" -ForegroundColor Red
    exit 1
}

# Create destination directory if it doesn't exist
if (-not (Test-Path $destPath)) {
    Write-Host "Creating destination directory..."
    New-Item -ItemType Directory -Path $destPath -Force | Out-Null
}

# Copy the executable
Write-Host "Copying executable to $destPath..."
try {
    Copy-Item -Path $sourceExe -Destination $destPath -Force
    Write-Host "Successfully deployed rootsmagic_utils.exe" -ForegroundColor Green
}
catch {
    Pop-Location
    Write-Host "Error copying executable: $_" -ForegroundColor Red
    exit 1
}

# Return to original directory
Pop-Location

Write-Host ""
Write-Host "Build and deploy completed successfully!" -ForegroundColor Green
Write-Host "Executable location: $destPath\rootsmagic_utils.exe" 