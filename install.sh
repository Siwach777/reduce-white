#!/bin/bash
# Simple installation script for Linux variants

set -e

echo "Checking for CMake..."
if ! command -v cmake &> /dev/null; then
    echo "CMake could not be found. Please install CMake and try again."
    exit 1
fi

echo "--- Configuring the project ---"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

echo "--- Building the project ---"
cmake --build build --config Release

echo "--- Installing the project ---"
echo "Root privileges might be required for installation..."
if command -v sudo &> /dev/null; then
    sudo cmake --install build
else
    cmake --install build
fi

echo "Installation complete!"
