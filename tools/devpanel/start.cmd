@echo off
rem Embark dev panel launcher. ASCII only: cmd.exe reads this file in the OEM code page.
setlocal
cd /d "%~dp0..\.."
where node >nul 2>nul
if errorlevel 1 (
  echo [devpanel] Node.js was not found in PATH. Install Node 18+ first.
  pause
  exit /b 1
)
node "tools\devpanel\server.mjs"
if errorlevel 1 pause
endlocal
