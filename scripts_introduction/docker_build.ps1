param(
    [string]$ImageName = "eth3d-dataset-pipeline",
    [string]$Dockerfile = "Dockerfile",
    [string]$Context = ".",
    [string]$Proxy = "http://host.docker.internal:7890",
    [switch]$NoProxy,
    [switch]$NoCache,
    [switch]$PlainProgress,
    [switch]$DirectContext
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")

Push-Location $repoRoot
try {
    $dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
    if (-not $dockerCommand) {
        throw "Docker CLI was not found. Start Docker Desktop and make sure docker is available in PATH."
    }

    $args = @("build")
    if ($DirectContext) {
        $args += @("-f", $Dockerfile)
    }
    elseif ($Dockerfile -ne "Dockerfile") {
        throw "Custom Dockerfile paths are only supported with -DirectContext."
    }
    $args += @("-t", $ImageName)

    if ($NoCache) {
        $args += "--no-cache"
    }

    if ($PlainProgress) {
        $args += @("--progress", "plain")
    }

    if (-not $NoProxy) {
        $args += @(
            "--build-arg", "HTTP_PROXY=$Proxy",
            "--build-arg", "HTTPS_PROXY=$Proxy",
            "--build-arg", "http_proxy=$Proxy",
            "--build-arg", "https_proxy=$Proxy"
        )
    }

    $tempContextTar = $null
    if ($DirectContext) {
        $args += $Context
    }
    else {
        $tempContextTar = Join-Path ([System.IO.Path]::GetTempPath()) ("dataset-pipeline-docker-context-{0}.tar" -f ([System.Guid]::NewGuid().ToString("N")))
        $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
        if (-not $pythonCommand) {
            throw "Python was not found. Install Python or rerun with -DirectContext."
        }

        $contextScript = @'
import os
import sys
import tarfile

root, output = sys.argv[1], sys.argv[2]
skip_dirs = {".git", "build", "build_RelWithDebInfo", ".vs", ".idea", ".vscode", "__pycache__"}
skip_files = {"docker_build.log"}

with tarfile.open(output, "w", format=tarfile.USTAR_FORMAT) as tar:
    dockerfile_path = os.path.join(root, "Dockerfile")
    if os.path.isfile(dockerfile_path):
        tar.add(dockerfile_path, arcname="Dockerfile", recursive=False)

    for current, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for name in files:
            if name in skip_files:
                continue
            path = os.path.join(current, name)
            arcname = os.path.relpath(path, root).replace(os.sep, "/")
            if arcname == "Dockerfile":
                continue
            tar.add(path, arcname=arcname, recursive=False)
'@
        $contextScriptPath = Join-Path ([System.IO.Path]::GetTempPath()) ("create-docker-context-{0}.py" -f ([System.Guid]::NewGuid().ToString("N")))
        Set-Content -LiteralPath $contextScriptPath -Value $contextScript -Encoding ASCII
        try {
            & python $contextScriptPath $repoRoot $tempContextTar
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to create Docker build context tar with Python."
            }
        }
        finally {
            Remove-Item -LiteralPath $contextScriptPath -Force -ErrorAction SilentlyContinue
        }

        $args += "-"
    }

    Write-Host "Repository root: $repoRoot"
    Write-Host "Dockerfile:      $Dockerfile"
    Write-Host "Build context:   $Context"
    Write-Host "Image name:      $ImageName"
    Write-Host "Use proxy:       $(-not $NoProxy)"
    if (-not $NoProxy) {
        Write-Host "Proxy:           $Proxy"
    }
    Write-Host "No cache:        $NoCache"
    Write-Host "Plain progress:  $PlainProgress"
    Write-Host "Direct context:  $DirectContext"
    if ($tempContextTar) {
        Write-Host "Context tar:     $tempContextTar"
    }
    Write-Host ""
    Write-Host "Running:"
    if ($tempContextTar) {
        Write-Host "docker $($args -join ' ') < $tempContextTar"
    }
    else {
        Write-Host "docker $($args -join ' ')"
    }
    Write-Host ""

    if ($tempContextTar) {
        $quotedDockerArgs = ($args | ForEach-Object {
            if ($_ -match '[\s"]') {
                '"' + ($_ -replace '"', '\"') + '"'
            }
            else {
                $_
            }
        }) -join ' '
        $cmdLine = 'docker ' + $quotedDockerArgs + ' < "' + $tempContextTar + '"'
        cmd /c $cmdLine
    }
    else {
        & docker @args
    }
    if ($LASTEXITCODE -ne 0) {
        throw "docker build failed with exit code $LASTEXITCODE."
    }
}
finally {
    if ($tempContextTar -and (Test-Path -LiteralPath $tempContextTar)) {
        Remove-Item -LiteralPath $tempContextTar -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
