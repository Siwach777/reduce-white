#!/usr/bin/env python3
"""Extract a CPack archive and verify a relocated installation with its own runtime."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packages', type=Path, help='Directory containing one ZIP or TGZ package')
    parser.add_argument('smoke', type=Path, help='Built native IPC smoke-test executable')
    args = parser.parse_args()
    archives = sorted(path for path in args.packages.iterdir()
                      if path.is_file() and (path.name.endswith('.tar.gz') or path.suffix == '.zip'))
    if len(archives) != 1:
        parser.error('Expected exactly one ZIP or TGZ archive in the package directory')
    archive = archives[0].resolve()
    smoke = args.smoke.resolve()
    if not smoke.is_file():
        parser.error('The native IPC smoke-test executable does not exist')
    with tempfile.TemporaryDirectory(prefix='rw-package-') as directory:
        root = Path(directory)
        extracted = root / 'extracted'
        extracted.mkdir()
        # CMake's archive extractor preserves Unix executable permissions.
        subprocess.run(['cmake', '-E', 'tar', 'xf', str(archive)], cwd=extracted,
                       check=True, timeout=120)
        packages = [path for path in extracted.iterdir() if path.is_dir()]
        if len(packages) != 1:
            raise RuntimeError('Archive must contain one top-level package directory')
        destination = root / 'relocated package \u03a9'
        shutil.move(str(packages[0]), str(destination))
        binary = destination / 'bin' / ('reduce-white.exe' if os.name == 'nt' else 'reduce-white')
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise RuntimeError('The extracted command is missing or not executable')
        subprocess.run([str(smoke), str(binary), '--deployed'], check=True, timeout=45)
    print(f'Extracted and relocated package passed: {archive.name}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
