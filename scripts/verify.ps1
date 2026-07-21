$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$drive = $null
foreach ($candidate in @('R:', 'S:', 'T:', 'U:')) {
    if (-not (Test-Path "${candidate}\")) {
        $drive = $candidate
        break
    }
}
if ($null -eq $drive) {
    throw 'R:、S:、T:、U: 均已占用，无法创建临时英文路径映射。'
}

$cmake = Get-Command cmake.exe -ErrorAction Stop
$ctest = Get-Command ctest.exe -ErrorAction Stop
$driveSuffix = $drive.Substring(0, 1).ToLowerInvariant()
$buildDirectory = "${drive}\build-mingw-${driveSuffix}"

subst.exe $drive $projectRoot
if ($LASTEXITCODE -ne 0) {
    throw "创建临时路径映射 $drive 失败。"
}

try {
    & $cmake.Source -S "${drive}\" -B $buildDirectory -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Debug
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败，退出码：$LASTEXITCODE"
    }

    & $cmake.Source --build $buildDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "项目构建失败，退出码：$LASTEXITCODE"
    }

    & $ctest.Source --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "CTest 失败，退出码：$LASTEXITCODE"
    }
} finally {
    subst.exe $drive /D | Out-Null
}
