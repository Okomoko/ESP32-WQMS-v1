<#
.SYNOPSIS
    Advanced HTML/CSS/JS Minifier with Subfolder Support and UTF-8 Preservation
.DESCRIPTION
    Minifies HTML, CSS, and JavaScript files while preserving UTF-8 characters,
    emojis, and special characters. Maintains folder structure in output.
.PARAMETER InputDir
    Directory containing files to minify (default: "input")
.PARAMETER OutputDir
    Directory for minified files (default: "output")
.PARAMETER BackupDir
    Directory for backup copies (default: "backup")
.PARAMETER CreateBackup
    Switch to create backup copies of original files
.PARAMETER IncludeSubfolders
    Switch to include subfolders in processing (enabled by default)
.PARAMETER ExcludeFolders
    Array of folder names to exclude from processing
.PARAMETER FileExtensions
    Array of file extensions to process (default: @(".html", ".htm", ".css", ".js"))
.EXAMPLE
    .\Minify-Files.ps1
.EXAMPLE
    .\Minify-Files.ps1 -InputDir "src" -OutputDir "dist" -CreateBackup
.EXAMPLE
    .\Minify-Files.ps1 -ExcludeFolders @("node_modules", "vendor") -IncludeSubfolders
#>

param(
    [string]$InputDir = "C:\water_quality_monitor\web_assets",
    [string]$OutputDir = "C:\water_quality_monitor\web_assets.min",
    [string]$BackupDir = "C:\water_quality_monitor\backup",
    [switch]$CreateBackup,
    [switch]$IncludeSubfolders = $true,
    [string[]]$ExcludeFolders = @("node_modules", ".git", "vendor", "bower_components", "dist", "build", $OutputDir, $BackupDir),
    [string[]]$FileExtensions = @(".html", ".htm", ".css", ".js")
)

# Set console encoding to UTF-8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# Color functions with UTF-8 support
function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

function Write-Success {
    param([string]$Message)
    Write-ColorOutput $Message "Green"
}

function Write-Warning {
    param([string]$Message)
    Write-ColorOutput $Message "Yellow"
}

function Write-Error {
    param([string]$Message)
    Write-ColorOutput $Message "Red"
}

function Write-Info {
    param([string]$Message)
    Write-ColorOutput $Message "Cyan"
}

function Write-Header {
    param([string]$Message)
    Write-ColorOutput $Message "Magenta"
}

# Clear screen and show header
Clear-Host
Write-Header "============================================="
Write-Header "    Advanced HTML/CSS/JS Minifier v3.1"
Write-Header "         with UTF-8 Support"
Write-Header "============================================="
Write-Host ""

# Copy non-attacked files to the destination first.
Get-ChildItem -Path $InputDir -Recurse -File | 
    Where-Object { $_.Extension -NotIn $FileExtensions } | 
    ForEach-Object {
        # Calculate the new path by swapping the source root for the destination root
        $NewPath = $_.FullName.Replace($InputDir, $OutputDir)
        $NewFolder = Split-Path -Path $NewPath -Parent

        # Ensure the destination subfolder exists before copying
        if (-not (Test-Path -Path $NewFolder)) {
            New-Item -Path $NewFolder -ItemType Directory | Out-Null
        }

        Copy-Item -Path $_.FullName -Destination $NewPath -Force
    }

Write-host "Copying non-compressible files to destination folder..."
Write-Host ""

# Validate input directory
if (-not (Test-Path $InputDir)) {
    Write-Error "ERROR: Input directory '$InputDir' does not exist!"
    Write-Host "Please create a folder named '$InputDir' and put your files there."
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

# Create output directory if it doesn't exist
if (-not (Test-Path $OutputDir)) {
    Write-Info "Creating output directory..."
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Create backup directory if requested
if ($CreateBackup) {
    if (-not (Test-Path $BackupDir)) {
        Write-Info "Creating backup directory..."
        New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
    }
}

# Function to detect file encoding
function Get-FileEncoding {
    param([string]$FilePath)
    
    $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    
    # Check for BOM
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        return [System.Text.Encoding]::UTF8
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) {
        return [System.Text.Encoding]::Unicode
    }
    if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF) {
        return [System.Text.Encoding]::BigEndianUnicode
    }
    
    # Default to UTF-8
    return [System.Text.Encoding]::UTF8
}

