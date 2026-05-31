param(
    [string]$ScaleEstimatorExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [int]$CubeMapSize = 2048,
    [int]$CubeMapFaceCameraId = -1,
    [string]$SfmModelSubdir = "colmap\sparse_txt",
    [string]$OutputSubdir = "sparse_reconstruction_scaled"
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

function Resolve-ScaleEstimator {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "ScaleEstimatorExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\SfMScaleEstimator.exe"),
        (Join-Path $repoRoot "build_windows\Release\SfMScaleEstimator.exe"),
        (Join-Path $repoRoot "build_windows\SfMScaleEstimator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\SfMScaleEstimator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\SfMScaleEstimator.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "SfMScaleEstimator" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "SfMScaleEstimator.exe was not found. Pass -ScaleEstimatorExe explicitly."
}

function Find-CubeMapCameraId {
    param([string]$CamerasPath)

    $lastCameraLine = $null
    foreach ($line in Get-Content -LiteralPath $CamerasPath) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }

        $lastCameraLine = $trimmed
    }

    if (-not $lastCameraLine) {
        throw "No camera entries were found in cameras.txt."
    }

    $parts = $lastCameraLine -split '\s+'
    if ($parts.Count -lt 4) {
        throw "The last camera entry in cameras.txt is malformed: $lastCameraLine"
    }
    if ($parts[1] -ne "PINHOLE") {
        throw "The last camera entry in cameras.txt is not PINHOLE: $lastCameraLine. Pass -CubeMapFaceCameraId explicitly."
    }

    return [int]$parts[0]
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

$scaleEstimator = Resolve-ScaleEstimator -ExplicitPath $ScaleEstimatorExe
$sfmModelPath = Join-Path $datasetDir $SfmModelSubdir
$camerasPath = Join-Path $sfmModelPath "cameras.txt"
$imagesPath = Join-Path $sfmModelPath "images.txt"
$pointsPath = Join-Path $sfmModelPath "points3D.txt"
$scansPath = Join-Path $datasetDir "scan_clean"
$outputPath = Join-Path $datasetDir $OutputSubdir
$scaledModelPath = Join-Path $outputPath "colmap_model"

foreach ($requiredPath in @($camerasPath, $imagesPath, $pointsPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required COLMAP text model file does not exist: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $scansPath -PathType Container)) {
    throw "scan_clean does not exist: $scansPath"
}

if ($CubeMapFaceCameraId -lt 0) {
    $CubeMapFaceCameraId = Find-CubeMapCameraId -CamerasPath $camerasPath
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
if (Test-Path -LiteralPath $scaledModelPath) {
    $resolvedScaledModelPath = [System.IO.Path]::GetFullPath($scaledModelPath)
    $resolvedOutputPath = [System.IO.Path]::GetFullPath($outputPath).TrimEnd('\', '/')
    $expectedPrefix = $resolvedOutputPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedScaledModelPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove scaled model path outside output directory: $resolvedScaledModelPath"
    }
    Write-Host "Removing previous scaled COLMAP model: $resolvedScaledModelPath"
    Remove-Item -LiteralPath $resolvedScaledModelPath -Recurse -Force
}

Write-Host "Dataset dir:             $datasetDir"
Write-Host "SfM model:               $sfmModelPath"
Write-Host "SfM image base path:     $datasetDir"
Write-Host "Scans path:              $scansPath"
Write-Host "Output path:             $outputPath"
Write-Host "SfMScaleEstimator:       $scaleEstimator"
Write-Host "Cube map face camera id: $CubeMapFaceCameraId"
Write-Host ""

& $scaleEstimator `
    -s $sfmModelPath `
    -si $datasetDir `
    -i $scansPath `
    -o $outputPath `
    --cube_map_face_camera_id $CubeMapFaceCameraId

if ($LASTEXITCODE -ne 0) {
    throw "SfMScaleEstimator failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Scale estimation finished."
Write-Host "Scaled COLMAP model: $scaledModelPath"
Write-Host "MeshLab project:     $outputPath\meshlab_project.mlp"
