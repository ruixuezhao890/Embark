#Requires -Version 7.0
<#
.SYNOPSIS
  Embark 开发循环：导出界面 → 构建宿主 → 跑起来，改一行就看见结果。

.DESCRIPTION
  一条命令把「画屏 → 看到界面」串起来（issue 06）：

    1. tools/aidogui/export.ps1 把 app/aidogui_ui/screen.aid 导出成 C 代码
    2. cmake configure（刷新构建期生成的屏/控件/字号表）+ build embark_host_ui
    3. 起 embark_host_ui --screen <屏名> --watch

  两件事要分清，否则会以为热重载坏了：

    * export/ 目录里的东西（图片 BIN 这类运行期才从 S: 盘读的）—— 重新导出后**窗口当场变**，
      进程不重启，由 embark_host_ui 的 --watch 负责（每 200 ms 看一次 export/）。
    * 生成的是 C 代码、编进二进制 —— 所以**文案与几何**改了必须重新导出 + 重新构建，
      那是本脚本第 1、2 步的活。

  改 .cpp/.h 也归本脚本管：它盯着 src/ app/ include/ platform/ cmake/ 的源文件，一发现变了
  就停掉窗口、重新构建、再起一个（进程级重启，不是 C++ 热重载 —— 那是 Q3 明确排除的方案）。

.PARAMETER Screen
  启动直接进这块屏（屏名 == App 名：launcher / clock / settings / weather / music / album）。
  不给则停在 launcher。

.PARAMETER SkipExport
  跳过导出（只改了 C++、想省掉一次导出时用）。

.PARAMETER NoRun
  只导出 + 构建，不启动窗口（CI / 手工检查用）。

.PARAMETER NoRestart
  不盯源码：窗口关掉脚本就结束（改了 .cpp 需要自己重跑）。

.PARAMETER ExeArgs
  透传给 embark_host_ui 的额外参数，例如 -ExeArgs '--quit-at','600','--delay','10'。

.EXAMPLE
  pwsh -NoProfile -File tools/aidogui/dev.ps1 -Screen clock

.EXAMPLE
  pwsh -NoProfile -File tools/aidogui/dev.ps1 -SkipExport -Screen settings
#>
[CmdletBinding()]
param(
  [string]$Screen = '',
  [switch]$SkipExport,
  [switch]$NoRun,
  [switch]$NoRestart,
  [string[]]$ExeArgs = @(),
  [int]$PollMs = 500
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $repoRoot 'build'
$exe = Join-Path $buildDir 'platform\host\embark_host_ui.exe'

# 盯哪些目录（源文件）。export/ 与 assets/ 不在里面：它们的变化由 --watch 在进程内消化，
# 不该触发重新构建（否则每导一次就重启一次窗口，白丢上下文）。
$watchRoots = @('src', 'app', 'include', 'platform', 'cmake', 'config') |
  ForEach-Object { Join-Path $repoRoot $_ } |
  Where-Object { Test-Path $_ }
$skipDirs = @(
  (Join-Path $repoRoot 'app\aidogui_ui\export'),
  (Join-Path $repoRoot 'app\aidogui_ui\assets')
)

function Get-SourceStamp {
  <# 源文件的最新写入时刻。拿不到就返回 0（不因此炸掉整个循环）。 #>
  $newest = [datetime]::MinValue
  foreach ($root in $watchRoots) {
    Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
      Where-Object {
        # 注意别在内层 Where-Object 里再用 $_：两层 $_ 会打架，先把文件存进局部变量。
        $file = $_
        $file.Extension -in @('.cpp', '.h', '.hpp', '.c', '.cmake') -and
        -not ($skipDirs | Where-Object { $file.FullName.StartsWith($_, 'OrdinalIgnoreCase') })
      } |
      ForEach-Object { if ($_.LastWriteTimeUtc -gt $newest) { $newest = $_.LastWriteTimeUtc } }
  }
  foreach ($top in @('CMakeLists.txt')) {
    $path = Join-Path $repoRoot $top
    if (Test-Path $path) {
      $stamp = (Get-Item -LiteralPath $path).LastWriteTimeUtc
      if ($stamp -gt $newest) { $newest = $stamp }
    }
  }
  return $newest
}

function Invoke-Build {
  Write-Host '[dev] cmake configure（刷新屏/控件/字号表）' -ForegroundColor Cyan
  # 必须显式 configure：app/aidogui_ui/CMakeLists.txt 用 GLOB CONFIGURE_DEPENDS 收集 export/ 下的
  # .c，控件改名（文件名不变、内容变）不会触发自动重跑，而屏表是 configure 期生成的。
  & cmake -S $repoRoot -B $buildDir
  if ($LASTEXITCODE -ne 0) { throw "cmake configure 失败（退出码 $LASTEXITCODE）" }

  Write-Host '[dev] build embark_host_ui' -ForegroundColor Cyan
  & cmake --build $buildDir --target embark_host_ui
  if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）" }
  if (-not (Test-Path $exe)) { throw "构建成功但找不到 $exe" }
}

function Start-Ui {
  $args = @()
  if ($Screen -ne '') { $args += @('--screen', $Screen) }
  $args += '--watch'
  if ($ExeArgs.Count -gt 0) { $args += $ExeArgs }
  Write-Host "[dev] 起窗口：embark_host_ui $($args -join ' ')" -ForegroundColor Green
  return Start-Process -FilePath $exe -ArgumentList $args -PassThru -NoNewWindow
}

$child = $null
try {
  if (-not $SkipExport) {
    Write-Host '[dev] 导出界面（screen.aid → app/aidogui_ui/export）' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'export.ps1')
    if ($LASTEXITCODE -ne 0) { throw "导出失败（退出码 $LASTEXITCODE）" }
  }

  Invoke-Build
  if ($NoRun) { Write-Host '[dev] -NoRun：构建完就结束' -ForegroundColor Green; exit 0 }

  $restart = $true
  while ($restart) {
    $restart = $false
    $child = Start-Ui
    $stamp = Get-SourceStamp
    while ($true) {
      Start-Sleep -Milliseconds $PollMs
      $child.Refresh()
      if ($child.HasExited) { break }  # 用户自己关了窗口
      $now = Get-SourceStamp
      if ($now -ne $stamp) {
        Write-Host '[dev] 源码变了 → 停窗口、重新构建、再起一个' -ForegroundColor Yellow
        if (-not $NoRestart) {
          Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue
          $child.WaitForExit(5000) | Out-Null
          Invoke-Build
          $restart = $true
        }
        break
      }
    }
  }
  Write-Host '[dev] 窗口已关闭，结束' -ForegroundColor Green
} finally {
  if ($null -ne $child) {
    $child.Refresh()
    if (-not $child.HasExited) { Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue }
  }
}
