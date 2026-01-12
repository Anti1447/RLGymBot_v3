@echo off
REM ============================================================================
REM Auto-Relaunch Render Script for GigaLearnBot (Batch Version)
REM ============================================================================
REM This is a simple wrapper that calls the PowerShell script.
REM
REM Usage:
REM   auto_render.bat
REM
REM To customize paths, edit the PowerShell script instead.
REM ============================================================================

echo Starting Auto-Render Script...
echo.

set PYTHONHOME=C:\Users\nhafe\AppData\Local\Programs\Python\Python311
set PYTHONPATH=%PYTHONHOME%\Lib;%PYTHONHOME%\DLLs

powershell.exe -ExecutionPolicy Bypass -File "%~dp0auto_render.ps1"

if errorlevel 1 (
    echo.
    echo Error running PowerShell script!
    echo Make sure PowerShell is available on your system.
    pause
)
