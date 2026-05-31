param(
    [string]$ICPScanAlignerExe = "",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [string]$InputProjectSubpath = "sparse_reconstruction_scaled\meshlab_project.mlp",
    [string]$OutputProjectSubpath = "scan_clean\scan_alignment.mlp",
    [double]$MaxCorrespondenceDistance = 0.01,
    [int]$MaxIterations = 100,
    [double]$ConvergenceThreshold = 1e-10,
    [int]$NumberOfScales = 4,
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

function Resolve-ICPScanAligner {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "ICPScanAlignerExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\ICPScanAligner.exe"),
        (Join-Path $repoRoot "build_windows\Release\ICPScanAligner.exe"),
        (Join-Path $repoRoot "build_windows\ICPScanAligner.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\ICPScanAligner.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\ICPScanAligner.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "ICPScanAligner" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "ICPScanAligner.exe was not found. Pass -ICPScanAlignerExe explicitly."
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

$icpScanAligner = Resolve-ICPScanAligner -ExplicitPath $ICPScanAlignerExe
$inputProject = Join-Path $datasetDir $InputProjectSubpath
$outputProject = Join-Path $datasetDir $OutputProjectSubpath
$outputDir = Split-Path -Parent $outputProject

if (-not (Test-Path -LiteralPath $inputProject -PathType Leaf)) {
    throw "Input MeshLab project does not exist: $inputProject. Run run_scale_estimator.ps1 first."
}
if (-not (Test-Path -LiteralPath $outputDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}
if ((Test-Path -LiteralPath $outputProject -PathType Leaf) -and -not $Overwrite) {
    throw "Output MeshLab project already exists: $outputProject. Pass -Overwrite to replace it."
}

Write-Host "Dataset dir:                 $datasetDir"
Write-Host "Input MeshLab project:       $inputProject"
Write-Host "Output MeshLab project:      $outputProject"
Write-Host "ICPScanAligner:              $icpScanAligner"
Write-Host "Max correspondence distance: $MaxCorrespondenceDistance"
Write-Host "Max iterations:              $MaxIterations"
Write-Host "Convergence threshold:       $ConvergenceThreshold"
Write-Host "Number of scales:            $NumberOfScales"
Write-Host ""

& $icpScanAligner `
    -i $inputProject `
    -o $outputProject `
    -d $MaxCorrespondenceDistance `
    --max_iterations $MaxIterations `
    --convergence_threshold $ConvergenceThreshold `
    --number_of_scales $NumberOfScales

if ($LASTEXITCODE -ne 0) {
    throw "ICPScanAligner failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "ICP scan alignment finished."
Write-Host "MeshLab project: $outputProject"
