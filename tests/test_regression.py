#!/usr/bin/env python3
"""Integration tests use isolated, offscreen daemons and never touch the desktop daemon."""
import concurrent.futures
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

BINARY = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[1]


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CliTests(unittest.TestCase):
    def test_invalid_arguments_do_not_start_daemon(self):
        cases = [('--unknown',), ('--set',), ('--set=',), ('--set', 'nan'),
                 ('--set', 'inf'), ('--set', '0.5junk'), ('--set', '-0.1'),
                 ('--set', '1.1'), ('--set', '1e999'), ('--set', '0x1p-1'),
                 ('--set', '+-0'), ('--set', '0.' + '0' * 300),
                 ('--increase', '0'), ('--increase', '-0.2'), ('--decrease', 'nan'),
                 ('--increase', 'garbage'), ('--decrease', '2'),
                 ('--get', '--toggle'), ('--get', '--set', '0.2'),
                 ('--daemon', '--toggle'), ('--ddcutil',)]
        with tempfile.TemporaryDirectory(prefix='rw-cli-', **({'dir': '/tmp'} if os.name != 'nt' else {})) as directory:
            env = dict(os.environ, XDG_RUNTIME_DIR=directory, QT_QPA_PLATFORM='offscreen')
            env.pop('REDUCE_WHITE_INSTANCE', None)
            for args in cases:
                with self.subTest(args=args):
                    result = subprocess.run([BINARY, *args], env=env, capture_output=True, timeout=3)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(b'Error:', result.stderr)
            self.assertFalse(Path(directory, 'reduce-white-ipc.sock').exists())

    def test_help_and_version_without_display(self):
        env = dict(os.environ, QT_QPA_PLATFORM='nonexistent')
        for arg in ('--help', '--version'):
            result = subprocess.run([BINARY, arg], env=env, capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 0)
            self.assertTrue(result.stdout)


