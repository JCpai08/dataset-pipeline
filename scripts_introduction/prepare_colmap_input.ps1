param(
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [string]$ImageDir = "dslr_images",
    [string]$OutputSubdir = "colmap\image_lists"
)

$ErrorActionPreference = "Stop"

function Get-RelativePath {
    param(
        [string]$Root,
        [string]$Path
    )

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar

    if (-not $pathFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside dataset directory: $pathFull"
    }

    return $pathFull.Substring($prefix.Length).Replace('\', '/')
}

function Find-DatasetDirectory {
    param([string]$SearchRoot)

    if (Test-Path -LiteralPath (Join-Path $SearchRoot "scan_clean") -PathType Container) {
        return (Resolve-Path -LiteralPath $SearchRoot).Path
    }

    $scanDirs = Get-ChildItem -LiteralPath $SearchRoot -Recurse -Directory -Filter "scan_clean" -ErrorAction SilentlyContinue
    if (-not $scanDirs) {
        return $null
    }

    $chosen = $scanDirs |
        Sort-Object @{ Expression = { $_.FullName.Length } }, FullName |
        Select-Object -First 1
    return (Resolve-Path -LiteralPath $chosen.Parent.FullName).Path
}

if (-not (Test-Path -LiteralPath $DatasetRoot -PathType Container)) {
    throw "DatasetRoot does not exist or is not a directory: $DatasetRoot"
}

$resolvedDatasetRoot = (Resolve-Path -LiteralPath $DatasetRoot).Path
if ($DatasetSubdir) {
    $datasetDir = Join-Path $resolvedDatasetRoot $DatasetSubdir
    if (-not (Test-Path -LiteralPath (Join-Path $datasetDir "scan_clean") -PathType Container)) {
        throw "Expected scan directory was not found: $(Join-Path $datasetDir 'scan_clean')"
    }
    $datasetDir = (Resolve-Path -LiteralPath $datasetDir).Path
}
else {
    $datasetDir = Find-DatasetDirectory -SearchRoot $resolvedDatasetRoot
    if (-not $datasetDir) {
        throw "Could not find a dataset directory containing scan_clean under: $resolvedDatasetRoot"
    }
}

$imageDirPath = Join-Path $datasetDir $ImageDir
if (-not (Test-Path -LiteralPath $imageDirPath -PathType Container)) {
    throw "ImageDir does not exist under dataset directory: $imageDirPath"
}

$cubeMapsDir = Join-Path $datasetDir "cube_maps"
if (-not (Test-Path -LiteralPath $cubeMapsDir -PathType Container)) {
    throw "cube_maps does not exist. Run run_cube_map_renderer_docker.ps1 first: $cubeMapsDir"
}

$outputDir = Join-Path $datasetDir $OutputSubdir
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$outputDir = (Resolve-Path -LiteralPath $outputDir).Path

$imageExtensions = @(".jpg", ".jpeg", ".png", ".tif", ".tiff", ".bmp")
$datasetDirFull = [System.IO.Path]::GetFullPath($datasetDir)

$dslrImages = Get-ChildItem -LiteralPath $imageDirPath -Recurse -File |
    Where-Object { $imageExtensions -contains $_.Extension.ToLowerInvariant() } |
    Sort-Object FullName |
    ForEach-Object { Get-RelativePath -Root $datasetDirFull -Path $_.FullName }

if (-not $dslrImages -or $dslrImages.Count -eq 0) {
    throw "No input camera images were found under: $imageDirPath"
}

$cubeMapImages = Get-ChildItem -LiteralPath $cubeMapsDir -File -Filter "*.png" |
    Where-Object { $_.Name -match '^scan\d+\.ply\.(front|left|back|right|up|down)\.png$' } |
    Sort-Object FullName |
    ForEach-Object { Get-RelativePath -Root $datasetDirFull -Path $_.FullName }

if (-not $cubeMapImages -or $cubeMapImages.Count -eq 0) {
    throw "No cube map face PNG images were found under: $cubeMapsDir"
}

$dslrListPath = Join-Path $outputDir "dslr_images.txt"
$cubeMapListPath = Join-Path $outputDir "cubemap_images.txt"
$allListPath = Join-Path $outputDir "all_images.txt"

[System.IO.File]::WriteAllLines($dslrListPath, $dslrImages, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllLines($cubeMapListPath, $cubeMapImages, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllLines($allListPath, @($dslrImages) + @($cubeMapImages), [System.Text.UTF8Encoding]::new($false))

Write-Host "Dataset dir:        $datasetDir"
Write-Host "Input image dir:    $imageDirPath"
Write-Host "Cube map dir:       $cubeMapsDir"
Write-Host "Output list dir:    $outputDir"
Write-Host "Camera image count: $($dslrImages.Count)"
Write-Host "Cube map count:     $($cubeMapImages.Count)"
Write-Host ""
Write-Host "Wrote: $dslrListPath"
Write-Host "Wrote: $cubeMapListPath"
Write-Host "Wrote: $allListPath"
