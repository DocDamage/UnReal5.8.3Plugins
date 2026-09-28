@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\Scripts\Verify-Suite.ps1" -EngineRoot "C:\Program Files\UE_5.8"
echo.
echo Done. Results are in Scripts\Output. You can close this window.
pause
