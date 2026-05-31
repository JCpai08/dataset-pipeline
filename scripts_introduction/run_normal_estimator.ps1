param(
    [string]$NormalEstimatorExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [string]$InputProjectSubpath = "scan_clean\scan_alignment.mlp",
    [string]$OutputPlySubpath = "scan_clean\point_cloud_with_normals.ply",
    [int]$NeighborCount = 8,
    [double]$NeighborRadius = -1,
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

function Resolve-NormalEstimator {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "NormalEstimatorExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\NormalEstimator.exe"),
        (Join-Path $repoRoot "build_windows\Release\NormalEstimator.exe"),
        (Join-Path $repoRoot "build_windows\NormalEstimator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\NormalEstimator.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\NormalEstimator.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "NormalEstimator" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "NormalEstimator.exe was not found. Pass -NormalEstimatorExe explicitly."
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

$normalEstimator = Resolve-NormalEstimator -ExplicitPath $NormalEstimatorExe
$inputProject = Join-Path $datasetDir $InputProjectSubpath
$outputPly = Join-Path $datasetDir $OutputPlySubpath
$outputDir = Split-Path -Parent $outputPly

if (-not (Test-Path -LiteralPath $inputProject -PathType Leaf)) {
    throw "Input MeshLab project does not exist: $inputProject. Run run_icp_scan_aligner.ps1 first or pass -InputProjectSubpath."
}
if (-not (Test-Path -LiteralPath $outputDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}
if ((Test-Path -LiteralPath $outputPly -PathType Leaf) -and -not $Overwrite) {
    throw "Output PLY already exists: $outputPly. Pass -Overwrite to replace it."
}

Write-Host "Dataset dir:           $datasetDir"
Write-Host "Input MeshLab project: $inputProject"
Write-Host "Output PLY:            $outputPly"
Write-Host "NormalEstimator:       $normalEstimator"
Write-Host "Neighbor count:        $NeighborCount"
Write-Host "Neighbor radius:       $NeighborRadius"
Write-Host ""

if ($NeighborRadius -gt 0) {
    & $normalEstimator `
        -i $inputProject `
        -o $outputPly `
        --neighbor_radius $NeighborRadius
}
else {
    & $normalEstimator `
        -i $inputProject `
        -o $outputPly `
        --neighbor_count $NeighborCount
}

if ($LASTEXITCODE -ne 0) {
    throw "NormalEstimator failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Normal estimation finished."
Write-Host "Point cloud with normals: $outputPly"
