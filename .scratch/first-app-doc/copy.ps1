
$ErrorActionPreference = 'Stop'
$src = 'E:\01_Workspace\00_Active_projects\00_code\0_embedded_project\0_esp32_project\simulation_project_development'
$dst = Join-Path $env:TEMP 'embark_stopwatch_check'
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
New-Item -ItemType Directory -Path $dst | Out-Null
robocopy $src $dst /E /XD .git build build-esp32 .idea .vscode /NFL /NDL /NJH /NJS /NP | Out-Null
Write-Output ("copied: " + (Get-ChildItem $dst -Recurse -File | Measure-Object -Property Length -Sum).Sum)
