@echo off
setlocal
REM Windows entry point for the shared installer. Use --prefix for a custom location.
where py >nul 2>nul
if not errorlevel 1 (
    py -3 "%~dp0install.py" %*
    exit /b
)
where python >nul 2>nul
if not errorlevel 1 (
    python "%~dp0install.py" %*
    exit /b
)
echo Python 3 is required for this installer. See docs/installation.md for manual CMake commands.
exit /b 1
