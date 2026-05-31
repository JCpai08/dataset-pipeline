param(
    [string]$SplatCreatorExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "terrace",
    [string]$PointNormalCloudSubpath = "scan_clean\point_cloud_with_normals.ply",
    [string]$MeshSubpath = "surface_reconstruction\surface.ply",
    [string]$OutputSplatsSubpath = "surface_reconstruction\splats.ply",
    [double]$DistanceThreshold = 0.02,
    [double]$MaxSplatSize = -1,
    [switch]$Overwrite = $true
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

function Resolve-SplatCreator {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "SplatCreatorExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\SplatCreator.exe"),
        (Join-Path $repoRoot "build_windows\Release\SplatCreator.exe"),
        (Join-Path $repoRoot "build_windows\SplatCreator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\SplatCreator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\SplatCreator.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "SplatCreator" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "SplatCreator.exe was not found. Pass -SplatCreatorExe explicitly."
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

$splatCreator = Resolve-SplatCreator -ExplicitPath $SplatCreatorExe
$pointNormalCloud = Join-Path $datasetDir $PointNormalCloudSubpath
$mesh = Join-Path $datasetDir $MeshSubpath
$outputSplats = Join-Path $datasetDir $OutputSplatsSubpath
$outputDir = Split-Path -Parent $outputSplats

if (-not (Test-Path -LiteralPath $pointNormalCloud -PathType Leaf)) {
    throw "Point-normal cloud does not exist: $pointNormalCloud. Run run_normal_estimator.ps1 first or pass -PointNormalCloudSubpath."
}
if (-not (Test-Path -LiteralPath $mesh -PathType Leaf)) {
    throw "Surface mesh does not exist: $mesh. Run run_poisson_reconstruction.ps1 first or pass -MeshSubpath."
}
if (-not (Test-Path -LiteralPath $outputDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}
if ((Test-Path -LiteralPath $outputSplats -PathType Leaf) -and -not $Overwrite) {
    throw "Output splats already exist: $outputSplats. Pass -Overwrite to replace it."
}

$arguments = @(
    "--point_normal_cloud_path", $pointNormalCloud,
    "--mesh_path", $mesh,
    "--output_path", $outputSplats,
    "--distance_threshold", $DistanceThreshold
)

if ($MaxSplatSize -gt 0) {
    # The executable currently exposes this misspelled flag name.
    $arguments += @("--max_plat_size", $MaxSplatSize)
}

Write-Host "Dataset dir:        $datasetDir"
Write-Host "Point-normal cloud: $pointNormalCloud"
Write-Host "Surface mesh:       $mesh"
Write-Host "Output splats:      $outputSplats"
Write-Host "SplatCreator:       $splatCreator"
Write-Host "Distance threshold: $DistanceThreshold"
Write-Host "Max splat size:     $MaxSplatSize"
Write-Host ""

& $splatCreator @arguments

if ($LASTEXITCODE -ne 0) {
    throw "SplatCreator failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Splat creation finished."
Write-Host "Splats mesh: $outputSplats"
