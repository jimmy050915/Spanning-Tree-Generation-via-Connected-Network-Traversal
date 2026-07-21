$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$drive = 'R:'

if (Test-Path "${drive}\") {
    throw "$drive is already in use; the temporary path mapping cannot be created."
}

$cmake = Get-Command cmake.exe -ErrorAction Stop
$ctest = Join-Path (Split-Path -Parent $cmake.Source) 'ctest.exe'

subst.exe $drive $projectRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to create the temporary drive mapping.'
}

try {
    & $cmake.Source -S "${drive}\" -B "${drive}\build-mingw" -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Debug
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake.Source --build "${drive}\build-mingw"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $ctest --test-dir "${drive}\build-mingw" --output-on-failure
    exit $LASTEXITCODE
} finally {
    subst.exe $drive /D | Out-Null
}
