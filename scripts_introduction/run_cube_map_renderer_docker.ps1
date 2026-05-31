param(
    [string]$ImageName = "eth3d-dataset-pipeline",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [int]$Size = 2048
)

$ErrorActionPreference = "Stop"

$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
if (-not $dockerCommand) {
    throw "Docker CLI was not found. Start Docker Desktop and make sure docker is available in PATH."
}

if (-not (Test-Path -LiteralPath $DatasetRoot -PathType Container)) {
    throw "DatasetRoot does not exist or is not a directory: $DatasetRoot"
}

$resolvedDatasetRoot = (Resolve-Path -LiteralPath $DatasetRoot).Path
$volumeSpec = "${resolvedDatasetRoot}:/data"

function Get-RelativeUnixPath {
    param(
        [string]$Root,
        [string]$Path
    )

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathFull = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')

    if ($pathFull.Equals($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ""
    }

    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if (-not $pathFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolved dataset directory is outside DatasetRoot: $pathFull"
    }

    return $pathFull.Substring($prefix.Length).Replace('\', '/')
}

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

$containerDatasetSubdir = Get-RelativeUnixPath -Root $resolvedDatasetRoot -Path $datasetDir

$containerScript = @'
set -euo pipefail

DATASET_DIR="/data"
if [ -n "${DATASET_SUBDIR}" ]; then
  DATASET_DIR="/data/${DATASET_SUBDIR}"
fi

if [ ! -d "${DATASET_DIR}" ]; then
  echo "Dataset directory does not exist in container: ${DATASET_DIR}" >&2
  exit 1
fi

if [ ! -d "${DATASET_DIR}/scan_clean" ]; then
  echo "Expected scan directory was not found: ${DATASET_DIR}/scan_clean" >&2
  echo "Use -DatasetSubdir if E:\sh\dataset contains scene subdirectories." >&2
  exit 1
fi

mkdir -p "${DATASET_DIR}/cube_maps"

shopt -s nullglob
scans=("${DATASET_DIR}"/scan_clean/scan*.ply)
if [ ${#scans[@]} -eq 0 ]; then
  echo "No scan*.ply files found in: ${DATASET_DIR}/scan_clean" >&2
  exit 1
fi

echo "Dataset directory: ${DATASET_DIR}"
echo "Output directory:  ${DATASET_DIR}/cube_maps"
echo "Image size:        ${CUBEMAP_SIZE}"
echo "Scan count:        ${#scans[@]}"
echo

for scan in "${scans[@]}"; do
  name="$(basename "${scan}")"
  output="${DATASET_DIR}/cube_maps/${name}"
  echo "Rendering ${scan} -> ${output}"
  "${PIPELINE_PATH}/CubeMapRenderer" -c "${scan}" -o "${output}" --size "${CUBEMAP_SIZE}"
done
'@
$containerScript = $containerScript -replace "`r`n", "`n"

$containerScriptName = ".run_cube_map_renderer_$([System.Guid]::NewGuid().ToString("N")).sh"
$hostContainerScript = Join-Path $resolvedDatasetRoot $containerScriptName
$containerScriptPath = "/data/$containerScriptName"

Write-Host "Docker image:      $ImageName"
Write-Host "Windows data root: $resolvedDatasetRoot"
Write-Host "Container mount:   /data"
Write-Host "Dataset dir:       $datasetDir"
if ($containerDatasetSubdir) {
    Write-Host "Container dataset: /data/$containerDatasetSubdir"
}
else {
    Write-Host "Container dataset: /data"
}
Write-Host "Cube map size:     $Size"
Write-Host "Container script:  $containerScriptPath"
Write-Host ""

try {
    [System.IO.File]::WriteAllText(
        $hostContainerScript,
        $containerScript,
        [System.Text.UTF8Encoding]::new($false)
    )

    & docker run --rm `
        -e "DATASET_SUBDIR=$containerDatasetSubdir" `
        -e "CUBEMAP_SIZE=$Size" `
        -v $volumeSpec `
        $ImageName `
        bash $containerScriptPath

    if ($LASTEXITCODE -ne 0) {
        throw "CubeMapRenderer docker run failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item -LiteralPath $hostContainerScript -Force -ErrorAction SilentlyContinue
}
