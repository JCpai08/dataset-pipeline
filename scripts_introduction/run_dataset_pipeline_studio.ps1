param(
    [string]$DatasetPipelineStudioExe = "",
    [switch]$BuildIfMissing = $true,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

function Resolve-DatasetPipelineStudio {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        throw "DatasetPipelineStudioExe does not exist: $ExplicitPath"
    }

    $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
    foreach ($path in @(
        (Join-Path $repoRoot "build_windows\RelWithDebInfo\DatasetPipelineStudio.exe"),
        (Join-Path $repoRoot "build_windows\Release\DatasetPipelineStudio.exe"),
        (Join-Path $repoRoot "build_windows\Debug\DatasetPipelineStudio.exe"),
        (Join-Path $repoRoot "build_windows\DatasetPipelineStudio.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\RelWithDebInfo\DatasetPipelineStudio.exe"),
        (Join-Path $repoRoot "build_RelWithDebInfo\DatasetPipelineStudio.exe")
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "DatasetPipelineStudio" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$studioExe = Resolve-DatasetPipelineStudio -ExplicitPath $DatasetPipelineStudioExe

if (-not $studioExe -and $BuildIfMissing) {
    $buildDir = Join-Path $repoRoot "build_windows"
    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        throw "Build directory does not exist: $buildDir. Configure the project first."
    }

    Write-Host "DatasetPipelineStudio.exe was not found. Building target ..."
    cmake --build $buildDir --target DatasetPipelineStudio --config RelWithDebInfo
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build DatasetPipelineStudio."
    }

    $studioExe = Resolve-DatasetPipelineStudio -ExplicitPath ""
}

if (-not $studioExe) {
    throw "DatasetPipelineStudio.exe was not found. Build it or pass -DatasetPipelineStudioExe explicitly."
}

Write-Host "Launching DatasetPipelineStudio:"
Write-Host "  $studioExe"

$startArgs = @{
    FilePath = $studioExe
    WorkingDirectory = (Split-Path -Parent $studioExe)
}
if ($Wait) {
    $startArgs["Wait"] = $true
}

Start-Process @startArgs
