param(
    [Parameter(Mandatory=$false)]
    [string]$StartDirectory,

    [Parameter(Mandatory=$false)]
    [string]$FileType,

    [Parameter(Mandatory=$false)]
    [switch]$Help
)

function Show-Usage {
    Write-Host "`nFileTypeDiscovery.ps1 - A utility to analyze file types in a directory"
    Write-Host "`nUsage:"
    Write-Host "  .\FileTypeDiscovery.ps1 -StartDirectory <path> [-FileType <extension>] [-Help]"
    Write-Host "`nParameters:"
    Write-Host "  -StartDirectory <path>  : Directory to analyze (required)"
    Write-Host "  -FileType <extension>   : Optional file extension to list files (e.g., 'bmp' or '.bmp')"
    Write-Host "  -Help                   : Show this help message"
    Write-Host "`nExamples:"
    Write-Host "  .\FileTypeDiscovery.ps1 -StartDirectory C:\MyFiles"
    Write-Host "  .\FileTypeDiscovery.ps1 -StartDirectory C:\MyFiles -FileType bmp"
    Write-Host "  .\FileTypeDiscovery.ps1 -Help"
    exit 0
}

# Show help if requested or no parameters provided
if ($Help -or (-not $StartDirectory -and -not $FileType)) {
    Show-Usage
}

# Verify directory exists
if (-not (Test-Path -Path $StartDirectory -PathType Container)) {
    Write-Error "Directory '$StartDirectory' does not exist or is not accessible."
    exit 1
}

# If FileType is specified, list all files of that type
if ($FileType) {
    # Remove leading dot if present and convert to lowercase
    $FileType = $FileType.TrimStart('.').ToLower()
    
    Get-ChildItem -Path $StartDirectory -Recurse -File | 
        Where-Object { $_.Extension.TrimStart('.').ToLower() -eq $FileType } |
        ForEach-Object { $_.FullName }
    exit 0
}

# Initialize hashtable to store extension counts
$extensionCounts = @{}

# Get all files recursively and process them
Get-ChildItem -Path $StartDirectory -Recurse -File | ForEach-Object {
    # Get the extension (converted to lowercase for consistency)
    $extension = $_.Extension.ToLower()
    
    # If empty extension, categorize as "no extension"
    if ([string]::IsNullOrEmpty($extension)) {
        $extension = "(no extension)"
    }
    
    # Increment count for this extension
    if ($extensionCounts.ContainsKey($extension)) {
        $extensionCounts[$extension]++
    } else {
        $extensionCounts[$extension] = 1
    }
}

# Output results
Write-Host "`nFile Extension Report"
Write-Host "-------------------"
$extensionCounts.GetEnumerator() | 
    Sort-Object -Property Value -Descending |
    ForEach-Object {
        $extension = if ($_.Key -eq "(no extension)") { $_.Key } else { $_.Key.TrimStart('.') }
        Write-Host ("{0,-15} : {1,6:N0} files" -f $extension, $_.Value)
    }

Write-Host "`nTotal unique file extensions found: $($extensionCounts.Count)"
Write-Host "Total files processed: $($extensionCounts.Values | Measure-Object -Sum | Select-Object -ExpandProperty Sum)"
