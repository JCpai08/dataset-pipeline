param(
    [string]$NeighborPhotometricProbeExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "terrace",
    [string]$ScanAlignmentSubpath = "scan_clean\scan_alignment.mlp",
    [string]$ImageBaseSubpath = "",
    [string]$StateSubpath = "image_alignment_refined\scale_1_state",
    [string]$MultiResPointCloudSubpath = "multi_res_point_cloud_benchmark",
    [string]$OutputCsvSubpath = "",
    [string]$CameraIdsToIgnore = "",
    [switch]$IgnoreOcclusion = $false,
    [int]$PointScale = -1,
    [int]$MaxImages = -1,
    [double]$MinCloudDiff = 5,
    [int]$MaxCsvRows = 100000,
    [int]$QuantileSampleCount = 200000,
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

function Resolve-NeighborPhotometricProbe {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "NeighborPhotometricProbeExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\NeighborPhotometricProbe.exe"),
        (Join-Path $repoRoot "build_windows\Release\NeighborPhotometricProbe.exe"),
        (Join-Path $repoRoot "build_windows\NeighborPhotometricProbe.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\NeighborPhotometricProbe.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\NeighborPhotometricProbe.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "NeighborPhotometricProbe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "NeighborPhotometricProbe.exe was not found. Build it or pass -NeighborPhotometricProbeExe explicitly."
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

$probeExe = Resolve-NeighborPhotometricProbe -ExplicitPath $NeighborPhotometricProbeExe
$scanAlignment = Join-Path $datasetDir $ScanAlignmentSubpath
$imageBasePath = if ($ImageBaseSubpath) { Join-Path $datasetDir $ImageBaseSubpath } else { $datasetDir }
$statePath = Join-Path $datasetDir $StateSubpath
$multiResPointCloudDirectory = Join-Path $datasetDir $MultiResPointCloudSubpath
$outputCsvPath = if ($OutputCsvSubpath) { Join-Path $datasetDir $OutputCsvSubpath } else { "" }

if (-not (Test-Path -LiteralPath $statePath -PathType Container)) {
    throw "COLMAP state directory does not exist: $statePath"
}
if (-not (Test-Path -LiteralPath $imageBasePath -PathType Container)) {
    throw "Image base path does not exist: $imageBasePath"
}
if (-not (Test-Path -LiteralPath $multiResPointCloudDirectory -PathType Container)) {
    throw "Multi-res point cloud directory does not exist: $multiResPointCloudDirectory"
}
if (-not $IgnoreOcclusion -and -not (Test-Path -LiteralPath $scanAlignment -PathType Leaf)) {
    throw "Scan alignment MeshLab project does not exist: $scanAlignment. Pass -IgnoreOcclusion or -ScanAlignmentSubpath."
}

$arguments = @(
    "--state_path", $statePath,
    "--image_base_path", $imageBasePath,
    "--multi_res_point_cloud_directory_path", $multiResPointCloudDirectory,
    "--min_cloud_diff", $MinCloudDiff,
    "--max_csv_rows", $MaxCsvRows,
    "--quantile_sample_count", $QuantileSampleCount
)

if ($IgnoreOcclusion) {
    $arguments += @("--ignore_occlusion", "1")
}
else {
    $arguments += @("--scan_alignment_path", $scanAlignment)
}
if ($CameraIdsToIgnore) {
    $arguments += @("--camera_ids_to_ignore", $CameraIdsToIgnore)
}
if ($PointScale -ge 0) {
    $arguments += @("--point_scale", $PointScale)
}
if ($MaxImages -ge 0) {
    $arguments += @("--max_images", $MaxImages)
}
if ($outputCsvPath) {
    $outputDir = Split-Path -Parent $outputCsvPath
    if ($outputDir) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }
    $arguments += @("--output_csv_path", $outputCsvPath)
}
if ($ExtraArgs.Count -gt 0) {
    $arguments += $ExtraArgs
}

Write-Host "Dataset dir:                  $datasetDir"
Write-Host "Probe exe:                    $probeExe"
Write-Host "Input COLMAP state:           $statePath"
Write-Host "Image base path:              $imageBasePath"
Write-Host "Multi-res point cloud:        $multiResPointCloudDirectory"
Write-Host "Ignore occlusion:             $IgnoreOcclusion"
if (-not $IgnoreOcclusion) {
    Write-Host "Scan alignment:               $scanAlignment"
}
Write-Host "Point scale:                  $PointScale"
Write-Host "Max images:                   $MaxImages"
Write-Host "Minimum cloud diff:           $MinCloudDiff"
Write-Host "Max CSV rows:                 $MaxCsvRows"
if ($outputCsvPath) {
    Write-Host "Output CSV:                   $outputCsvPath"
}
if ($ExtraArgs.Count -gt 0) {
    Write-Host "Extra args:                   $($ExtraArgs -join ' ')"
}
Write-Host ""

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
& $probeExe @arguments
$exitCode = $LASTEXITCODE
$stopwatch.Stop()
if ($exitCode -ne 0) {
    throw "NeighborPhotometricProbe failed with exit code $exitCode."
}

Write-Host ""
Write-Host "PowerShell measured wall time: $($stopwatch.Elapsed.TotalSeconds) s"
