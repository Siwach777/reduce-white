#!/usr/bin/env python3
"""Measure Linux client and socket latency using a private offscreen daemon."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def get_memory_usage(pid):
    """Return RSS, proportional and private memory in MiB, when procfs permits it."""
    try:
        fields = {}
        for line in Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[1].isdigit():
                fields[parts[0].rstrip(':')] = int(parts[1]) / 1024
        return {'RSS': fields['Rss'], 'PSS': fields['Pss'],
                'private': fields.get('Private_Clean', 0) + fields.get('Private_Dirty', 0) +
                           fields.get('Private_Hugetlb', 0)}
    except (OSError, KeyError):
        return None


def exchange(path, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(str(path))
        connection.sendall(command + b'\n')
        response = b''
        while not response.endswith(b'\n') and len(response) < 256:
            chunk = connection.recv(256 - len(response))
            if not chunk:
                break
            response += chunk
        if response not in (b'pong\n', b'ok\n'):
            raise RuntimeError(f'Unexpected daemon reply: {response!r}')


def show_memory(label, pid):
    memory = get_memory_usage(pid)
    if memory is None:
        print(f'{label}: unavailable (procfs access required)')
    else:
        print(label + ': ' + ', '.join(f'{key} {value:.2f} MiB' for key, value in memory.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path(__file__).resolve().parent / 'build/reduce-white')
    parser.add_argument('--iterations', type=int, default=1000)
    args = parser.parse_args()
    if not sys.platform.startswith('linux'):
        parser.error('This benchmark requires Linux Unix sockets and procfs.')
    if args.iterations <= 0:
        parser.error('--iterations must be positive')
    binary = args.binary.expanduser().resolve()
    if not binary.is_file():
        parser.error(f'Build the executable first: {binary}')
    with tempfile.TemporaryDirectory(prefix='rw-bench-') as runtime, tempfile.TemporaryFile() as log:
        env = dict(os.environ, XDG_RUNTIME_DIR=runtime, QT_QPA_PLATFORM='offscreen')
        env.pop('REDUCE_WHITE_INSTANCE', None)
        path = Path(runtime, 'reduce-white-ipc.sock')
        process = subprocess.Popen([str(binary), '--daemon'], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 3
            while not path.exists():
                if process.poll() is not None or time.monotonic() > deadline:
                    log.seek(0)
                    raise RuntimeError('Daemon startup failed: ' + log.read().decode(errors='replace'))
                time.sleep(.01)
            exchange(path, b'ping')
            print(f'Offscreen IPC benchmark: {args.iterations} sequential requests per measurement')
            show_memory('Before', process.pid)
            started = time.perf_counter()
            for index in range(args.iterations):
                command = '--increase' if index % 2 == 0 else '--decrease'
                subprocess.run([str(binary), command], env=env, check=True, timeout=3,
                               stdout=subprocess.DEVNULL, stderr=log)
            elapsed = time.perf_counter() - started
            print(f'Process launch + IPC: {elapsed / args.iterations * 1000:.3f} ms/request')
            started = time.perf_counter()
            for index in range(args.iterations):
                exchange(path, b'increase' if index % 2 == 0 else b'decrease')
            elapsed = time.perf_counter() - started
            print(f'Direct socket IPC: {elapsed / args.iterations * 1000:.3f} ms/request')
            show_memory('After', process.pid)
            exchange(path, b'quit')
            process.wait(timeout=3)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            print(f'Benchmark failed: {error}', file=sys.stderr)
            return 1
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
    return 0


if __name__ == '__main__':
    sys.exit(main())
