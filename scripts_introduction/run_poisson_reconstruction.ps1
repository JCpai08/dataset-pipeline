param(
    [string]$PoissonReconExe = "E:\sh\tools\AdaptiveSolvers.x64\PoissonRecon.exe",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "terrace",
    [string]$InputPlySubpath = "scan_clean\point_cloud_with_normals.ply",
    [string]$OutputMeshSubpath = "surface_reconstruction\surface.ply",
    [int]$Depth = 13,
    [double]$Data = 16,
    [switch]$Colors = $true,
    [switch]$Density = $true,
    [int]$Threads = 0,
    [int]$MaxMemory = 0,
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

function Resolve-PoissonRecon {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "PoissonReconExe does not exist: $ExplicitPath"
    }

    $command = Get-Command "PoissonRecon" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "PoissonRecon.exe was not found. Pass -PoissonReconExe explicitly."
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

$poissonRecon = Resolve-PoissonRecon -ExplicitPath $PoissonReconExe
$inputPly = Join-Path $datasetDir $InputPlySubpath
$outputMesh = Join-Path $datasetDir $OutputMeshSubpath
$outputDir = Split-Path -Parent $outputMesh

if (-not (Test-Path -LiteralPath $inputPly -PathType Leaf)) {
    throw "Input point cloud with normals does not exist: $inputPly. Run run_normal_estimator.ps1 first or pass -InputPlySubpath."
}
if (-not (Test-Path -LiteralPath $outputDir -PathType Container)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}
if ((Test-Path -LiteralPath $outputMesh -PathType Leaf) -and -not $Overwrite) {
    throw "Output mesh already exists: $outputMesh. Pass -Overwrite to replace it."
}

$arguments = @(
    "--in", $inputPly,
    "--out", $outputMesh,
    "--depth", $Depth,
    "--data", $Data
)

if ($Colors) {
    $arguments += "--colors"
}
if ($Density) {
    $arguments += "--density"
}
if ($Threads -gt 0) {
    $arguments += @("--threads", $Threads)
}
if ($MaxMemory -gt 0) {
    $arguments += @("--maxMemory", $MaxMemory)
}

Write-Host "Dataset dir:       $datasetDir"
Write-Host "Input point cloud: $inputPly"
Write-Host "Output mesh:       $outputMesh"
Write-Host "PoissonRecon:      $poissonRecon"
Write-Host "Depth:             $Depth"
Write-Host "Data:              $Data"
Write-Host "Colors:            $Colors"
Write-Host "Density:           $Density"
Write-Host "Threads:           $Threads"
Write-Host "Max memory:        $MaxMemory"
Write-Host ""

& $poissonRecon @arguments

if ($LASTEXITCODE -ne 0) {
    throw "PoissonRecon failed with exit code $LASTEXITCODE."
}

Write-Host ""
Write-Host "Poisson reconstruction finished."
Write-Host "Surface mesh: $outputMesh"
