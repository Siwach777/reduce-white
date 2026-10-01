#!/usr/bin/env python3
"""Build, test, and install Reduce White Point into a user-local directory."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def default_prefix():
    if sys.platform == 'win32':
        return Path(os.environ.get('LOCALAPPDATA', str(Path.home() / 'AppData/Local'))) / 'ReduceWhite'
    if sys.platform == 'darwin':
        return Path.home() / 'Applications/ReduceWhite'
    return Path.home() / '.local'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    location = parser.add_mutually_exclusive_group()
    location.add_argument('--prefix', type=Path, help='Install into this directory')
    location.add_argument('--system', action='store_true', help='Install system-wide; may require administrator privileges')
    runtime = parser.add_mutually_exclusive_group()
    runtime.add_argument('--portable', action='store_true', help='Bundle Qt runtime libraries and plugins (Qt 6.5+)')
    runtime.add_argument('--system-qt', action='store_true', help='Use an existing Qt runtime; do not bundle dependencies')
    parser.add_argument('--qt-prefix', type=Path, help='Qt installation prefix passed to CMake')
    parser.add_argument('--build-dir', type=Path, help='Use an alternate build directory')
    parser.add_argument('--skip-tests', action='store_true', help='Skip regression checks before installation')
    args = parser.parse_args()
    if not shutil.which('cmake'):
        parser.exit(1, 'CMake is not installed or not in PATH. See docs/installation.md.\n')
    source = Path(__file__).resolve().parent
    build = (args.build_dir or source / 'build').expanduser().resolve()
    if args.system:
        prefix = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'ReduceWhite' if sys.platform == 'win32' else Path('/usr/local')
    else:
        prefix = args.prefix or default_prefix()
    prefix = prefix.expanduser().resolve()
    configure = ['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release',
                 f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DBUILD_TESTING={"OFF" if args.skip_tests else "ON"}']
    if args.portable or args.system_qt:
        configure.append(f'-DREDUCE_WHITE_DEPLOY_RUNTIME={"ON" if args.portable else "OFF"}')
    qt_prefix = args.qt_prefix
    if sys.platform == 'darwin' and not qt_prefix and shutil.which('brew'):
        for formula in ('qtbase', 'qt'):
            detected = subprocess.run(['brew', '--prefix', formula], capture_output=True, text=True)
            if detected.returncode == 0:
                qt_prefix = Path(detected.stdout.strip())
                break
    if qt_prefix:
        configure.append(f'-DCMAKE_PREFIX_PATH={qt_prefix.expanduser().resolve()}')
    commands = [configure, ['cmake', '--build', str(build), '--config', 'Release', '--parallel']]
    if not args.skip_tests:
        if not shutil.which('ctest'):
            parser.exit(1, 'CTest is not in PATH. Install CMake completely or pass --skip-tests.\n')
        commands.append(['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure'])
    install = ['cmake', '--install', str(build), '--config', 'Release']
    if args.system and sys.platform != 'win32' and os.geteuid() != 0:
        if not shutil.which('sudo'):
            parser.exit(1, 'System installation requires root or sudo; use the default local installation instead.\n')
        install.insert(0, 'sudo')
    commands.append(install)
    try:
        for command in commands:
            print('Running:', ' '.join(command), flush=True)
            subprocess.run(command, check=True)
    except (subprocess.CalledProcessError, OSError) as error:
        print(f'Installation failed: {error}', file=sys.stderr)
        return 1
    print(f'Installation complete. Add {prefix / "bin"} to PATH.')
    print('Uninstall paths are recorded in:', build / 'install_manifest.txt')
    return 0


if __name__ == '__main__':
    sys.exit(main())
