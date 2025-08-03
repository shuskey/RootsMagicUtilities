# This will be a powershell script that will go recursively through a directory and find all image files 
# looking for images from our 'known incompatible' list.
# We will skip folders that have the word "original" in the name.
# the found incompatible images will be converted to a jpg, then the original will be moved into
# a new folder named OriginalIncompatibleImages.
# if that folder doesn't exist, we will create it.
# We will make use of the ImageMagick library & Command line tool to convert the images.

param(
    [Parameter(Mandatory=$false)]
    [string]$StartingFolder
)

# Define the incompatible file extensions (can be easily extended later)
$IncompatibleExtensions = @('.bmp', '.heic')

# Function to display help/synopsis
function Show-Help {
    Write-Host ""
    Write-Host "=== Incompatible Image Converter ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "SYNOPSIS:" -ForegroundColor Yellow
    Write-Host "    This script recursively searches through directories to find images with incompatible"
    Write-Host "    file extensions and converts them to JPG format using ImageMagick."
    Write-Host ""
    Write-Host "    Currently supported incompatible formats: BMP, HEIC"
    Write-Host ""
    Write-Host "BEHAVIOR:" -ForegroundColor Yellow
    Write-Host "    - Recursively searches the specified directory"
    Write-Host "    - Skips folders containing 'original' in their name"
    Write-Host "    - Skips folders that start with a period (dot folders like .dtrash)"
    Write-Host "    - Converts incompatible images to JPG format"
    Write-Host "    - Moves original files to 'OriginalIncompatibleImages' folder"
    Write-Host "    - Creates the backup folder if it doesn't exist"
    Write-Host ""
    Write-Host "USAGE:" -ForegroundColor Yellow
    Write-Host "    .\IncompatibleImageConvert.ps1 <StartingFolder>"
    Write-Host ""
    Write-Host "PARAMETERS:" -ForegroundColor Yellow
    Write-Host "    StartingFolder    [Required] The root directory to start the recursive search"
    Write-Host ""
    Write-Host "EXAMPLES:" -ForegroundColor Yellow
    Write-Host "    .\IncompatibleImageConvert.ps1 'C:\Photos'"
    Write-Host "    .\IncompatibleImageConvert.ps1 'D:\My Pictures\Vacation'"
    Write-Host ""
    Write-Host "REQUIREMENTS:" -ForegroundColor Yellow
    Write-Host "    - ImageMagick must be installed and available in PATH"
    Write-Host "    - PowerShell must be run with appropriate permissions for file operations"
    Write-Host ""
}

# Function to check if ImageMagick is available
function Test-ImageMagick {
    try {
        $null = magick -version 2>$null
        return $true
    }
    catch {
        return $false
    }
}

# Function to create backup directory
function New-BackupDirectory {
    param([string]$ParentPath)
    
    $backupPath = Join-Path $ParentPath "OriginalIncompatibleImages"
    if (-not (Test-Path $backupPath)) {
        try {
            New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
            Write-Host "Created backup directory: $backupPath" -ForegroundColor Green
        }
        catch {
            Write-Error "Failed to create backup directory: $backupPath. Error: $_"
            return $null
        }
    }
    return $backupPath
}

# Function to convert image using ImageMagick
function Convert-ImageToJpg {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )
    
    try {
        $convertResult = magick "$SourcePath" "$DestinationPath" 2>&1
        if ($LASTEXITCODE -eq 0) {
            return $true
        }
        else {
            Write-Warning "ImageMagick conversion failed for $SourcePath. Output: $convertResult"
            return $false
        }
    }
    catch {
        Write-Warning "Error during conversion of $SourcePath`: $_"
        return $false
    }
}

# Function to check if a path contains dot folders
function Test-ContainsDotFolder {
    param([string]$Path)
    
    $pathParts = $Path -split [regex]::Escape([System.IO.Path]::DirectorySeparatorChar)
    foreach ($part in $pathParts) {
        if ($part.StartsWith('.') -and $part.Length -gt 1) {
            return $true
        }
    }
    return $false
}

# Function to find and report skipped dot folders
function Report-SkippedDotFolders {
    param([string]$Directory)
    
    $dotFolders = Get-ChildItem -Path $Directory -Recurse -Directory | 
        Where-Object { $_.Name.StartsWith('.') }
    
    if ($dotFolders.Count -gt 0) {
        Write-Host "Skipping $($dotFolders.Count) dot folder(s):" -ForegroundColor Yellow
        foreach ($folder in $dotFolders) {
            Write-Host "  → Skipped: $($folder.FullName)" -ForegroundColor DarkYellow
        }
        Write-Host ""
    }
}

