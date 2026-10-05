# Spike 18 构建脚本：从 build-verify 缓存推导 MinGW 前缀（与仓库同款工具链）。
$ErrorActionPreference = "Stop"
$spike = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $spike "../../../..")

$cache = Get-Content (Join-Path $repo "build-verify/CMakeCache.txt") | Where-Object { $_ -like "CMAKE_CXX_COMPILER:FILEPATH=*" } | Select-Object -First 1
$cxx = $cache -replace "^CMAKE_CXX_COMPILER:FILEPATH=", ""
$mingw = Split-Path (Split-Path $cxx -Parent) -Parent
if (-not (Test-Path (Join-Path $mingw "bin/gcc.exe"))) { throw "MinGW root infer failed: $mingw (from $cxx)" }
Write-Host "MinGW prefix: $mingw"

$cmakeArgs = @("-S", $spike, "-B", (Join-Path $spike "build"), "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DCMAKE_C_COMPILER=" + (Join-Path $mingw "bin/gcc.exe"),
    "-DCMAKE_CXX_COMPILER=" + (Join-Path $mingw "bin/g++.exe"),
    "-DCMAKE_PREFIX_PATH=" + $mingw)
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& cmake --build (Join-Path $spike "build")
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Copy-Item (Join-Path $mingw "bin/SDL2.dll") (Join-Path $spike "build") -Force -ErrorAction SilentlyContinue
Write-Host "OK: $(Join-Path $spike 'build/spike18.exe')"