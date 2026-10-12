@echo off
setlocal
set "PS_SCRIPT=%~dp0FolderGuard.ps1"
if not exist "%PS_SCRIPT%" (
  echo FolderGuard.ps1 not found beside this launcher.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath PowerShell.exe -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%PS_SCRIPT%""'"
if errorlevel 1 pause