param(
    [string]$Qt6Dir = $env:Qt6_DIR,
    [string]$QtMinGwBin = $env:QT_MINGW_BIN,
    [ValidateSet(
        'all',
        'unit',
        'integration',
        'corruption',
        'graph-validation',
        'performance',
        'user-flow',
        'phase-six'
    )]
    [string]$TestLabel = 'all'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

function Find-Qt6Directory {
    param([string]$RequestedDirectory)

    if (-not [string]::IsNullOrWhiteSpace($RequestedDirectory)) {
        $candidate = [System.IO.Path]::GetFullPath($RequestedDirectory)
        if (Test-Path -LiteralPath (Join-Path $candidate 'Qt6Config.cmake')) {
            return $candidate
        }
        throw "Qt6Dir 不包含 Qt6Config.cmake：$candidate"
    }

    foreach ($commandName in @('qmake6.exe', 'qmake.exe')) {
        $qmake = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($null -eq $qmake) {
            continue
        }
        $prefix = (& $qmake.Source -query QT_INSTALL_PREFIX).Trim()
        $candidate = Join-Path $prefix 'lib\cmake\Qt6'
        if (Test-Path -LiteralPath (Join-Path $candidate 'Qt6Config.cmake')) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    $searchRoots = @('C:\Qt', 'D:\Qt', 'D:\A0\Qt') |
        Where-Object { Test-Path -LiteralPath $_ }
    $found = foreach ($root in $searchRoots) {
        Get-ChildItem -LiteralPath $root -Filter 'Qt6Config.cmake' -File `
            -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '[\\/]lib[\\/]cmake[\\/]Qt6[\\/]Qt6Config\.cmake$' }
    }
    $selected = $found | Sort-Object FullName -Descending | Select-Object -First 1
    if ($null -eq $selected) {
        throw '未找到 Qt 6。请安装 Qt 6 Widgets/Test，并通过 -Qt6Dir 或 Qt6_DIR 指定 Qt6Config.cmake 所在目录。'
    }
    return $selected.Directory.FullName
}

function Find-QtMinGwBin {
    param(
        [string]$RequestedDirectory,
        [string]$ResolvedQt6Dir
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedDirectory)) {
        $candidate = [System.IO.Path]::GetFullPath($RequestedDirectory)
        if ((Test-Path -LiteralPath (Join-Path $candidate 'g++.exe')) -and
            (Test-Path -LiteralPath (Join-Path $candidate 'mingw32-make.exe'))) {
            return $candidate
        }
        throw "QtMinGwBin 缺少 g++.exe 或 mingw32-make.exe：$candidate"
    }

    $qtPrefix = [System.IO.Path]::GetFullPath(
        (Join-Path $ResolvedQt6Dir '..\..\..')
    )
    $qtRoot = [System.IO.Path]::GetFullPath((Join-Path $qtPrefix '..\..'))
    $toolsDirectory = Join-Path $qtRoot 'Tools'
    if (Test-Path -LiteralPath $toolsDirectory) {
        $candidate = Get-ChildItem -LiteralPath $toolsDirectory -Directory `
            -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'bin' } |
            Where-Object {
                (Test-Path -LiteralPath (Join-Path $_ 'g++.exe')) -and
                (Test-Path -LiteralPath (Join-Path $_ 'mingw32-make.exe'))
            } |
            Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate
        }
    }

    throw '未找到与 Qt 匹配的 MinGW。请通过 -QtMinGwBin 或 QT_MINGW_BIN 指定包含 g++.exe 和 mingw32-make.exe 的目录。'
}

$resolvedQt6Dir = Find-Qt6Directory $Qt6Dir
$resolvedMinGwBin = Find-QtMinGwBin $QtMinGwBin $resolvedQt6Dir
$qtPrefix = [System.IO.Path]::GetFullPath(
    (Join-Path $resolvedQt6Dir '..\..\..')
)
$qtBin = Join-Path $qtPrefix 'bin'
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "未找到 Qt 部署工具：$windeployqt"
}

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
$buildDirectory = "${drive}\build-qt-${driveSuffix}"

Write-Host "Qt6_DIR: $resolvedQt6Dir"
Write-Host "Qt MinGW: $resolvedMinGwBin"
if ($TestLabel -ne 'all') {
    Write-Host "CTest 标签过滤：$TestLabel"
}

subst.exe $drive $projectRoot
if ($LASTEXITCODE -ne 0) {
    throw "创建临时路径映射 $drive 失败。"
}

$oldPath = $env:PATH
$oldQpaPlatform = $env:QT_QPA_PLATFORM
$oldQpaPlatformPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$resolvedMinGwBin;$qtBin;$oldPath"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtPrefix 'plugins\platforms'

    & $cmake.Source `
        -S "${drive}\" `
        -B $buildDirectory `
        -G 'MinGW Makefiles' `
        -DCMAKE_BUILD_TYPE=Debug `
        -DBUILD_TESTING=ON `
        "-DQt6_DIR=$resolvedQt6Dir" `
        "-DCMAKE_CXX_COMPILER=$(Join-Path $resolvedMinGwBin 'g++.exe')" `
        "-DCMAKE_MAKE_PROGRAM=$(Join-Path $resolvedMinGwBin 'mingw32-make.exe')"
    if ($LASTEXITCODE -ne 0) {
        throw "CMake 配置失败，退出码：$LASTEXITCODE"
    }

    & $cmake.Source --build $buildDirectory --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "项目构建失败，退出码：$LASTEXITCODE"
    }

    $ctestArguments = @(
        '--test-dir', $buildDirectory,
        '--output-on-failure',
        '--no-tests=error'
    )
    if ($TestLabel -ne 'all') {
        $ctestArguments += @('-L', $TestLabel)
    }

    & $ctest.Source @ctestArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CTest 失败，退出码：$LASTEXITCODE"
    }

    $applicationExecutable = Join-Path $buildDirectory 'novel_relation_app.exe'
    & $windeployqt `
        --compiler-runtime `
        --no-translations `
        $applicationExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "Qt 运行库部署失败，退出码：$LASTEXITCODE"
    }

    foreach ($relativePath in @(
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'libgcc_s_seh-1.dll',
        'libstdc++-6.dll',
        'libwinpthread-1.dll',
        'platforms\qwindows.dll'
    )) {
        $deployedPath = Join-Path $buildDirectory $relativePath
        if (-not (Test-Path -LiteralPath $deployedPath)) {
            throw "Qt 部署结果缺少运行文件：$deployedPath"
        }
    }
    Write-Host "可直接运行：$applicationExecutable"
} finally {
    $env:PATH = $oldPath
    if ($null -eq $oldQpaPlatform) {
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    } else {
        $env:QT_QPA_PLATFORM = $oldQpaPlatform
    }
    if ($null -eq $oldQpaPlatformPluginPath) {
        Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    } else {
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldQpaPlatformPluginPath
    }
    subst.exe $drive /D | Out-Null
}