# Function to read file with proper encoding
function Read-FileWithEncoding {
    param([string]$FilePath)
    
    $encoding = Get-FileEncoding -FilePath $FilePath
    return [System.IO.File]::ReadAllText($FilePath, $encoding)
}

# Function to write file with UTF-8 encoding (no BOM)
function Write-FileUTF8 {
    param(
        [string]$FilePath,
        [string]$Content
    )
    
    # Create UTF-8 encoding without BOM
    $utf8NoBOM = New-Object System.Text.UTF8Encoding $false
    
    # Write file with UTF-8 encoding
    [System.IO.File]::WriteAllText($FilePath, $Content, $utf8NoBOM)
}

# Function to get file size in human-readable format
function Format-FileSize {
    param([long]$Bytes)
    if ($Bytes -ge 1TB) { return "{0:N2} TB" -f ($Bytes / 1TB) }
    elseif ($Bytes -ge 1GB) { return "{0:N2} GB" -f ($Bytes / 1GB) }
    elseif ($Bytes -ge 1MB) { return "{0:N2} MB" -f ($Bytes / 1MB) }
    elseif ($Bytes -ge 1KB) { return "{0:N2} KB" -f ($Bytes / 1KB) }
    else { return "$Bytes bytes" }
}

# Function to calculate savings percentage
function Get-SavingsPercentage {
    param([long]$Original, [long]$Minified)
    if ($Original -eq 0) { return 0 }
    return [math]::Round((($Original - $Minified) / $Original) * 100, 2)
}

# Function to create backup with UTF-8 preservation
function Backup-File {
    param([string]$FilePath, [string]$BackupPath)
    if ($CreateBackup) {
        $backupDir = Split-Path $BackupPath -Parent
        if (-not (Test-Path $backupDir)) {
            New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
        }
        # Copy the file preserving encoding
        Copy-Item -Path $FilePath -Destination $BackupPath -Force
    }
}