@unittest.skipIf(os.name == 'nt', 'Raw Unix socket tests; Windows needs native pipe tests')
class DaemonTests(unittest.TestCase):
    def setUp(self):
        self.runtime = tempfile.TemporaryDirectory(prefix='rw-test-', dir='/tmp')
        self.addCleanup(self.runtime.cleanup)
        self.env = dict(os.environ, XDG_RUNTIME_DIR=self.runtime.name, QT_QPA_PLATFORM='offscreen')
        self.env.pop('REDUCE_WHITE_INSTANCE', None)
        self.socket_path = str(Path(self.runtime.name, 'reduce-white-ipc.sock'))
        self.log = tempfile.TemporaryFile()
        self.addCleanup(self.log.close)
        self.daemons = []
        self.addCleanup(self.stop_daemons)
        self.start_daemon()

    def start_daemon(self, *args):
        process = subprocess.Popen([BINARY, '--daemon', *args], env=self.env,
                                   stdout=self.log, stderr=self.log)
        self.daemons.append(process)
        deadline = time.monotonic() + 3
        while True:
            if process.poll() is not None or time.monotonic() > deadline:
                self.log.seek(0)
                self.fail('Daemon failed to start: ' + self.log.read().decode())
            try:
                with socket.socket(socket.AF_UNIX) as connection:
                    connection.settimeout(.1)
                    connection.connect(self.socket_path)
                    connection.sendall(b'ping\n')
                    if connection.recv(256) == b'pong\n':
                        break
            except OSError:
                pass
            time.sleep(.01)
        return process

    def stop_daemons(self):
        for process in self.daemons:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)

    def cli(self, *args, expected=0):
        result = subprocess.run([BINARY, *args], env=self.env, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, expected, result.stderr.decode())
        return result.stdout.decode()

    def connect(self):
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(3)
        connection.connect(self.socket_path)
        self.addCleanup(connection.close)
        return connection

    def raw(self, command):
        with self.connect() as connection:
            connection.sendall(command)
            return connection.recv(256)

    def test_state_commands_and_numeric_forms(self):
        self.assertEqual(self.cli('--get'), 'opacity: 0.30 active: 1\n')
        for value in ('0.4', '.4', '+0.4', '4e-1'):
            self.cli('--set', value)
            self.assertEqual(self.cli('--get'), 'opacity: 0.40 active: 1\n')
        self.cli('--increase', '.1')
        self.cli('--toggle')
        self.assertEqual(self.cli('--status'), 'opacity: 0.50 active: 0\n')
        self.cli('--decrease')
        self.assertEqual(self.cli('--get'), 'opacity: 0.45 active: 1\n')
        self.cli('--set=0')
        self.cli('--decrease')
        self.assertEqual(self.cli('--get'), 'opacity: 0.00 active: 1\n')
        self.cli('--set=1')
        self.cli('--increase')
        self.assertEqual(self.cli('--get'), 'opacity: 1.00 active: 1\n')
        self.cli('--set', '0')
        self.assertIsNone(self.daemons[0].poll(), 'Hiding the last overlay must not quit the daemon')

    def test_invalid_wire_commands_preserve_state(self):
        commands = [b'set nan\n', b'set inf\n', b'set 0.4junk\n', b'set \n',
                    b'increasex\n', b'decreasejunk\n', b'increase -1\n',
                    b'increase 0\n', b'decrease 2\n', b'set 0.4\x00junk\n', b'\n']
        for command in commands:
            with self.subTest(command=command):
                self.assertTrue(self.raw(command).startswith(b'error:'))
                self.assertEqual(self.cli('--get'), 'opacity: 0.30 active: 1\n')

    def test_idle_client_does_not_block_other_clients(self):
        idle = self.connect()
        started = time.monotonic()
        self.assertIn('opacity:', self.cli('--get'))
        self.assertLess(time.monotonic() - started, 1)
        self.assertEqual(idle.recv(1), b'', 'Idle clients must expire')

    def test_fragmented_request_waits_for_complete_frame(self):
        connection = self.connect()
        connection.sendall(b'se')
        time.sleep(.02)
        self.assertIn('0.30', self.cli('--get'))
        connection.sendall(b't 0.')
        time.sleep(.02)
        connection.sendall(b'6\n')
        self.assertEqual(connection.recv(256), b'ok\n')
        self.assertIn('0.60', self.cli('--get'))

    def test_oversized_request_is_rejected(self):
        self.assertIn(b'error:', self.raw(b'x' * 300))
        self.assertIn('0.30', self.cli('--get'))

    def test_disconnected_client_does_not_crash_daemon(self):
        for _ in range(20):
            connection = self.connect()
            connection.sendall(b'increase\n')
            connection.close()
        self.assertIn('opacity:', self.cli('--get'))
        self.assertIsNone(self.daemons[0].poll())

    def test_concurrent_clients(self):
        self.cli('--set', '0')
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            responses = list(pool.map(lambda _: self.raw(b'increase .01\n'), range(30)))
        self.assertTrue(all(reply == b'ok\n' for reply in responses))
        self.assertIn('0.30', self.cli('--get'))

    def test_duplicate_daemon_cannot_replace_live_socket(self):
        inode = Path(self.socket_path).stat().st_ino
        competitors = [subprocess.Popen([BINARY, '--daemon'], env=self.env,
                                       stdout=self.log, stderr=self.log) for _ in range(6)]
        self.daemons.extend(competitors)
        for process in competitors:
            self.assertEqual(process.wait(timeout=5), 1)
        self.assertEqual(inode, Path(self.socket_path).stat().st_ino)
        self.assertIn('0.30', self.cli('--get'))
        self.assertEqual(Path(self.socket_path).stat().st_mode & 0o077, 0)
        self.assertEqual(Path(self.socket_path).stat().st_mode & 0o600, 0o600)

    def test_quit_cleans_up_socket_and_lock(self):
        self.cli('--quit')
        self.assertEqual(self.daemons[0].wait(timeout=3), 0)
        self.assertFalse(Path(self.socket_path).exists())
        self.assertFalse(Path(self.socket_path + '.lock').exists())
        self.assertIn('not running', self.cli('--quit'))

    def test_crash_recovery(self):
        self.daemons[0].kill()
        self.daemons[0].wait(timeout=3)
        self.start_daemon()
        self.assertIn('0.30', self.cli('--get'))

    def test_unexpected_socket_file_is_preserved(self):
        self.cli('--quit')
        self.daemons[0].wait(timeout=3)
        Path(self.socket_path).write_text('keep this file')
        self.cli('--daemon', expected=1)
        self.assertEqual(Path(self.socket_path).read_text(), 'keep this file')

    def test_auto_start_and_cleanup(self):
        self.cli('--quit')
        self.daemons[0].wait(timeout=3)
        try:
            self.cli('--set', '0.2')
            self.assertIn('0.20', self.cli('--get'))
        finally:
            self.cli('--quit')
        deadline = time.monotonic() + 3
        while Path(self.socket_path).exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertFalse(Path(self.socket_path).exists())

    def test_unresponsive_server_has_bounded_client_timeout(self):
        self.cli('--quit')
        self.daemons[0].wait(timeout=3)
        with socket.socket(socket.AF_UNIX) as fake:
            fake.bind(self.socket_path)
            fake.listen(1)
            started = time.monotonic()
            self.cli('--toggle', expected=1)
            self.assertLess(time.monotonic() - started, 2)
            self.assertFalse(Path(self.socket_path + '.lock').exists(), 'Must not auto-start after a connected request times out')

    def test_simultaneous_auto_start_applies_each_command_once(self):
        self.cli('--quit')
        self.daemons[0].wait(timeout=3)
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                list(pool.map(lambda _: self.cli('--increase', '.01'), range(8)))
            self.assertIn('0.38', self.cli('--get'))
        finally:
            self.cli('--quit')

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux-only ddcutil support')
    def test_hardware_updates_are_serialized_and_latest_value_wins(self):
        self.cli('--quit')
        self.daemons[0].wait(timeout=3)
        shim = Path(self.runtime.name, 'ddcutil')
        events = Path(self.runtime.name, 'events')
        shim.write_text('#!' + sys.executable + '\n' +
                        'import pathlib, sys, time\n' +
                        f'p = pathlib.Path({str(events)!r})\n' +
                        'with p.open("a") as f: f.write("start " + sys.argv[-1] + "\\n")\n' +
                        'time.sleep(.4)\n' +
                        'with p.open("a") as f: f.write("end " + sys.argv[-1] + "\\n")\n')
        shim.chmod(0o700)
        self.env['PATH'] = self.runtime.name + os.pathsep + self.env.get('PATH', '')
        self.start_daemon('--ddcutil')
        deadline = time.monotonic() + 3
        while not events.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(events.exists())
        self.cli('--set', '.2')
        time.sleep(.2)
        self.cli('--set', '.6')
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            lines = events.read_text().splitlines()
            if lines and lines[-1] == 'end 40': break
            time.sleep(.02)
        self.assertEqual(lines, ['start 70', 'end 70', 'start 40', 'end 40'])


