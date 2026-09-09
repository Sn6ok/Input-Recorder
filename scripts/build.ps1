<#
.SYNOPSIS
    Configure, build and test Input Recorder with the bundled VS 2022 toolchain.

.EXAMPLE
    ./scripts/build.ps1                # configure + build Release
    ./scripts/build.ps1 -Config Debug  # build Debug
    ./scripts/build.ps1 -Test          # build + run tests
    ./scripts/build.ps1 -Clean         # delete the build directory first
#>
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$Test,
    [switch]$Clean,
    [switch]$WarningsAsErrors
)

$ErrorActionPreference = 'Stop'

# Locate CMake/CTest: prefer PATH, fall back to the VS 2022 Build Tools bundle.
function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $bundled = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$name.exe"
    if (Test-Path $bundled) { return $bundled }
    throw "$name not found in PATH or the VS 2022 Build Tools bundle."
}

$cmake = Find-Tool 'cmake'
$ctest = Find-Tool 'ctest'

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

if ($Clean -and (Test-Path $build)) {
    Write-Host "Removing $build" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $build
}

$configureArgs = @('-S', $root, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($WarningsAsErrors) { $configureArgs += '-DIR_WARNINGS_AS_ERRORS=ON' }

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

& $cmake --build $build --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

if ($Test) {
    & $ctest --test-dir $build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
}

Write-Host "OK: $Config build complete." -ForegroundColor Green
Write-Host "Executable: $build\bin\$Config\InputRecorder.exe"
