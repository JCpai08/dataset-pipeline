param(
    [string]$ColmapExe = "E:\zj\project\colmap\colmap-x64-windows-cuda\bin\colmap.exe",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [string]$ImageDir = "dslr_images",
    [int]$CubeMapSize = 2048,
    [string]$DslrCameraModel = "OPENCV",
    [string]$DslrCameraParams = "",
    [switch]$DslrSingleCamera,
    [ValidateSet("exhaustive", "sequential", "spatial", "vocab_tree")]
    [string]$Matcher = "exhaustive",
    [string]$VocabTreePath = "",
    [switch]$OverwriteDatabase,
    [int]$ModelIndex = 0
)

$ErrorActionPreference = "Stop"

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

function Invoke-Colmap {
    param([string[]]$Arguments)

    Write-Host ""
    Write-Host "COLMAP $($Arguments[0])"
    & $ColmapExe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "COLMAP command failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

if (-not (Test-Path -LiteralPath $ColmapExe -PathType Leaf)) {
    throw "COLMAP executable does not exist: $ColmapExe"
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

$scriptDir = Split-Path -Parent $PSCommandPath
$prepareScript = Join-Path $scriptDir "prepare_colmap_input.ps1"
& $prepareScript -DatasetRoot $resolvedDatasetRoot -DatasetSubdir $DatasetSubdir -ImageDir $ImageDir

$colmapDir = Join-Path $datasetDir "colmap"
$sparseDir = Join-Path $colmapDir "sparse"
$sparseTextDir = Join-Path $colmapDir "sparse_txt"
$imageListsDir = Join-Path $colmapDir "image_lists"
$databasePath = Join-Path $colmapDir "database.db"
$dslrListPath = Join-Path $imageListsDir "dslr_images.txt"
$cubeMapListPath = Join-Path $imageListsDir "cubemap_images.txt"

New-Item -ItemType Directory -Force -Path $colmapDir, $sparseDir, $sparseTextDir | Out-Null

if ((Test-Path -LiteralPath $databasePath -PathType Leaf) -and $OverwriteDatabase) {
    Remove-Item -LiteralPath $databasePath -Force
}
elseif (Test-Path -LiteralPath $databasePath -PathType Leaf) {
    throw "Database already exists: $databasePath. Pass -OverwriteDatabase to recreate it."
}

$cubeMapFocal = [int]($CubeMapSize / 2)
$cubeMapParams = "$cubeMapFocal,$cubeMapFocal,$cubeMapFocal,$cubeMapFocal"

Write-Host ""
Write-Host "Dataset dir:        $datasetDir"
Write-Host "COLMAP exe:         $ColmapExe"
Write-Host "Database:           $databasePath"
Write-Host "Sparse output:      $sparseDir"
Write-Host "Text model output:  $sparseTextDir"
Write-Host "Cube map intrinsics PINHOLE $CubeMapSize $CubeMapSize $cubeMapParams"

$dslrArgs = @(
    "feature_extractor",
    "--database_path", $databasePath,
    "--image_path", $datasetDir,
    "--image_list_path", $dslrListPath,
    "--ImageReader.camera_model", $DslrCameraModel
)
if ($DslrCameraParams) {
    $dslrArgs += @("--ImageReader.camera_params", $DslrCameraParams)
}
if ($DslrSingleCamera) {
    $dslrArgs += @("--ImageReader.single_camera", "1")
}
Invoke-Colmap -Arguments $dslrArgs

$cubeMapArgs = @(
    "feature_extractor",
    "--database_path", $databasePath,
    "--image_path", $datasetDir,
    "--image_list_path", $cubeMapListPath,
    "--ImageReader.camera_model", "PINHOLE",
    "--ImageReader.camera_params", $cubeMapParams,
    "--ImageReader.single_camera", "1",
    "--SiftExtraction.max_image_size", "$CubeMapSize"
)
Invoke-Colmap -Arguments $cubeMapArgs

switch ($Matcher) {
    "exhaustive" {
        Invoke-Colmap -Arguments @("exhaustive_matcher", "--database_path", $databasePath)
    }
    "sequential" {
        Invoke-Colmap -Arguments @("sequential_matcher", "--database_path", $databasePath)
    }
    "spatial" {
        Invoke-Colmap -Arguments @("spatial_matcher", "--database_path", $databasePath)
    }
    "vocab_tree" {
        if (-not $VocabTreePath) {
            throw "Matcher vocab_tree requires -VocabTreePath."
        }
        Invoke-Colmap -Arguments @("vocab_tree_matcher", "--database_path", $databasePath, "--VocabTreeMatching.vocab_tree_path", $VocabTreePath)
    }
}

Invoke-Colmap -Arguments @(
    "mapper",
    "--database_path", $databasePath,
    "--image_path", $datasetDir,
    "--output_path", $sparseDir,
    "--Mapper.multiple_models", "0"
)

$selectedSparseModel = Join-Path $sparseDir "$ModelIndex"
if (-not (Test-Path -LiteralPath $selectedSparseModel -PathType Container)) {
    throw "Selected sparse model does not exist: $selectedSparseModel"
}

Get-ChildItem -LiteralPath $sparseTextDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Invoke-Colmap -Arguments @(
    "model_converter",
    "--input_path", $selectedSparseModel,
    "--output_path", $sparseTextDir,
    "--output_type", "TXT"
)

Write-Host ""
Write-Host "COLMAP sparse reconstruction finished."
Write-Host "Binary model: $selectedSparseModel"
Write-Host "Text model:   $sparseTextDir"
Write-Host "Next step:    run_scale_estimator.ps1"
