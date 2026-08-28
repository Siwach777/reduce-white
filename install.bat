@echo off
REM Simple installation script for Windows

echo Checking for CMake...
where cmake >nul 2>nul
if %errorlevel% neq 0 (
    echo CMake could not be found. Please install CMake and try again.
    exit /b 1
)

echo --- Configuring the project ---
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 exit /b %errorlevel%

echo --- Building the project ---
cmake --build build --config Release
if %errorlevel% neq 0 exit /b %errorlevel%

echo --- Installing the project ---
cmake --install build
if %errorlevel% neq 0 exit /b %errorlevel%

echo Installation complete! Please ensure the installation directory is in your PATH.
