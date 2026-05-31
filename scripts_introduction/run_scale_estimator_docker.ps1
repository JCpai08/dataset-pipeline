param(
    [string]$ImageName = "eth3d-dataset-pipeline",
    [string]$DatasetRoot = "E:\sh\dataset",
    [string]$DatasetSubdir = "",
    [int]$CubeMapSize = 2048,
    [int]$CubeMapFaceCameraId = -1,
    [string]$SfmModelSubdir = "colmap\sparse_txt",
    [string]$OutputSubdir = "sparse_reconstruction_scaled"
)

$ErrorActionPreference = "Stop"

$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
if (-not $dockerCommand) {
    throw "Docker CLI was not found. Start Docker Desktop and make sure docker is available in PATH."
}

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
        throw "The last camera entry in cameras.txt is not PINHOLE: $lastCameraLine. Pass -CubeMapFaceCameraId explicitly."
    }

    return [int]$parts[0]
}

if (-not (Test-Path -LiteralPath $DatasetRoot -PathType Container)) {
    throw "DatasetRoot does not exist or is not a directory: $DatasetRoot"
}

$resolvedDatasetRoot = (Resolve-Path -LiteralPath $DatasetRoot).Path
$volumeSpec = "${resolvedDatasetRoot}:/data"

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
$sfmModelSubdirUnix = $SfmModelSubdir.Replace('\', '/').Trim('/')
$outputSubdirUnix = $OutputSubdir.Replace('\', '/').Trim('/')

$sfmModelPath = Join-Path $datasetDir $SfmModelSubdir
$camerasPath = Join-Path $sfmModelPath "cameras.txt"
$imagesPath = Join-Path $sfmModelPath "images.txt"
$pointsPath = Join-Path $sfmModelPath "points3D.txt"
$scansPath = Join-Path $datasetDir "scan_clean"
$outputPath = Join-Path $datasetDir $OutputSubdir
$scaledModelPath = Join-Path $outputPath "colmap_model"

foreach ($requiredPath in @($camerasPath, $imagesPath, $pointsPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required COLMAP text model file does not exist: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $scansPath -PathType Container)) {
    throw "scan_clean does not exist: $scansPath"
}

if ($CubeMapFaceCameraId -lt 0) {
    $CubeMapFaceCameraId = Find-CubeMapCameraId -CamerasPath $camerasPath
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
if (Test-Path -LiteralPath $scaledModelPath) {
    $resolvedScaledModelPath = [System.IO.Path]::GetFullPath($scaledModelPath)
    $resolvedOutputPath = [System.IO.Path]::GetFullPath($outputPath).TrimEnd('\', '/')
    $expectedPrefix = $resolvedOutputPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedScaledModelPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove scaled model path outside output directory: $resolvedScaledModelPath"
    }
    Write-Host "Removing previous scaled COLMAP model: $resolvedScaledModelPath"
    Remove-Item -LiteralPath $resolvedScaledModelPath -Recurse -Force
}

$containerScript = @'
set -euo pipefail

DATASET_DIR="/data"
if [ -n "${DATASET_SUBDIR}" ]; then
  DATASET_DIR="/data/${DATASET_SUBDIR}"
fi

SFM_MODEL_PATH="${DATASET_DIR}/${SFM_MODEL_SUBDIR}"
SCANS_PATH="${DATASET_DIR}/scan_clean"
OUTPUT_PATH="${DATASET_DIR}/${OUTPUT_SUBDIR}"

if [ ! -d "${DATASET_DIR}" ]; then
  echo "Dataset directory does not exist in container: ${DATASET_DIR}" >&2
  exit 1
fi

if [ ! -d "${SCANS_PATH}" ]; then
  echo "Expected scan directory was not found: ${SCANS_PATH}" >&2
  exit 1
fi

for required in "${SFM_MODEL_PATH}/cameras.txt" "${SFM_MODEL_PATH}/images.txt" "${SFM_MODEL_PATH}/points3D.txt"; do
  if [ ! -f "${required}" ]; then
    echo "Required COLMAP text model file does not exist: ${required}" >&2
    exit 1
  fi
done

mkdir -p "${OUTPUT_PATH}"

echo "Dataset directory:       ${DATASET_DIR}"
echo "SfM model:               ${SFM_MODEL_PATH}"
echo "SfM image base path:     ${DATASET_DIR}"
echo "Scans path:              ${SCANS_PATH}"
echo "Output path:             ${OUTPUT_PATH}"
echo "Cube map face camera id: ${CUBE_MAP_FACE_CAMERA_ID}"
echo

"${PIPELINE_PATH}/SfMScaleEstimator" \
  -s "${SFM_MODEL_PATH}" \
  -si "${DATASET_DIR}" \
  -i "${SCANS_PATH}" \
  -o "${OUTPUT_PATH}" \
  --cube_map_face_camera_id "${CUBE_MAP_FACE_CAMERA_ID}"
'@
$containerScript = $containerScript -replace "`r`n", "`n"

$containerScriptName = ".run_scale_estimator_$([System.Guid]::NewGuid().ToString("N")).sh"
$hostContainerScript = Join-Path $resolvedDatasetRoot $containerScriptName
$containerScriptPath = "/data/$containerScriptName"

Write-Host "Docker image:            $ImageName"
Write-Host "Windows data root:       $resolvedDatasetRoot"
Write-Host "Container mount:         /data"
Write-Host "Dataset dir:             $datasetDir"
if ($containerDatasetSubdir) {
    Write-Host "Container dataset:       /data/$containerDatasetSubdir"
}
else {
    Write-Host "Container dataset:       /data"
}
Write-Host "SfM model:               $sfmModelPath"
Write-Host "SfM image base path:     $datasetDir"
Write-Host "Scans path:              $scansPath"
Write-Host "Output path:             $outputPath"
Write-Host "Cube map face camera id: $CubeMapFaceCameraId"
Write-Host "Container script:        $containerScriptPath"
Write-Host ""

try {
    [System.IO.File]::WriteAllText(
        $hostContainerScript,
        $containerScript,
        [System.Text.UTF8Encoding]::new($false)
    )

    & docker run --rm `
        -e "DATASET_SUBDIR=$containerDatasetSubdir" `
        -e "SFM_MODEL_SUBDIR=$sfmModelSubdirUnix" `
        -e "OUTPUT_SUBDIR=$outputSubdirUnix" `
        -e "CUBE_MAP_FACE_CAMERA_ID=$CubeMapFaceCameraId" `
        -v $volumeSpec `
        $ImageName `
        bash $containerScriptPath

    if ($LASTEXITCODE -ne 0) {
        throw "SfMScaleEstimator docker run failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item -LiteralPath $hostContainerScript -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "Scale estimation finished."
Write-Host "Scaled COLMAP model: $scaledModelPath"
Write-Host "MeshLab project:     $outputPath\meshlab_project.mlp"
