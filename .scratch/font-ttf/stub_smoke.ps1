$ErrorActionPreference = "Stop"
$src = "C:/Users/ruixuezhao.LAPTOP-V8EB592S/AppData/Local/Temp/ui_raw.c"
if (-not (Test-Path -LiteralPath $src)) { throw "no raw" }
$stubInit = @"
static bool aidogui_fonts_init(void)
{
    return true;
}
"@
$stubDestroy = @"
static void aidogui_fonts_destroy(void)
{
}
"@
$uiCText = [System.IO.File]::ReadAllText($src)
$uiCPatched = [regex]::Replace($uiCText, '(?s)static void aidogui_fonts_destroy\(void\)[^{]*\{.*?\r?\n\}', $stubDestroy)
$uiCPatched = [regex]::Replace($uiCPatched, '(?s)static bool aidogui_fonts_init\(void\)[^{]*\{.*?\r?\n\}', $stubInit)
"raw had lv_font_load: " + ($uiCText -match 'lv_font_load')
"patched has lv_font_load: " + ($uiCPatched -match 'lv_font_load')
"changed: " + ($uiCPatched -ne $uiCText)
"patched lines: " + ($uiCPatched -split "`n").Count
"font slots kept: " + ([regex]::Matches($uiCPatched, 'lv_font_t \* ui_font_ui_heiti_\d+')).Count
"has stub init: " + ($uiCPatched -match 'return true;')
"tail of patched:"
($uiCPatched -split "`n")[20..45] -join "`n"