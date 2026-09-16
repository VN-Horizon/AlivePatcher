@echo off
setlocal
set "SRC=%~dp0game\vorbis.acm"
if not exist "%SRC%" set "SRC=%~dp0vorbis.acm"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-vorbis-acm.ps1" -Source "%SRC%"
exit /b %ERRORLEVEL%
