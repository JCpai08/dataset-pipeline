param(
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$SceneName = "terrace",
    [string]$SevenZipPath = "",
    [switch]$SkipExtract
)

$ErrorActionPreference = "Stop"

function Resolve-SevenZip {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "SevenZipPath does not exist: $ExplicitPath"
    }

    foreach ($name in @("7z", "7za", "7zr")) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    foreach ($path in @(
        "D:\7-Zip\7z.exe",
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe"
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return $path
        }
    }

    throw "7-Zip was not found. Install 7-Zip, add 7z.exe to PATH, or pass -SevenZipPath 'C:\Path\to\7z.exe'."
}

function Move-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        $target = Join-Path $Destination $item.Name
        if (Test-Path -LiteralPath $target) {
            throw "Cannot move '$($item.FullName)' because target already exists: $target"
        }
        Move-Item -LiteralPath $item.FullName -Destination $target
    }
}

if (-not (Test-Path -LiteralPath $DatasetRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $DatasetRoot | Out-Null
}

$datasetRootPath = (Resolve-Path -LiteralPath $DatasetRoot).Path
$sceneDir = Join-Path $datasetRootPath $SceneName
New-Item -ItemType Directory -Force -Path $sceneDir | Out-Null
$sceneDir = (Resolve-Path -LiteralPath $sceneDir).Path

if (-not $SkipExtract) {
    $sevenZip = Resolve-SevenZip -ExplicitPath $SevenZipPath
    $archivePattern = "$SceneName`_*.7z"
    $archives = @(Get-ChildItem -LiteralPath $datasetRootPath -File -Filter $archivePattern -ErrorAction SilentlyContinue)

    if ($archives.Count -eq 0) {
        throw "No archives matched '$archivePattern' under: $datasetRootPath"
    }

    Write-Host "7-Zip:          $sevenZip"
    Write-Host "Dataset root:   $datasetRootPath"
    Write-Host "Scene dir:      $sceneDir"
    Write-Host "Archive count:  $($archives.Count)"
    Write-Host ""

    foreach ($archive in $archives) {
        Write-Host "Extracting:     $($archive.FullName)"
        & $sevenZip x $archive.FullName "-o$sceneDir" -y
        if ($LASTEXITCODE -ne 0) {
            throw "7-Zip extraction failed for $($archive.FullName) with exit code $LASTEXITCODE."
        }
    }
}

# Some ETH3D archives contain a top-level scene folder. If extracting into
# E:\sh\dataset\terrace created E:\sh\dataset\terrace\terrace, flatten it.
$nestedSceneDir = Join-Path $sceneDir $SceneName
if ((Test-Path -LiteralPath $nestedSceneDir -PathType Container) -and
    -not (Test-Path -LiteralPath (Join-Path $sceneDir "scan_clean") -PathType Container)) {
    Write-Host "Flattening nested scene directory: $nestedSceneDir"
    Move-DirectoryContents -Source $nestedSceneDir -Destination $sceneDir

    $remaining = @(Get-ChildItem -LiteralPath $nestedSceneDir -Force)
    if ($remaining.Count -eq 0) {
        Remove-Item -LiteralPath $nestedSceneDir
    }
}

# Pipeline expects dslr_images next to masks_for_images, not below images.
$nestedDslr = Join-Path $sceneDir "images\dslr_images"
$targetDslr = Join-Path $sceneDir "dslr_images"
if (Test-Path -LiteralPath $nestedDslr -PathType Container) {
    if (Test-Path -LiteralPath $targetDslr) {
        Write-Host "dslr_images already exists at target; leaving nested directory unchanged: $nestedDslr"
    }
    else {
        Write-Host "Moving dslr_images to dataset root."
        Move-Item -LiteralPath $nestedDslr -Destination $targetDslr
    }
}

$imagesDir = Join-Path $sceneDir "images"
if (Test-Path -LiteralPath $imagesDir -PathType Container) {
    $remaining = @(Get-ChildItem -LiteralPath $imagesDir -Force)
    if ($remaining.Count -eq 0) {
        Remove-Item -LiteralPath $imagesDir
    }
}

$scanCleanDir = Join-Path $sceneDir "scan_clean"
if (-not (Test-Path -LiteralPath $scanCleanDir -PathType Container)) {
    throw "Prepared scene is missing scan_clean: $scanCleanDir"
}

Write-Host ""
Write-Host "Dataset prepared: $sceneDir"
Write-Host "Expected Docker dataset path: /data/$SceneName"
