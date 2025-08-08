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

Pop-Location

# Source and destination paths
$sourceUtilsExe = ".\build\src\Debug\rootsmagic_utils.exe"
$sourceSyncExe = ".\build\src\Debug\rootsmagic_sync.exe"
$sourceHuskeyPs1 = ".\RootsMagicHuskeyPeopleToDigiKamTags.ps1"
$sourceKennedyPs1 = ".\RootsMagicKennedyPeopleToDigiKamTags.ps1"
$sourceHuskeySyncPs1 = ".\RootsMagicHuskeySyncToDigiKamTags.ps1"
$sourceKennedySyncPs1 = ".\RootsMagicKennedySyncToDigiKamTags.ps1"
$KennedyPicturesDestPath = "C:\Users\shuskey\Github\Timeline-Traveler\Assets\Resources\SampleData\DigiKam"
$HuskeyPicturesDestPath = "C:\Users\shuskey\OneDrive\Pictures"

# Verify source executables exist
if (-not (Test-Path $sourceUtilsExe)) {
    Write-Host "Error: Built executable not found at $sourceUtilsExe" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $sourceSyncExe)) {
    Write-Host "Error: Built executable not found at $sourceSyncExe" -ForegroundColor Red
    exit 1
}

# Verify destination paths exist
if (-not (Test-Path $KennedyPicturesDestPath)) {
    Write-Host "Error: Destination path does not exist: ${KennedyPicturesDestPath}" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $HuskeyPicturesDestPath)) {
    Write-Host "Error: Alternate destination path does not exist: ${HuskeyPicturesDestPath}" -ForegroundColor Red
    exit 1
}

# Copy the executables to both locations
Write-Host "Copying executables and scripts to $KennedyPicturesDestPath"
try {
    Copy-Item -Path $sourceUtilsExe -Destination $KennedyPicturesDestPath -Force
    Copy-Item -Path $sourceSyncExe -Destination $KennedyPicturesDestPath -Force
    Copy-Item -Path $sourceKennedyPs1 -Destination $KennedyPicturesDestPath -Force
    Copy-Item -Path $sourceKennedySyncPs1 -Destination $KennedyPicturesDestPath -Force
    Write-Host "Successfully deployed to ${KennedyPicturesDestPath}" -ForegroundColor Green
}
catch {
    Write-Host "Error deploying to ${KennedyPicturesDestPath}: $_" -ForegroundColor Red
    exit 1
}

Write-Host "Copying executables and scripts to $HuskeyPicturesDestPath"
try {
    Copy-Item -Path $sourceUtilsExe -Destination $HuskeyPicturesDestPath -Force
    Copy-Item -Path $sourceSyncExe -Destination $HuskeyPicturesDestPath -Force
    Copy-Item -Path $sourceHuskeyPs1 -Destination $HuskeyPicturesDestPath -Force
    Copy-Item -Path $sourceHuskeySyncPs1 -Destination $HuskeyPicturesDestPath -Force
    Write-Host "Successfully deployed to ${HuskeyPicturesDestPath}" -ForegroundColor Green
}
catch {
    Write-Host "Error deploying to ${HuskeyPicturesDestPath}: $_" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Build and deploy completed successfully!" -ForegroundColor Green
Write-Host "Deployed to these locations:"
Write-Host "Kennedy Location:"
Write-Host "- ${KennedyPicturesDestPath}\$(Split-Path $sourceUtilsExe -Leaf)"
Write-Host "- ${KennedyPicturesDestPath}\$(Split-Path $sourceSyncExe -Leaf)"
Write-Host "- ${KennedyPicturesDestPath}\$(Split-Path $sourceKennedyPs1 -Leaf)"
Write-Host "- ${KennedyPicturesDestPath}\$(Split-Path $sourceKennedySyncPs1 -Leaf)"
Write-Host "Huskey Location:"
Write-Host "- ${HuskeyPicturesDestPath}\$(Split-Path $sourceUtilsExe -Leaf)" 
Write-Host "- ${HuskeyPicturesDestPath}\$(Split-Path $sourceSyncExe -Leaf)"
Write-Host "- ${HuskeyPicturesDestPath}\$(Split-Path $sourceHuskeyPs1 -Leaf)"
Write-Host "- ${HuskeyPicturesDestPath}\$(Split-Path $sourceHuskeySyncPs1 -Leaf)"