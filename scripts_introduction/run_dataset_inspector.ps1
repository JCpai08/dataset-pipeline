param(
    [string]$DatasetInspectorExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "delivery",
    [string]$ScanAlignmentSubpath = "scan_clean\scan_alignment.mlp",
    [string]$OcclusionMeshSubpath = "surface_reconstruction\surface.ply",
    [string]$OcclusionSplatsSubpath = "surface_reconstruction\splats.ply",
    [string]$MultiResPointCloudSubpath = "multi_res_point_cloud_cache",
    [string]$ImageBaseSubpath = ".",
    [string]$StateSubpath = "sparse_reconstruction_scaled\colmap_model",
    [string]$CameraIdsToIgnore = "",
    [double]$OcclusionDepthSaturation = 20,
    [switch]$NoOptimizationTools = $false,
    [switch]$DisableAutoIgnoreCubeMapCamera = $false
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

function Resolve-DatasetInspector {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "DatasetInspectorExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\DatasetInspector.exe"),
        (Join-Path $repoRoot "build_windows\Release\DatasetInspector.exe"),
        (Join-Path $repoRoot "build_windows\DatasetInspector.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\DatasetInspector.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\DatasetInspector.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "DatasetInspector" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "DatasetInspector.exe was not found. Pass -DatasetInspectorExe explicitly."
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
        return $null
    }

    $parts = $lastCameraLine -split '\s+'
    if ($parts.Count -lt 4) {
        return $null
    }
    if ($parts[1] -ne "PINHOLE") {
        return $null
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

$datasetInspector = Resolve-DatasetInspector -ExplicitPath $DatasetInspectorExe
$scanAlignment = Join-Path $datasetDir $ScanAlignmentSubpath
$occlusionMesh = Join-Path $datasetDir $OcclusionMeshSubpath
$occlusionSplats = Join-Path $datasetDir $OcclusionSplatsSubpath
$multiResPointCloud = Join-Path $datasetDir $MultiResPointCloudSubpath
$imageBase = Join-Path $datasetDir $ImageBaseSubpath
$state = Join-Path $datasetDir $StateSubpath

if (-not (Test-Path -LiteralPath $scanAlignment -PathType Leaf)) {
    throw "Scan alignment does not exist: $scanAlignment. Run run_icp_scan_aligner.ps1 first or pass -ScanAlignmentSubpath."
}
if (-not (Test-Path -LiteralPath $imageBase -PathType Container)) {
    throw "Image base path does not exist: $imageBase"
}
if (-not (Test-Path -LiteralPath $state -PathType Container)) {
    throw "State path does not exist: $state. Run run_scale_estimator.ps1 first or pass -StateSubpath."
}
$camerasPath = Join-Path $state "cameras.txt"
if (-not (Test-Path -LiteralPath $camerasPath -PathType Leaf)) {
    throw "COLMAP cameras.txt does not exist: $camerasPath"
}
if (-not (Test-Path -LiteralPath $occlusionMesh -PathType Leaf) -and
    -not (Test-Path -LiteralPath $occlusionSplats -PathType Leaf)) {
    throw "Neither occlusion mesh nor splats exist. Expected $occlusionMesh or $occlusionSplats."
}

if (-not $CameraIdsToIgnore -and -not $DisableAutoIgnoreCubeMapCamera) {
    $cubeMapCameraId = Find-CubeMapCameraId -CamerasPath $camerasPath
    if ($null -ne $cubeMapCameraId) {
        $CameraIdsToIgnore = [string]$cubeMapCameraId
        Write-Host "Auto-detected cube-map camera id to ignore: $CameraIdsToIgnore"
    }
}

$arguments = @(
    "--scan_alignment_path", $scanAlignment,
    "--image_base_path", $imageBase,
    "--state_path", $state,
    "--occlusion_depth_saturation", $OcclusionDepthSaturation
)

if (Test-Path -LiteralPath $occlusionMesh -PathType Leaf) {
    $arguments += @("--occlusion_mesh_path", $occlusionMesh)
}
else {
    Write-Warning "Occlusion mesh does not exist and will be skipped: $occlusionMesh"
}

if (Test-Path -LiteralPath $occlusionSplats -PathType Leaf) {
    $arguments += @("--occlusion_splats_path", $occlusionSplats)
}
else {
    Write-Warning "Occlusion splats do not exist and will be skipped: $occlusionSplats"
}

if (-not $NoOptimizationTools) {
    if (-not (Test-Path -LiteralPath $multiResPointCloud -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $multiResPointCloud | Out-Null
    }
    $arguments += @("--multi_res_point_cloud_directory_path", $multiResPointCloud)
}

if ($CameraIdsToIgnore) {
    $arguments += @("--camera_ids_to_ignore", $CameraIdsToIgnore)
}

Write-Host "Dataset dir:             $datasetDir"
Write-Host "Scan alignment:          $scanAlignment"
Write-Host "Occlusion mesh:          $occlusionMesh"
Write-Host "Occlusion splats:        $occlusionSplats"
Write-Host "Multi-res point cloud:   $(if ($NoOptimizationTools) { '<disabled>' } else { $multiResPointCloud })"
Write-Host "Image base path:         $imageBase"
Write-Host "State path:              $state"
Write-Host "DatasetInspector:        $datasetInspector"
Write-Host "Camera IDs to ignore:    $CameraIdsToIgnore"
Write-Host ""

& $datasetInspector @arguments

if ($LASTEXITCODE -ne 0) {
    throw "DatasetInspector failed with exit code $LASTEXITCODE."
}
