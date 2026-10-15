<#
.SYNOPSIS
  无头重新生成 app/aidogui_ui 的界面代码 + BIN 资源（不需要打开 AiDoGUI Designer）。

.DESCRIPTION
  主路径是 GUI：在 AiDoGUI Designer 里「打开工程」app/aidogui_ui/screen.aid → 改完屏 →
  导出向导（C 代码生成）→ 输出目录选 app/aidogui_ui/export。本脚本是可选路径：给「没开
  设计器 / CI / 想敲一条命令重生成」用，走的是同一个生成器、写到同一个目录，产物完全一样。

  产物（AiDoGUI 自己的导出布局，原样入库、原样拷到 S: 盘根下即可运行）：
    app/aidogui_ui/export/ui.c · ui.h · ui_helpers.* · ui_comm.* · ui_events.h · filelist.txt
    app/aidogui_ui/export/screens/ui_<屏id>.{c,h} · components/* · font/*.bin · image/*.bin
  屏表/控件表/字体表不在这里生成：cmake/embark_aidogui_tables.cmake 在配置期解析上面这些文件，
  写出构建目录 include/embark_aidogui_ui_tables.h。

.PARAMETER AiDoGuiHome
  AiDoGUI Designer 安装目录（含 AiDoGUIDesigner.exe）。默认依次找：参数 → $env:AIDOGUI_HOME →
  仓库根的 .aidogui-home 文件 → tools/aidogui/home.txt（本机私有，不入库）。

.EXAMPLE
  pwsh tools/aidogui/export.ps1
  pwsh tools/aidogui/export.ps1 -Project app/aidogui_ui/screen.aid -OutDir app/aidogui_ui/export
#>
[CmdletBinding()]
param(
  [string]$Project = 'app/aidogui_ui/screen.aid',
  [string]$OutDir = 'app/aidogui_ui/export',
  [string]$AiDoGuiHome = $env:AIDOGUI_HOME,
  [ValidateSet('BIN', 'C')][string]$ImageFormat = 'BIN',
  [ValidateSet('BIN', 'C')][string]$FontFormat = 'BIN'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

function Resolve-UnderRepo([string]$p) {
  if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
  return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $p))
}

function Resolve-AiDoGuiHome([string]$explicit) {
  $candidates = @()
  if ($explicit) { $candidates += $explicit }
  foreach ($f in @((Join-Path $repoRoot '.aidogui-home'), (Join-Path $PSScriptRoot 'home.txt'))) {
    if (Test-Path -LiteralPath $f) { $candidates += (Get-Content -LiteralPath $f -First 1).Trim() }
  }
  foreach ($c in $candidates) {
    if ($c -and (Test-Path -LiteralPath (Join-Path $c 'AiDoGUIDesigner.exe'))) { return (Resolve-Path -LiteralPath $c).Path }
  }
  throw "找不到 AiDoGUI Designer 安装目录。请设置环境变量 AIDOGUI_HOME，或把路径写进 .aidogui-home（仓库根）。试过：$($candidates -join '; ')"
}

$projectAbs = Resolve-UnderRepo $Project
$outAbs = Resolve-UnderRepo $OutDir
if (-not (Test-Path -LiteralPath $projectAbs)) { throw "工程文件不存在：$projectAbs" }

# 只在「仓库里那个 export 目录」上做清理：生成器不会删掉上一轮多出来的文件（屏删了、
# 改名了会留孤儿）。路径先解析成绝对路径再校验，避免删错地方。
$expected = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'app\aidogui_ui\export'))
if ($outAbs -ne $expected) {
  throw "拒绝清理：-OutDir 必须是 $expected（当前 $outAbs）。要用别的目录请手动导出。"
}
if (Test-Path -LiteralPath $outAbs) { Get-ChildItem -LiteralPath $outAbs -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Path $outAbs -Force | Out-Null

# 注意：别用 $home 这个名字 —— PowerShell 的 $HOME 是只读自动变量，赋值直接报错。
$designerHome = Resolve-AiDoGuiHome $AiDoGuiHome
$gen = Join-Path $PSScriptRoot 'gen_ui.cjs'
Write-Host "工程   : $projectAbs"
Write-Host "输出   : $outAbs"
Write-Host "设计器 : $designerHome（$ImageFormat 图片 / $FontFormat 字体）"

$prevRunAsNode = $env:ELECTRON_RUN_AS_NODE
try {
  $env:ELECTRON_RUN_AS_NODE = '1'
  # 必须把输出接进管道（Out-String）才会等这个 GUI 子系统 exe 跑完；而且它作为 GUI 进程
  # 不设 $LASTEXITCODE，判断成败看生成器自己打的 RESULT 行（下面还有产物检查兜底）。
  $output = (& (Join-Path $designerHome 'AiDoGUIDesigner.exe') $gen $projectAbs $outAbs $ImageFormat $FontFormat 2>&1 | Out-String)
  Write-Host $output.Trim()
  if ($output -notmatch 'RESULT: .*"success":true') { throw "生成器没有报告成功：$($output.Trim())" }
} finally {
  if ($null -eq $prevRunAsNode) { Remove-Item Env:ELECTRON_RUN_AS_NODE -ErrorAction SilentlyContinue }
  else { $env:ELECTRON_RUN_AS_NODE = $prevRunAsNode }
}

# 验收：生成代码在不在（屏头文件必须有 _screen_init/_screen_destroy），字体/图片 BIN 有几个
$uiC = Join-Path $outAbs 'ui.c'
$screenHeaders = @(Get-ChildItem -LiteralPath (Join-Path $outAbs 'screens') -Filter 'ui_*.h' -File -ErrorAction SilentlyContinue)
if (-not (Test-Path -LiteralPath $uiC)) { throw "生成结果里没有 ui.c：$outAbs" }
if ($screenHeaders.Count -eq 0) {
  $inside = Get-ChildItem -LiteralPath $outAbs -Recurse -Filter 'ui_*.h' -File | ForEach-Object { $_.FullName.Replace($outAbs, '') }
  throw ("生成结果里没有屏头文件（screens/ui_*.h）。实际产物：" + ($inside -join ', '))
}
foreach ($h in $screenHeaders) {
  $screenId = [System.IO.Path]::GetFileNameWithoutExtension($h.Name) -replace '^ui_', ''
  $text = Get-Content -LiteralPath $h.FullName -Raw
  if ($text -notmatch ('void\s+ui_' + $screenId + '_screen_init\s*\(\s*void\s*\)')) { throw ('屏 ' + $screenId + ' 缺少 ui_' + $screenId + '_screen_init()') }
  if ($text -notmatch ('void\s+ui_' + $screenId + '_screen_destroy\s*\(\s*void\s*\)')) { throw ('屏 ' + $screenId + ' 缺少 ui_' + $screenId + '_screen_destroy()') }
}

# 导航事件自检：设计器里给控件挂的「调用函数」动作会生成一句裸调用（embark_ui_open_clock();）。
# 事件丢了不会编译报错，只会"点了没反应" —— 所以这里数一遍：0 条 = 大声告警；调用了 hook 头里
# 没声明的名字 = 直接报错（那种情况链接必然失败，早报早好）。
$hookHeader = Join-Path $repoRoot 'app\common\aidogui_ui_hooks.h'
$hookNames = @()
if (Test-Path -LiteralPath $hookHeader) {
  $hookNames = @([regex]::Matches((Get-Content -LiteralPath $hookHeader -Raw), 'X\(([a-z0-9_]+)\)') |
    ForEach-Object { 'embark_ui_open_' + $_.Groups[1].Value })
}
$hookCalls = @{}
foreach ($c in @(Get-ChildItem -LiteralPath $outAbs -Recurse -File -Include '*.c' -ErrorAction SilentlyContinue)) {
  $text = Get-Content -LiteralPath $c.FullName -Raw
  foreach ($m in [regex]::Matches($text, 'embark_ui_open_[A-Za-z0-9_]*\s*\(\s*\)')) {
    $name = ($m.Value -replace '\s*\(\s*\)$', '')
    $hookCalls[$name] = 1 + [int]$hookCalls[$name]
  }
}
$hookUnknown = @($hookCalls.Keys | Where-Object { $hookNames -notcontains $_ })
if ($hookUnknown.Count -gt 0) {
  throw ("生成代码调用了未声明的导航入口：" + ($hookUnknown -join ', ') + "。请在 app/common/aidogui_ui_hooks.h 的表里加一行。")
}
if ($hookCalls.Count -eq 0) {
  Write-Warning ("生成代码里没有任何 embark_ui_open_*() 调用：设计器里的「调用函数」事件可能丢了（屏上点了不会切）。")
} else {
  Write-Host ("导航事件：" + (($hookCalls.Keys | Sort-Object | ForEach-Object { "$_ x$($hookCalls[$_])" }) -join ' · '))
}

$codeCount = @(Get-ChildItem -LiteralPath $outAbs -Recurse -File -Include '*.c', '*.h').Count
$fontCount = @(Get-ChildItem -LiteralPath (Join-Path $outAbs 'font') -File -ErrorAction SilentlyContinue).Count
$imageCount = @(Get-ChildItem -LiteralPath (Join-Path $outAbs 'image') -File -ErrorAction SilentlyContinue).Count
$screenNames = ($screenHeaders | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_.Name) -replace '^ui_', '' }) -join ', '
Write-Host "完成：代码 $codeCount 个 · 字体BIN $fontCount 个 · 图片BIN $imageCount 个 · 屏：$screenNames"
Write-Host "下一步：cmake --build build --target embark_host_ui（表由 cmake 配置期从 export/ 生成；改了导出产物要重新 configure 才刷新表）"
