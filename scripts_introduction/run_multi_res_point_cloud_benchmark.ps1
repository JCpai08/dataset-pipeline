param(
    [string]$MultiResPointCloudBenchmarkExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "terrace",
    [string]$ScanAlignmentSubpath = "scan_clean\scan_alignment.mlp",
    [string]$OcclusionMeshSubpath = "surface_reconstruction\surface.ply",
    [string]$OcclusionSplatsSubpath = "surface_reconstruction\splats.ply",
    [string]$ImageBaseSubpath = "",
    [string]$StateSubpath = "sparse_reconstruction_scaled\colmap_model",
    [string]$MultiResPointCloudSubpath = "multi_res_point_cloud_benchmark",
    [string]$CameraIdsToIgnore = "",
    [switch]$AutoIgnoreCubeMapCamera = $true,
    [switch]$SaveMultiResPointCloud = $false,
    [switch]$CleanOutput = $false,
    [string[]]$ExtraArgs = @()
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

function Resolve-MultiResPointCloudBenchmark {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "MultiResPointCloudBenchmarkExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\MultiResPointCloudBenchmark.exe"),
        (Join-Path $repoRoot "build_windows\Release\MultiResPointCloudBenchmark.exe"),
        (Join-Path $repoRoot "build_windows\MultiResPointCloudBenchmark.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\MultiResPointCloudBenchmark.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\MultiResPointCloudBenchmark.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "MultiResPointCloudBenchmark" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "MultiResPointCloudBenchmark.exe was not found. Build it or pass -MultiResPointCloudBenchmarkExe explicitly."
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

$benchmarkExe = Resolve-MultiResPointCloudBenchmark -ExplicitPath $MultiResPointCloudBenchmarkExe
$scanAlignment = Join-Path $datasetDir $ScanAlignmentSubpath
$occlusionMesh = Join-Path $datasetDir $OcclusionMeshSubpath
$occlusionSplats = Join-Path $datasetDir $OcclusionSplatsSubpath
$imageBasePath = if ($ImageBaseSubpath) { Join-Path $datasetDir $ImageBaseSubpath } else { $datasetDir }
$statePath = Join-Path $datasetDir $StateSubpath
$multiResPointCloudDirectory = Join-Path $datasetDir $MultiResPointCloudSubpath

if (-not (Test-Path -LiteralPath $scanAlignment -PathType Leaf)) {
    throw "Scan alignment MeshLab project does not exist: $scanAlignment"
}
if (-not (Test-Path -LiteralPath $statePath -PathType Container)) {
    throw "COLMAP state directory does not exist: $statePath"
}
if (-not (Test-Path -LiteralPath $imageBasePath -PathType Container)) {
    throw "Image base path does not exist: $imageBasePath"
}

$camerasPath = Join-Path $statePath "cameras.txt"
if (-not $CameraIdsToIgnore -and $AutoIgnoreCubeMapCamera -and
    (Test-Path -LiteralPath $camerasPath -PathType Leaf)) {
    $cubeMapCameraId = Find-CubeMapCameraId -CamerasPath $camerasPath
    if ($null -ne $cubeMapCameraId) {
        $CameraIdsToIgnore = [string]$cubeMapCameraId
        Write-Host "Auto-detected cube-map camera id to ignore: $CameraIdsToIgnore"
    }
}

if ($CleanOutput -and (Test-Path -LiteralPath $multiResPointCloudDirectory)) {
    Remove-Item -LiteralPath $multiResPointCloudDirectory -Recurse -Force
}
if ($SaveMultiResPointCloud) {
    New-Item -ItemType Directory -Force -Path $multiResPointCloudDirectory | Out-Null
}

$arguments = @(
    "--scan_alignment_path", $scanAlignment,
    "--image_base_path", $imageBasePath,
    "--state_path", $statePath
)

if (Test-Path -LiteralPath $occlusionMesh -PathType Leaf) {
    $arguments += @("--occlusion_mesh_path", $occlusionMesh)
}
else {
    Write-Warning "Occlusion mesh does not exist and will not be used: $occlusionMesh"
}

if (Test-Path -LiteralPath $occlusionSplats -PathType Leaf) {
    $arguments += @("--occlusion_splats_path", $occlusionSplats)
}
else {
    Write-Warning "Occlusion splats do not exist and will not be used: $occlusionSplats"
}

if ($CameraIdsToIgnore) {
    $arguments += @("--camera_ids_to_ignore", $CameraIdsToIgnore)
}
if ($SaveMultiResPointCloud) {
    $arguments += @(
        "--multi_res_point_cloud_directory_path", $multiResPointCloudDirectory,
        "--save_multi_res_point_cloud", "1"
    )
}
if ($ExtraArgs.Count -gt 0) {
    $arguments += $ExtraArgs
}

Write-Host "Dataset dir:                 $datasetDir"
Write-Host "Scan alignment:              $scanAlignment"
Write-Host "Occlusion mesh:              $occlusionMesh"
Write-Host "Occlusion splats:            $occlusionSplats"
Write-Host "Image base path:             $imageBasePath"
Write-Host "Input COLMAP state:          $statePath"
Write-Host "Benchmark exe:               $benchmarkExe"
Write-Host "Camera IDs to ignore:        $CameraIdsToIgnore"
Write-Host "Save multi-res point cloud:  $SaveMultiResPointCloud"
if ($SaveMultiResPointCloud) {
    Write-Host "Multi-res output directory:  $multiResPointCloudDirectory"
}
if ($ExtraArgs.Count -gt 0) {
    Write-Host "Extra args:                  $($ExtraArgs -join ' ')"
}
Write-Host ""

$elapsed = Measure-Command {
    & $benchmarkExe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "MultiResPointCloudBenchmark failed with exit code $LASTEXITCODE."
    }
}

Write-Host ""
Write-Host "PowerShell measured wall time: $($elapsed.TotalSeconds) s"
