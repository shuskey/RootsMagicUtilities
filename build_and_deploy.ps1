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
$KennedyDigiKamDestPath = "C:\Users\shuskey\Github\Timeline-Traveler\Assets\Resources\SampleData\DigiKam"
$HuskeyDigiKamDestPath = "C:\Users\shuskey\OneDrive\Pictures"

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
if (-not (Test-Path $KennedyDigiKamDestPath)) {
    Write-Host "Error: Destination path does not exist: ${KennedyDigiKamDestPath}" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $HuskeyDigiKamDestPath)) {
    Write-Host "Error: Alternate destination path does not exist: ${HuskeyDigiKamDestPath}" -ForegroundColor Red
    exit 1
}

# Copy the executables to both locations
Write-Host "Copying executables and scripts to $KennedyDigiKamDestPath"
try {
    Copy-Item -Path $sourceUtilsExe -Destination $KennedyDigiKamDestPath -Force
    Copy-Item -Path $sourceSyncExe -Destination $KennedyDigiKamDestPath -Force
    Copy-Item -Path $sourceKennedyPs1 -Destination $KennedyDigiKamDestPath -Force
    Copy-Item -Path $sourceKennedySyncPs1 -Destination $KennedyDigiKamDestPath -Force
    Write-Host "Successfully deployed to ${KennedyDigiKamDestPath}" -ForegroundColor Green
}
catch {
    Write-Host "Error deploying to ${KennedyDigiKamDestPath}: $_" -ForegroundColor Red
    exit 1
}

Write-Host "Copying executables and scripts to $HuskeyDigiKamDestPath"
try {
    Copy-Item -Path $sourceUtilsExe -Destination $HuskeyDigiKamDestPath -Force
    Copy-Item -Path $sourceSyncExe -Destination $HuskeyDigiKamDestPath -Force
    Copy-Item -Path $sourceHuskeyPs1 -Destination $HuskeyDigiKamDestPath -Force
    Copy-Item -Path $sourceHuskeySyncPs1 -Destination $HuskeyDigiKamDestPath -Force
    Write-Host "Successfully deployed to ${HuskeyDigiKamDestPath}" -ForegroundColor Green
}
catch {
    Write-Host "Error deploying to ${HuskeyDigiKamDestPath}: $_" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Build and deploy completed successfully!" -ForegroundColor Green
Write-Host "Deployed to these locations:"
Write-Host "Kennedy Location:"
Write-Host "- ${KennedyDigiKamDestPath}\$(Split-Path $sourceUtilsExe -Leaf)"
Write-Host "- ${KennedyDigiKamDestPath}\$(Split-Path $sourceSyncExe -Leaf)"
Write-Host "- ${KennedyDigiKamDestPath}\$(Split-Path $sourceKennedyPs1 -Leaf)"
Write-Host "- ${KennedyDigiKamDestPath}\$(Split-Path $sourceKennedySyncPs1 -Leaf)"
Write-Host "Huskey Location:"
Write-Host "- ${HuskeyDigiKamDestPath}\$(Split-Path $sourceUtilsExe -Leaf)" 
Write-Host "- ${HuskeyDigiKamDestPath}\$(Split-Path $sourceSyncExe -Leaf)"
Write-Host "- ${HuskeyDigiKamDestPath}\$(Split-Path $sourceHuskeyPs1 -Leaf)"
Write-Host "- ${HuskeyDigiKamDestPath}\$(Split-Path $sourceHuskeySyncPs1 -Leaf)"