# Function to process incompatible images
function Process-IncompatibleImages {
    param([string]$Directory)
    
    $processedCount = 0
    $errorCount = 0
    
    # Report skipped dot folders first
    Report-SkippedDotFolders -Directory $Directory
    
    # Get all files with incompatible extensions, excluding 'original' folders and dot folders
    $incompatibleFiles = Get-ChildItem -Path $Directory -Recurse -File | 
        Where-Object { 
            $_.Extension.ToLower() -in $IncompatibleExtensions -and 
            $_.DirectoryName -notmatch "original" -and
            -not (Test-ContainsDotFolder -Path $_.DirectoryName)
        }
    
    if ($incompatibleFiles.Count -eq 0) {
        Write-Host "No incompatible image files found in the specified directory." -ForegroundColor Yellow
        return
    }
    
    Write-Host "Found $($incompatibleFiles.Count) incompatible image file(s) to process..." -ForegroundColor Cyan
    Write-Host ""
    
    $currentFileIndex = 0
    $totalFiles = $incompatibleFiles.Count
    
    foreach ($file in $incompatibleFiles) {
        try {
            $currentFileIndex++
            $progressPercent = [math]::Round(($currentFileIndex / $totalFiles) * 100, 1)
            
            Write-Host "[$currentFileIndex/$totalFiles - $progressPercent%] Processing: $($file.FullName)" -ForegroundColor White
            
            # Create backup directory in the same folder as the image
            $backupDir = New-BackupDirectory -ParentPath $file.DirectoryName
            if (-not $backupDir) {
                $errorCount++
                continue
            }
            
            # Generate JPG filename
            $jpgFileName = [System.IO.Path]::GetFileNameWithoutExtension($file.Name) + ".jpg"
            $jpgPath = Join-Path $file.DirectoryName $jpgFileName
            
            # Convert to JPG
            $conversionSuccess = Convert-ImageToJpg -SourcePath $file.FullName -DestinationPath $jpgPath
            
            if ($conversionSuccess) {
                # Move original to backup directory
                $backupFilePath = Join-Path $backupDir $file.Name
                
                # Handle duplicate names in backup directory
                $counter = 1
                $originalBackupPath = $backupFilePath
                while (Test-Path $backupFilePath) {
                    $fileNameWithoutExt = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
                    $fileExt = $file.Extension
                    $backupFilePath = Join-Path $backupDir "$fileNameWithoutExt`_$counter$fileExt"
                    $counter++
                }
                
                Move-Item $file.FullName $backupFilePath -Force
                Write-Host "  → Converted to JPG: $jpgPath" -ForegroundColor Green
                Write-Host "  → Original moved to: $backupFilePath" -ForegroundColor Green
                $processedCount++
            }
            else {
                Write-Warning "[$currentFileIndex/$totalFiles - $progressPercent%] Failed to convert $($file.FullName), skipping..."
                $errorCount++
            }
        }
        catch {
            Write-Error "[$currentFileIndex/$totalFiles - $progressPercent%] Error processing $($file.FullName): $_"
            $errorCount++
        }
        
        # Show progress summary every 10 files or on the last file
        if ($currentFileIndex % 10 -eq 0 -or $currentFileIndex -eq $totalFiles) {
            Write-Host "  Progress: $processedCount successful, $errorCount errors so far..." -ForegroundColor Cyan
        }
    }
    
    # Summary
    Write-Host ""
    Write-Host "=== PROCESSING COMPLETE ===" -ForegroundColor Green
    Write-Host "Total files found: $totalFiles" -ForegroundColor White
    Write-Host "Successfully processed: $processedCount files" -ForegroundColor Green
    if ($errorCount -gt 0) {
        Write-Host "Errors encountered: $errorCount files" -ForegroundColor Red
    }
    
    $successPercent = if ($totalFiles -gt 0) { [math]::Round(($processedCount / $totalFiles) * 100, 1) } else { 0 }
    Write-Host "Success rate: $successPercent%" -ForegroundColor $(if ($successPercent -eq 100) { "Green" } elseif ($successPercent -ge 80) { "Yellow" } else { "Red" })
}

# Main script logic
try {
    # Check if help is needed (no parameters provided)
    if ([string]::IsNullOrWhiteSpace($StartingFolder)) {
        Show-Help
        exit 0
    }
    
    # Validate starting folder
    if (-not (Test-Path $StartingFolder -PathType Container)) {
        Write-Error "The specified starting folder does not exist or is not a directory: $StartingFolder"
        exit 1
    }
    
    # Check if ImageMagick is available
    if (-not (Test-ImageMagick)) {
        Write-Error "ImageMagick is not installed or not available in PATH. Please install ImageMagick and ensure 'magick' command is accessible."
        Write-Host "You can download ImageMagick from: https://imagemagick.org/script/download.php" -ForegroundColor Yellow
        exit 1
    }
    
    Write-Host "Starting incompatible image conversion process..." -ForegroundColor Cyan
    Write-Host "Starting directory: $StartingFolder" -ForegroundColor White
    Write-Host "Incompatible extensions: $($IncompatibleExtensions -join ', ')" -ForegroundColor White
    Write-Host ""
    
    # Process the images
    Process-IncompatibleImages -Directory $StartingFolder
    
}
catch {
    Write-Error "An unexpected error occurred: $_"
    exit 1
}
