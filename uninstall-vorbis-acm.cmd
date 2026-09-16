@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-vorbis-acm.ps1"
exit /b %ERRORLEVEL%
