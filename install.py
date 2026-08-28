import os
import sys
import subprocess
import shutil

def run_command(command, cwd=None):
    try:
        print(f"Running: {' '.join(command)}")
        subprocess.check_call(command, cwd=cwd)
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"Command not found: {command[0]}")
        sys.exit(1)

def main():
    if not shutil.which("cmake"):
        print("CMake is not installed or not in PATH. Please install CMake and try again.")
        sys.exit(1)

    build_dir = "build"

    print("--- Configuring the project ---")
    run_command(["cmake", "-S", ".", "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"])

    print("--- Building the project ---")
    run_command(["cmake", "--build", build_dir, "--config", "Release"])

    print("--- Installing the project ---")
    if sys.platform == "win32":
        # On Windows, we typically install to a local directory or system path
        run_command(["cmake", "--install", build_dir])
        print("\nInstallation complete. Please ensure the installation directory is in your PATH.")
    else:
        # On Linux, usually requires sudo for system-wide installation
        print("Root privileges might be required for installation on Linux.")
        try:
            run_command(["sudo", "cmake", "--install", build_dir])
            print("\nInstallation complete.")
        except Exception:
            print("Failed to install using sudo. Trying without sudo...")
            run_command(["cmake", "--install", build_dir])

if __name__ == "__main__":
    main()
