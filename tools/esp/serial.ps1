<#
tools/esp/serial.ps1 —— 读真机串口，一行一行打出来（开发台面板的「看串口」就是它）。

为什么不用 idf.py monitor：monitor 会接管终端、发 ANSI 控制序列、还等键盘输入，
塞进网页日志里全是乱码。这里只做一件事：把串口读成纯文本行。

  pwsh -NoProfile -File tools/esp/serial.ps1 -Port COM3

它会一直挂着直到进程被杀（面板上「停串口」就是杀它）。烧录之前要先停 —— 串口是独占的。
#>
param(
  [Parameter(Mandatory = $true)][string]$Port,
  [int]$Baud = 115200
)
$ErrorActionPreference = 'Stop'

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, 'None', 8, 'One')
$sp.ReadTimeout = 1000
# DTR/RTS 拉低：别让「打开串口」这个动作把板子复位了（烧录时 esptool 才需要动这两根线）
$sp.DtrEnable = $false
$sp.RtsEnable = $false
try {
  $sp.Open()
} catch {
  Write-Error "打不开 $Port：$($_.Exception.Message)（被别的程序占着？先关掉串口助手 / idf.py monitor）"
  exit 1
}
Write-Output "== 串口 $Port @ $Baud 已连上（面板上点停串口，或 Ctrl+C 结束）=="
try {
  while ($true) {
    try {
      $line = $sp.ReadLine()
      if ($line) { Write-Output $line.TrimEnd() }
    } catch [System.TimeoutException] {
      # 一秒没数据是正常的（真机不打印的时候就该安静）
    }
  }
} finally {
  try { $sp.Close() } catch { }
}