class InstallerTests(unittest.TestCase):
    def test_failure_is_reported_without_retrying_install(self):
        installer = load_script('install')
        failure = subprocess.CalledProcessError(1, ['sudo', 'cmake'])
        with mock.patch.object(sys, 'argv', ['install.py', '--qt-prefix', str(ROOT)]), \
             mock.patch.object(installer.shutil, 'which', return_value='/usr/bin/tool'), \
             mock.patch.object(installer.subprocess, 'run', side_effect=[None, None, None, failure]) as run:
            self.assertEqual(installer.main(), 1)
            self.assertEqual(run.call_count, 4)

    def test_prefix_install_uses_release_and_no_sudo(self):
        installer = load_script('install')
        prefix = str(Path(tempfile.gettempdir(), 'rw-prefix').resolve())
        with mock.patch.object(sys, 'argv', ['install.py', '--prefix', prefix, '--qt-prefix', str(ROOT)]), \
             mock.patch.object(installer.shutil, 'which', return_value='/usr/bin/tool'), \
             mock.patch.object(installer.subprocess, 'run') as run:
            self.assertEqual(installer.main(), 0)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertIn('-DCMAKE_INSTALL_PREFIX=' + prefix, commands[0])
            self.assertEqual(commands[-1][0], 'cmake')
            self.assertEqual(commands[-1][-2:], ['--config', 'Release'])
            self.assertEqual(commands[0][2], str(ROOT))


if __name__ == '__main__':
    unittest.main()