# Function to ensure output directory exists
function Ensure-Directory {
    param([string]$Path)
    $dir = Split-Path $Path -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

# Function to get relative path
function Get-RelativePath {
    param([string]$FullPath, [string]$BasePath)
    if ($FullPath -ne $BasePath) {
        $relative = $FullPath.Substring($BasePath.Length + 1)
    } else {
        $relative = ""
    }
    return $relative
}

# Function to minify HTML with UTF-8 preservation
function Minify-HTML {
    param([string]$FilePath, [string]$OutputPath)
    
    try {
        # Read file with proper encoding
        $content = Read-FileWithEncoding -FilePath $FilePath
        
        # Store original encoding for reference
        $originalEncoding = Get-FileEncoding -FilePath $FilePath
        
        # Remove HTML comments (preserve conditional comments)
        $content = $content -replace '(?m)^\s*<!--(?!\[).*?-->\s*$', ''
        $content = $content -replace '<!--(?!\[).*?-->', ''
        
        # Remove script and style comments (but preserve important ones)
        $content = $content -replace '(?m)^\s*//.*?$', ''
        
        # Remove whitespace (but preserve spaces in text content)
        $content = $content -replace '\s+', ' '
        
        # Remove spaces between tags
        $content = $content -replace '>\s+<', '><'
        
        # Remove spaces around attributes
        $content = $content -replace '\s*([{}:;,])\s*', '$1'
        
        # Remove spaces after opening tags and before closing tags
        $content = $content -replace '\s+>', '>'
        $content = $content -replace '<\s+', '<'

        # Additional cleanup
        $content = $content -replace ';[\s\r\n]*\}', '}'

        # Write with UTF-8 encoding
        Write-FileUTF8 -FilePath $OutputPath -Content $content
        
        return $true
    }
    catch {
        Write-Error "  Error minifying HTML: $_"
        return $false
    }
}

# Function to minify CSS with UTF-8 preservation
function Minify-CSS {
    param([string]$FilePath, [string]$OutputPath)
    
    try {
        # Read file with proper encoding
        $content = Read-FileWithEncoding -FilePath $FilePath
        
        # Remove CSS comments (preserve important ones)
        $content = $content -replace '/\*.*?\*/', ''
        
        # Remove whitespace
        $content = $content -replace '\s+', ' '
        
        # Remove spaces around CSS special characters
        $content = $content -replace '\s*([{}:;,])\s*', '$1'
        
        # Remove semicolon before closing brace
        $content = $content -replace ';\}', '}'
        
        # Remove last semicolon in blocks
        $content = $content -replace ';(\})', '$1'
        
        # Remove empty rules
        $content = $content -replace '[^{}]+\{\s*\}', ''
        
        # Remove spaces after !important
        $content = $content -replace '!\s*important', '!important'
        
        # Write with UTF-8 encoding
        Write-FileUTF8 -FilePath $OutputPath -Content $content
        
        return $true
    }
    catch {
        Write-Error "  Error minifying CSS: $_"
        return $false
    }
}

# Function to minify JavaScript with UTF-8 preservation
function Minify-JS {
    param([string]$FilePath, [string]$OutputPath)
    
    try {
        # Read file with proper encoding
        $content = Read-FileWithEncoding -FilePath $FilePath
        
        # Remove single-line comments (preserve important ones)
        $content = $content -replace "(?m)(?<!:)//[^\r\n]*", ""  # "(?m)^\s*//.*?$", ""
        
        # Remove multi-line comments
        $content = $content -replace '(?s)/\*.*?\*/', ''
        $content = $content -replace "/\*.*?\*/", ""
        
        # Remove empty lines
        $content = $content -replace "(`r?`n){3,}", "`r`n`r`n"

        # Remove trailing spaces of each line
        $content = $content -replace "(?m)^\s+", ""

        # Additional cleanup
        $content = ($content -split "`r`n" -join "`n")
        $content = ($content -replace "{ ", "{")
        $content = ($content -replace " {`n", " {")
        $content = ($content -replace " }", "}")
        $content = ($content -replace "`n}`n", "}`n")

         # Remove whitespace
#        $content = $content -replace '\s+', ' '
        
        # Remove spaces around JS special characters
#        $content = $content -replace '\s*([{}()\[\];,:])\s*', '$1'
        
        # Remove semicolon before closing brace
#        $content = $content -replace ';\}', '}'
        
        # Remove unnecessary semicolons
#        $content = $content -replace ';(\}|\)|\])', '$1'
        
        # Remove empty statements
#        $content = $content -replace ';;', ';'
        
        # Remove spaces between operators
#        $content = $content -replace '\s*([+\-*/%=])\s*', '$1'
        
        # Remove spaces after ! and ?
#        $content = $content -replace '!\s+', '!'
#        $content = $content -replace '\?\s+', '?'
        
        # Write with UTF-8 encoding
        Write-FileUTF8 -FilePath $OutputPath -Content $content
        
        return $true
    }
    catch {
        Write-Error "  Error minifying JavaScript: $_"
        return $false
    }
}

# Function to process files in a directory
function Process-Files {
    param(
        [string]$CurrentDir,
        [string]$BaseInputDir,
        [string]$BaseOutputDir
    )
    
    # Get all files in current directory
    $files = Get-ChildItem -Path $CurrentDir -File
    
    # Filter files by extension
    $filteredFiles = $files | Where-Object {
        $extension = $_.Extension.ToLower()
        $FileExtensions -contains $extension
    }
    
    foreach ($file in $filteredFiles) {
        $relativePath = Get-RelativePath -FullPath $file.DirectoryName -BasePath $BaseInputDir
        $outputSubDir = if ($relativePath) { Join-Path $BaseOutputDir $relativePath } else { $BaseOutputDir }
        
        # Determine output file name
        $extension = $file.Extension.ToLower()
        $baseName = $file.BaseName
        
        $outputFileName = $file.Name
        
        
        $outputPath = Join-Path $outputSubDir $outputFileName
        
        # Ensure output directory exists
        Ensure-Directory -Path $outputPath
        
        $originalSize = $file.Length
        
        # Get file encoding
        $encoding = Get-FileEncoding -FilePath $file.FullName
        $encodingName = $encoding.EncodingName
        
        Write-Host "  Processing: $relativePath\$($file.Name) ($(Format-FileSize $originalSize)) [Encoding: $encodingName]"
        
        # Create backup if requested
        if ($CreateBackup) {
            $backupSubDir = if ($relativePath) { Join-Path $BackupDir $relativePath } else { $BackupDir }
            $backupPath = Join-Path $backupSubDir $file.Name
            Backup-File -FilePath $file.FullName -BackupPath $backupPath
        }
        
        # Minify based on file type
        $success = $false
        $minifiedSize = 0
        
        switch ($extension) {
            {$_ -in @(".html", ".htm")} {
                $success = Minify-HTML -FilePath $file.FullName -OutputPath $outputPath
            }
            ".css" {
                $success = Minify-CSS -FilePath $file.FullName -OutputPath $outputPath
            }
            ".js" {
                $success = Minify-JS -FilePath $file.FullName -OutputPath $outputPath
            }
        }
        
        if ($success) {
            $minifiedSize = (Get-Item $outputPath).Length
            $savings = Get-SavingsPercentage -Original $originalSize -Minified $minifiedSize
            
            $script:totalOriginal += $originalSize
            $script:totalMinified += $minifiedSize
            $script:processedFiles++
            
            if ($savings -gt 0) {
                Write-Success "    ✓ Minified: $(Format-FileSize $minifiedSize) (Saved $savings%)"
            } else {
                Write-Success "    ✓ Minified: $(Format-FileSize $minifiedSize) (No significant savings)"
            }
            
            $script:results += [PSCustomObject]@{
                File = "$relativePath\$($file.Name)"
                Type = $extension.Replace(".", "").ToUpper()
                Original = $originalSize
                Minified = $minifiedSize
                Savings = $savings
                Status = "Success"
                Encoding = $encodingName
            }
        }
        else {
            $script:failedFiles++
            $script:results += [PSCustomObject]@{
                File = "$relativePath\$($file.Name)"
                Type = $extension.Replace(".", "").ToUpper()
                Original = $originalSize
                Minified = 0
                Savings = 0
                Status = "Failed"
                Encoding = $encodingName
            }
        }
        
        $script:totalFiles++
    }
    
    # Process subdirectories if enabled
    if ($IncludeSubfolders) {
        $subDirs = Get-ChildItem -Path $CurrentDir -Directory
        
        foreach ($subDir in $subDirs) {
            # Check if folder should be excluded
            if ($ExcludeFolders -contains $subDir.Name) {
                Write-Warning "  Excluding folder: $($subDir.Name)"
                continue
            }
            
            # Recursively process subdirectory
            Process-Files -CurrentDir $subDir.FullName -BaseInputDir $BaseInputDir -BaseOutputDir $BaseOutputDir
        }
    }
}

# Initialize statistics
$totalOriginal = 0
$totalMinified = 0
$totalFiles = 0
$processedFiles = 0
$failedFiles = 0
$results = @()

# Start processing
Write-Info "Starting minification process..."
Write-Host ""
Write-Info "Configuration:"
Write-Host "  Input Directory:  $InputDir"
Write-Host "  Output Directory: $OutputDir"
if ($CreateBackup) {
    Write-Host "  Backup Directory: $BackupDir"
}
Write-Host "  Include Subfolders: $IncludeSubfolders"
Write-Host "  Excluded Folders: $($ExcludeFolders -join ', ')"
Write-Host "  File Extensions: $($FileExtensions -join ', ')"
Write-Host "  UTF-8 Support: Enabled (preserves emojis and special characters)"
Write-Host ""

# Process all files
Process-Files -CurrentDir $InputDir -BaseInputDir $InputDir -BaseOutputDir $OutputDir

# Display summary
Write-Host ""
Write-Header "============================================="
Write-Success "Minification Complete!"
Write-Header "============================================="

Write-Host ""
Write-Host "Input Directory:  $InputDir"
Write-Host "Output Directory: $OutputDir"
if ($CreateBackup) {
    Write-Host "Backup Directory: $BackupDir"
}
Write-Host ""

# Show statistics by file type
Write-Info "Statistics by File Type:"
$typeStats = $results | Where-Object { $_.Status -eq "Success" } | Group-Object Type | ForEach-Object {
    $type = $_.Name
    $count = $_.Count
    $totalOrig = ($_.Group | Measure-Object Original -Sum).Sum
    $totalMin = ($_.Group | Measure-Object Minified -Sum).Sum
    $avgSavings = if ($count -gt 0) { [math]::Round(($_.Group | Measure-Object Savings -Average).Average, 2) } else { 0 }
    
    [PSCustomObject]@{
        Type = $type
        Count = $count
        Original = Format-FileSize $totalOrig
        Minified = Format-FileSize $totalMin
        "Avg Savings" = "$avgSavings%"
    }
}
$typeStats | Format-Table -AutoSize

Write-Host "Files Processed: $processedFiles"
Write-Host "Files Failed:    $failedFiles"
Write-Host "Total Files:     $totalFiles"
Write-Host ""

if ($totalOriginal -gt 0) {
    $totalSavings = Get-SavingsPercentage -Original $totalOriginal -Minified $totalMinified
    Write-Host "Total Original Size: $(Format-FileSize $totalOriginal)"
    Write-Host "Total Minified Size: $(Format-FileSize $totalMinified)"
    Write-Host "Total Savings:       $totalSavings%"
    Write-Host "Space Saved:         $(Format-FileSize ($totalOriginal - $totalMinified))"
}

# Show encoding summary
Write-Host ""
Write-Info "Encoding Summary:"
$encodingStats = $results | Group-Object Encoding | ForEach-Object {
    [PSCustomObject]@{
        Encoding = $_.Name
        Count = $_.Count
    }
}
$encodingStats | Format-Table -AutoSize

# Show folder structure summary
Write-Host ""
Write-Info "Folder Structure Summary:"
$allDirs = Get-ChildItem -Path $OutputDir -Directory -Recurse | ForEach-Object {
    $relPath = Get-RelativePath -FullPath $_.FullName -BasePath $OutputDir
    $fileCount = (Get-ChildItem -Path $_.FullName -File | Where-Object { $FileExtensions -contains $_.Extension.ToLower() }).Count
    if ($fileCount -gt 0) {
        Write-Host "  $relPath - $fileCount files"
    }
}

# Show failed files if any
if ($failedFiles -gt 0) {
    Write-Host ""
    Write-Error "Failed Files:"
    $failedFilesList = $results | Where-Object { $_.Status -eq "Failed" }
    $failedFilesList | ForEach-Object {
        Write-Host "  ✗ $($_.File) (Encoding: $($_.Encoding))"
    }
}

Write-Host ""
$results | Where-Object { $_.Status -eq "Success" } | Format-Table @{
    Label = "File"
    Expression = { $_.File }
}, @{
    Label = "Type"
    Expression = { $_.Type }
}, @{
    Label = "Original"
    Expression = { Format-FileSize $_.Original }
}, @{
    Label = "Minified"
    Expression = { Format-FileSize $_.Minified }
}, @{
    Label = "Savings"
    Expression = { if ($_.Savings -gt 0) { "$($_.Savings)%" } else { "0%" } }
} -AutoSize
