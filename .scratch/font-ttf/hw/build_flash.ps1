$ErrorActionPreference = "Stop"
$env:IDF_PATH = "E:\00_Software\03_Dev_Env\IDF"
$env:IDF_TOOLS_PATH = "E:\00_Software\03_Dev_Env\Espressif\tools"
$py = "$env:IDF_TOOLS_PATH\python_env\idf5.4_py3.11_env\Scripts\python.exe"
foreach ($line in (& $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value)) {
    if ($line -like "Not using*") { continue }
    $i = $line.IndexOf("="); if ($i -lt 1) { continue }
    $k = $line.Substring(0, $i); $v = $line.Substring($i + 1)
    if ($k -eq "PATH") { $env:PATH = $v.Replace("%PATH%", $env:PATH) }
    else { Set-Item -Path ("env:" + $k) -Value $v }
}
Set-Location "E:\01_Workspace\00_Active_projects\00_code\0_embedded_project\0_esp32_project\simulation_project_development"
& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 build 2>&1 | Select-Object -Last 12
Write-Output "BUILD-EXIT=$LASTEXITCODE"
& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 -p COM3 flash 2>&1 | Select-Object -Last 20
Write-Output "FLASH-EXIT=$LASTEXITCODE"