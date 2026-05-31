param(
    [string]$VcpkgExe = "E:\zj\vcpkg\vcpkg\vcpkg.exe",
    [string]$Triplet = "x64-windows",
    [string]$BuildDir = "build_windows",
    [string]$Config = "RelWithDebInfo",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Platform = "x64",
    [string]$OpenGvRepo = "https://github.com/laurentkneip/opengv.git",
    [string]$OpenGvArchiveUrl = "https://codeload.github.com/laurentkneip/opengv/zip/refs/heads/master",
    [string]$OpenGvSourceDir = "thirdparty_build\opengv\src",
    [string]$OpenGvBuildDir = "thirdparty_build\opengv\build",
    [string]$OpenGvInstallDir = "thirdparty_build\opengv\install",
    [switch]$SkipVcpkgInstall,
    [switch]$SkipOpenGvBuild,
    [switch]$Clean,
    [switch]$RunTests
)

$ErrorActionPreference = "Continue"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")

function Resolve-RepoPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

Push-Location $repoRoot
try {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) {
        throw "CMake was not found in PATH."
    }

    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if (-not $gitCommand) {
        throw "Git was not found in PATH."
    }

    if (-not (Test-Path -LiteralPath $VcpkgExe)) {
        throw "vcpkg.exe was not found: $VcpkgExe"
    }

    $vcpkgRoot = Split-Path -Parent $VcpkgExe
    $toolchainFile = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if (-not (Test-Path -LiteralPath $toolchainFile)) {
        throw "vcpkg CMake toolchain file was not found: $toolchainFile"
    }

    if (-not $SkipVcpkgInstall) {
        $packages = @(
            "eigen3:$Triplet",
            "boost-filesystem:$Triplet",
            "boost-system:$Triplet",
            "gmp:$Triplet",
            "glew:$Triplet",
            "glog:$Triplet",
            "qt5-base:$Triplet",
            "pcl:$Triplet",
            "opencv4:$Triplet"
        )
        Write-Host "Installing vcpkg packages for $Triplet..."
        & $VcpkgExe install @packages
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg install failed with exit code $LASTEXITCODE."
        }
    }

    $openGvSource = Resolve-RepoPath $OpenGvSourceDir
    $openGvBuild = Resolve-RepoPath $OpenGvBuildDir
    $openGvInstall = Resolve-RepoPath $OpenGvInstallDir

    if (-not $SkipOpenGvBuild) {
        if (-not (Test-Path -LiteralPath $openGvSource)) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $openGvSource) -Force | Out-Null
            Write-Host "Cloning OpenGV..."
            & git clone $OpenGvRepo $openGvSource
            if ($LASTEXITCODE -ne 0) {
                Write-Warning "git clone OpenGV failed with exit code $LASTEXITCODE. Falling back to archive download."
                Remove-Item -LiteralPath $openGvSource -Recurse -Force -ErrorAction SilentlyContinue

                $archivePath = Join-Path ([System.IO.Path]::GetTempPath()) ("opengv-{0}.zip" -f ([System.Guid]::NewGuid().ToString("N")))
                $extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("opengv-{0}" -f ([System.Guid]::NewGuid().ToString("N")))
                try {
                    $ProgressPreference = "SilentlyContinue"
                    Invoke-WebRequest -Uri $OpenGvArchiveUrl -OutFile $archivePath -TimeoutSec 120
                    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force
                    $expandedSource = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
                    if (-not $expandedSource) {
                        throw "OpenGV archive did not contain a source directory."
                    }
                    Move-Item -LiteralPath $expandedSource.FullName -Destination $openGvSource
                }
                finally {
                    Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
                    Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
        }

        if ($Clean -and (Test-Path -LiteralPath $openGvBuild)) {
            Remove-Item -LiteralPath $openGvBuild -Recurse -Force
        }

        Write-Host "Configuring OpenGV..."
        & cmake -Wno-deprecated -S $openGvSource -B $openGvBuild `
            -G $Generator -A $Platform `
            "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
            "-DVCPKG_TARGET_TRIPLET=$Triplet" `
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
            "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/MD /Od /Ob0 /Zi /DNDEBUG" `
            "-DCMAKE_C_FLAGS_RELWITHDEBINFO=/MD /Od /Ob0 /Zi /DNDEBUG" `
            "-DCMAKE_INSTALL_PREFIX=$openGvInstall"
        if ($LASTEXITCODE -ne 0) {
            throw "OpenGV configure failed with exit code $LASTEXITCODE."
        }

        Write-Host "Building and installing OpenGV..."
        & cmake --build $openGvBuild --config $Config --target INSTALL --parallel
        if ($LASTEXITCODE -ne 0) {
            throw "OpenGV build/install failed with exit code $LASTEXITCODE."
        }
    }

    $buildPath = Resolve-RepoPath $BuildDir
    if ($Clean -and (Test-Path -LiteralPath $buildPath)) {
        Remove-Item -LiteralPath $buildPath -Recurse -Force
    }

    $openGvPackageDir = Join-Path $openGvInstall "lib\cmake\opengv"
    if (-not (Test-Path -LiteralPath (Join-Path $openGvPackageDir "opengvConfig.cmake"))) {
        $openGvConfig = Get-ChildItem -LiteralPath (Join-Path $openGvInstall "lib\cmake") -Recurse -Filter "opengvConfig.cmake" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($openGvConfig) {
            $openGvPackageDir = Split-Path -Parent $openGvConfig.FullName
        }
    }
    Write-Host "Configuring dataset-pipeline..."
    & cmake -Wno-deprecated -S $repoRoot -B $buildPath `
        -G $Generator -A $Platform `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
        "-DVCPKG_TARGET_TRIPLET=$Triplet" `
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
        "-DCMAKE_BUILD_TYPE=$Config" `
        "-Dopengv_DIR=$openGvPackageDir"
    if ($LASTEXITCODE -ne 0) {
        throw "dataset-pipeline configure failed with exit code $LASTEXITCODE."
    }

    Write-Host "Building dataset-pipeline..."
    & cmake --build $buildPath --config $Config --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "dataset-pipeline build failed with exit code $LASTEXITCODE."
    }

    if ($RunTests) {
        Write-Host "Running tests..."
        & ctest --test-dir $buildPath -C $Config --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "ctest failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}
