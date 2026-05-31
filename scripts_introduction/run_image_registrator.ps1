param(
    [string]$ImageRegistratorExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "delivery",
    [string]$ScanAlignmentSubpath = "scan_clean\scan_alignment.mlp",
    [string]$OcclusionMeshSubpath = "surface_reconstruction\surface.ply",
    [string]$OcclusionSplatsSubpath = "surface_reconstruction\splats.ply",
    [string]$MultiResPointCloudSubpath = "multi_res_point_cloud_cache",
    [string]$ImageBaseSubpath = "",
    [string]$StateSubpath = "sparse_reconstruction_scaled\colmap_model",
    [string]$OutputFolderSubpath = "image_alignment_refined",
    [string]$ObservationsCacheSubpath = "observations_cache",
    [string]$CameraIdsToIgnore = "",
    [switch]$AutoIgnoreCubeMapCamera = $true,
    [int]$MaxIterations = 400,
    [double]$InitialScalingFactor = 0,
    [double]$TargetScalingFactor = 2,
    [switch]$CacheObservations,
    [switch]$WriteDebugPointClouds,
    [switch]$Overwrite = $true,
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

function Resolve-ImageRegistrator {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "ImageRegistratorExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\ImageRegistrator.exe"),
        (Join-Path $repoRoot "build_windows\Release\ImageRegistrator.exe"),
        (Join-Path $repoRoot "build_windows\ImageRegistrator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\ImageRegistrator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\ImageRegistrator.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "ImageRegistrator" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "ImageRegistrator.exe was not found. Pass -ImageRegistratorExe explicitly."
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
        throw "The last camera entry in cameras.txt is not PINHOLE: $lastCameraLine. Pass -CameraIdsToIgnore explicitly or disable -AutoIgnoreCubeMapCamera."
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

$imageRegistrator = Resolve-ImageRegistrator -ExplicitPath $ImageRegistratorExe
$scanAlignment = Join-Path $datasetDir $ScanAlignmentSubpath
$occlusionMesh = Join-Path $datasetDir $OcclusionMeshSubpath
$occlusionSplats = Join-Path $datasetDir $OcclusionSplatsSubpath
$multiResPointCloudDirectory = Join-Path $datasetDir $MultiResPointCloudSubpath
$imageBasePath = if ($ImageBaseSubpath) { Join-Path $datasetDir $ImageBaseSubpath } else { $datasetDir }
$statePath = Join-Path $datasetDir $StateSubpath
$outputFolder = Join-Path $datasetDir $OutputFolderSubpath
$observationsCache = Join-Path $datasetDir $ObservationsCacheSubpath

if (-not (Test-Path -LiteralPath $scanAlignment -PathType Leaf)) {
    throw "Scan alignment MeshLab project does not exist: $scanAlignment. Run run_icp_scan_aligner.ps1 first or pass -ScanAlignmentSubpath."
}
if (-not (Test-Path -LiteralPath $statePath -PathType Container)) {
    throw "COLMAP state directory does not exist: $statePath. Run run_scale_estimator.ps1 first or pass -StateSubpath."
}
if (-not (Test-Path -LiteralPath $imageBasePath -PathType Container)) {
    throw "Image base path does not exist: $imageBasePath"
}
if ((Test-Path -LiteralPath $outputFolder -PathType Container) -and -not $Overwrite) {
    $outputItems = @(Get-ChildItem -LiteralPath $outputFolder -Force -ErrorAction SilentlyContinue)
    if ($outputItems.Count -gt 0) {
        throw "Output folder already exists and is not empty: $outputFolder. Pass -Overwrite to allow writing into it."
    }
}

$arguments = @(
    "--scan_alignment_path", $scanAlignment,
    "--multi_res_point_cloud_directory_path", $multiResPointCloudDirectory,
    "--image_base_path", $imageBasePath,
    "--state_path", $statePath,
    "--output_folder_path", $outputFolder,
    "--observations_cache_path", $observationsCache,
    "--max_iterations", $MaxIterations,
    "--initial_scaling_factor", $InitialScalingFactor,
    "--target_scaling_factor", $TargetScalingFactor
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

if (-not $CameraIdsToIgnore -and $AutoIgnoreCubeMapCamera) {
    $CameraIdsToIgnore = [string](Find-CubeMapCameraId -CamerasPath (Join-Path $statePath "cameras.txt"))
}
if ($CameraIdsToIgnore) {
    $arguments += @("--camera_ids_to_ignore", $CameraIdsToIgnore)
}
if ($CacheObservations) {
    $arguments += @("--cache_observations", "1")
}
if ($WriteDebugPointClouds) {
    $arguments += @("--write_debug_point_clouds", "1")
}
if (Test-Path -LiteralPath $observationsCache -PathType Container) {
    $cacheFiles = @(Get-ChildItem -LiteralPath $observationsCache -Recurse -File -Filter "*.observed_indices" -ErrorAction SilentlyContinue)
    if ($cacheFiles.Count -eq 0) {
        Remove-Item -LiteralPath $observationsCache -Recurse -Force
    }
}

if ($ExtraArgs.Count -gt 0) {
    $arguments += $ExtraArgs
}

New-Item -ItemType Directory -Force -Path $multiResPointCloudDirectory, $outputFolder | Out-Null

Write-Host "Dataset dir:                 $datasetDir"
Write-Host "Scan alignment:              $scanAlignment"
Write-Host "Occlusion mesh:              $occlusionMesh"
Write-Host "Occlusion splats:            $occlusionSplats"
Write-Host "Multi-res point cloud cache: $multiResPointCloudDirectory"
Write-Host "Image base path:             $imageBasePath"
Write-Host "Input COLMAP state:          $statePath"
Write-Host "Output folder:               $outputFolder"
Write-Host "Observations cache:          $observationsCache"
Write-Host "ImageRegistrator:            $imageRegistrator"
Write-Host "Camera IDs to ignore:        $CameraIdsToIgnore"
Write-Host "Max iterations per scale:    $MaxIterations"
Write-Host "Initial scaling factor:      $InitialScalingFactor"
Write-Host "Target scaling factor:       $TargetScalingFactor"
Write-Host "Cache observations:          $CacheObservations"
Write-Host "Write debug point clouds:    $WriteDebugPointClouds"
if ($ExtraArgs.Count -gt 0) {
    Write-Host "Extra args:                  $($ExtraArgs -join ' ')"
}
Write-Host ""

& $imageRegistrator @arguments

if ($LASTEXITCODE -ne 0) {
    throw "ImageRegistrator failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Image alignment refinement finished."
Write-Host "Refined COLMAP states are in: $outputFolder"
