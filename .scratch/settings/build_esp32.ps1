# issue 01/02 的真机侧构建校验（照 platform/esp32/README.md:52-64 激活 IDF 环境）
# ⚠️ -B 相对当前工作目录（仓库根）解析；-C 指工程目录。写成 '-B build' 会在宿主构建目录里跑 cmake 并毁缓存。
$ErrorActionPreference = 'Stop'
$env:IDF_PATH = 'E:\00_Software\03_Dev_Env\IDF'
$env:IDF_TOOLS_PATH = 'E:\00_Software\03_Dev_Env\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'E:\00_Software\03_Dev_Env\Espressif\tools\python_env\idf5.4_py3.11_env'
$py = "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe"
$kv = & $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value
$paths = @()
foreach ($line in $kv) {
  if ($line -match '^PATH=(.+)$') { $paths = @($Matches[1]) + $paths }
  elseif ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.+)$') { Set-Item -Path ('env:' + $Matches[1]) -Value $Matches[2] }
}
$env:PATH = (($paths | Select-Object -Unique) -join ';') + ';' + $env:PATH
& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 build 2>&1 | Select-Object -Last 30
exit $LASTEXITCODE
