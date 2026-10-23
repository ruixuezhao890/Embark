<#
device/tools/idf.ps1 —— 真机构建/烧录的一层薄壳：注入 ESP-IDF 环境，然后转调 idf.py。

为什么需要它：IDF 那套环境变量每次都得重新注入（道理见 device/README.md §3），
手打十几行容易漏一个，然后 CMake 挑到 PATH 里 Rust 的 esp 工具链报「Tool doesn't match」。
在命令行里直接用它，参数与 idf.py 一一对应（App 管理台不管烧录，这条路只有命令行）。

  pwsh -NoProfile -File device/tools/idf.ps1 -Task build
  pwsh -NoProfile -File device/tools/idf.ps1 -Task flash -Port COM3

构建目录固定 build-esp32/（已被 .gitignore 的 build-*/ 覆盖）；第一次跑会自动 set-target esp32s3。
#>
param(
  [ValidateSet('build', 'flash', 'version', 'fullclean')][string]$Task = 'build',
  [string]$Port = '',
  [string]$BuildDir = 'build-esp32'
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $env:IDF_PATH) { $env:IDF_PATH = 'E:\00_Software\03_Dev_Env\IDF' }
if (-not $env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH = 'E:\00_Software\03_Dev_Env\Espressif\tools' }
if (-not $env:IDF_PYTHON_ENV_PATH) { $env:IDF_PYTHON_ENV_PATH = Join-Path $env:IDF_TOOLS_PATH 'python_env\idf5.4_py3.11_env' }
$env:PYTHONUTF8 = '1'   # 不加则 idf.py 按 GBK 解码中文提示，直接 UnicodeDecodeError

$py = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
if (-not (Test-Path $py)) {
  Write-Error "找不到 IDF 的 python：$py（先核对 device/README.md §3 里的三条路径）"
  exit 1
}
$idfPy = Join-Path $env:IDF_PATH 'tools\idf.py'

# 按 idf_tools.py 自己的说法拼 PATH —— 不碰 activate.py（本机 .py 关联到了 node.exe，见 README §3）
$kv = & $py (Join-Path $env:IDF_PATH 'tools\idf_tools.py') export --format key-value
$paths = @()
foreach ($line in $kv) {
  if ($line -match '^PATH=(.+)$') { $paths = @($Matches[1]) + $paths }
  elseif ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.+)$') { Set-Item -Path ('env:' + $Matches[1]) -Value $Matches[2] }
}
$env:PATH = (($paths | Select-Object -Unique) -join ';') + ';' + $env:PATH

$proj = Join-Path $repo 'device\project'
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
$common = @('-C', $proj, '-B', $build)

if ($Task -eq 'version') {
  & $py $idfPy --version
  exit $LASTEXITCODE
}

# 第一次构建：IDF 会问「用哪个目标」，非交互环境会直接失败，所以先把 target 定死
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
  Write-Output '== 第一次构建，先 set-target esp32s3 =='
  & $py $idfPy @common set-target esp32s3
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Task) {
  'build'     { & $py $idfPy @common build }
  'fullclean' { & $py $idfPy @common fullclean }
  'flash' {
    if (-not $Port) { Write-Error '烧录要给 -Port（例如 COM3）'; exit 1 }
    & $py $idfPy @common -p $Port flash
  }
}
exit $LASTEXITCODE
