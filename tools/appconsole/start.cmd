@echo off
rem App console: double-click to open http://127.0.0.1:7788/ in a browser.
rem Pass --no-open to keep it quiet, or set EMBARK_APPCONSOLE_PORT=7790 to change the port.
setlocal
cd /d "%~dp0..\.."
node tools\appconsole\server.mjs %*
if errorlevel 1 pause
