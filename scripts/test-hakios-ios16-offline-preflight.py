#!/usr/bin/env python3
"""Host-only regression harness; all package/device data and writes are in memory.

Run with python -B scripts/test-hakios-ios16-offline-preflight.py.
This does not establish readiness of the real package or authorize device work.
"""
import argparse
import builtins
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from copy import deepcopy
import io
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name('boot-hakios-ios16.py')
SUBJECT = compile(SCRIPT.read_bytes(), str(SCRIPT), 'exec')
READY = 'reports/hakios-ios16-boot-ready.json'
CONTRACT = 'artifacts/frozen.json'
PACKAGE = 'reports/hakios-ios16-package-host-checks.json'
TARGET = 'reports/local/hakios-device-target.json'
HOST = 'reports/hakios-ios16-host-checks.json'
GUARD = 'reports/local/ios16-r19-boot-intent.json'


class CopyBoundary(RuntimeError):
    """Stop an entirely simulated boot path before any copy, intent or capture."""


class Fixture:
    def __init__(self, simulate_device=False):
        self.simulate_device = simulate_device
        self.consumed = False
        self.usb_available = False
        self.fail_stage = None
        self.events = []
        self.commands = []
        self.directories = []
        self.writes = []
        self.copy_attempts = []
        self.stdout, self.stderr = io.StringIO(), io.StringIO()
        fixture = self

        class FixturePath(PurePosixPath):
            """No method on this path type accesses the host filesystem."""
            def exists(self):
                fixture.events.append(('exists', str(self)))
                if self == fixture.root / GUARD:
                    return fixture.consumed
                raise AssertionError('Unexpected existence check: ' + str(self))

            def mkdir(self, **kwargs):
                fixture.directories.append(self)

            def stat(self):
                if self != fixture.root / 'artifacts/boot.img':
                    raise AssertionError('Unexpected stat: ' + str(self))
                fixture.events.append(('stat', str(self)))
                return SimpleNamespace(st_size=512)

            def is_file(self):
                fixture.events.append(('is_file', str(self)))
                if str(self) != 'E:/IOS-MIX1-artifacts/host-usb-venv/Scripts/python.exe':
                    raise AssertionError('Unexpected file check: ' + str(self))
                return fixture.usb_available

            def open(self, *args, **kwargs):
                return fixture.reject_io(self)

            write_text = open
            write_bytes = open
            touch = open

        self.path_type = FixturePath
        self.root = FixturePath('/offline-fixture')
        self.inputs = {'fixture_source': 'current'}
        ready = dict(passed=True, bootable_package_ready=True, inputs=self.inputs,
                     frozen_contract_path=CONTRACT, evidence_sha256={'reports/evidence.json': 'evidence-sha'},
                     image_path='artifacts/boot.img', image_sha256='image-sha', revision=19,
                     cmdline='fixture-only')
        self.documents = {
            'config/manifest.json': {'fixture_manifest': True},
            'project.json': {'migration': {'phase4_approved': True}},
            READY: ready, CONTRACT: deepcopy(ready),
            PACKAGE: dict(passed=True, image_sha256='image-sha',
                          package_script_sha256='package-sha', test_script_sha256='test-sha'),
            TARGET: dict(serial='fixture-serial', product='fixture-product', userdata_bytes=4096),
            HOST: dict(passed=True, evidence_sha256={'capture_script': 'capture-sha'}),
        }
        self.hashes = {
            'reports/evidence.json': 'evidence-sha', 'artifacts/boot.img': 'image-sha',
            'scripts/package-hakios-ios16.py': 'package-sha',
            'scripts/test-hakios-ios16-package.py': 'test-sha',
            'scripts/capture-hakios-usb.py': 'capture-sha',
        }
        self.responses = {
            ('devices',): (0, 'fixture-serial fastboot\n', ''),
            ('getvar', 'product'): (0, '', 'product: fixture-product\n'),
            ('oem', 'device-info'): (0, '', 'Device unlocked: true\n'),
            ('getvar', 'partition-size:userdata'): (0, '', 'partition-size:userdata: 1000\n'),
            ('getvar', 'battery-soc-ok'): (0, '', 'battery-soc-ok: yes\n'),
            ('getvar', 'max-download-size'): (0, '', 'max-download-size: 1000\n'),
        }

    @staticmethod
    def reject_io(*args, **kwargs):
        raise AssertionError('Host filesystem I/O tripwire')

    @staticmethod
    def reject_process(*args, **kwargs):
        raise AssertionError('Real subprocess tripwire: no commands may run')

    @staticmethod
    def require(condition, message):
        if not condition:
            raise RuntimeError(message)

    def stage(self, name):
        self.events.append((name,))
        self.require(self.fail_stage != name, 'Fixture ' + name + ' rejected')

    def read(self, path):
        name = path.relative_to(self.root).as_posix()
        self.events.append(('read', name))
        return deepcopy(self.documents[name])

    def sha(self, path):
        name = path.relative_to(self.root).as_posix()
        self.events.append(('sha', name))
        return self.hashes[name]

    def write(self, path, value):
        self.writes.append((path.relative_to(self.root).as_posix(), deepcopy(value)))

    def command(self, argv, **kwargs):
        # This is an in-memory emulator, never a forwarding subprocess wrapper.
        if not self.simulate_device:
            return self.reject_process(argv)
        argv = tuple(map(str, argv))
        if argv[0] != 'C:/platform-tools/fastboot.exe':
            raise AssertionError('Unexpected simulated executable: ' + repr(argv))
        self.commands.append(argv)
        if argv[1:] == ('devices',):
            key = ('devices',)
        else:
            if argv[1:3] != ('-s', 'fixture-serial'):
                raise AssertionError('Simulated command lacks exact target serial: ' + repr(argv))
            key = argv[3:]
        if key not in self.responses:
            raise AssertionError('Unexpected simulated command: ' + repr(argv))
        code, stdout, stderr = self.responses[key]
        return SimpleNamespace(returncode=code, stdout=stdout, stderr=stderr)

    def copyfile(self, source, destination):
        self.copy_attempts.append((source, destination))
        raise CopyBoundary('All simulated pre-copy boot gates passed')

    def invoke(self, *args):
        hakios = ModuleType('hakios')
        hakios.ROOT, hakios.MANIFEST = self.root, self.root / 'config/manifest.json'
        hakios.read, hakios.sha, hakios.write = self.read, self.sha, self.write
        hakios.require = self.require
        hakios.policy = lambda spec: self.stage('policy')
        hakios.frozen_check = lambda: self.stage('frozen_check')
        hakios.inputs = lambda spec: deepcopy(self.inputs)
        pathlib = ModuleType('pathlib')
        pathlib.Path = self.path_type
        child = ModuleType('subprocess')
        child.run, child.Popen = self.command, self.reject_process
        child.STDOUT, child.CREATE_NO_WINDOW = -2, 0
        shutil = ModuleType('shutil')
        shutil.copyfile = self.copyfile
        # argparse uses shutil's terminal-size helper even though the subject only copies.
        shutil.get_terminal_size = lambda fallback=(80, 24): SimpleNamespace(columns=fallback[0], lines=fallback[1])
        namespace = {'__name__': 'isolated_offline_preflight', '__file__': str(SCRIPT)}
        with ExitStack() as stack:
            stack.enter_context(patch.dict(sys.modules, hakios=hakios, pathlib=pathlib,
                                          subprocess=child, shutil=shutil))
            stack.enter_context(patch.object(sys, 'argv', [str(SCRIPT), *args]))
            stack.enter_context(patch.object(builtins, 'open', side_effect=self.reject_io))
            stack.enter_context(patch.object(time, 'sleep', side_effect=self.reject_process))
            for name in ('run', 'Popen', 'call', 'check_call', 'check_output'):
                stack.enter_context(patch.object(subprocess, name, side_effect=self.reject_process))
            stack.enter_context(redirect_stdout(self.stdout))
            stack.enter_context(redirect_stderr(self.stderr))
            exec(SUBJECT, namespace)
            return namespace['main']()


class OfflinePreflightTests(unittest.TestCase):
    def assert_before_target(self, fixture):
        self.assertNotIn(('read', TARGET), fixture.events)
        self.assertEqual(fixture.directories, [])
        self.assertEqual(fixture.writes, [])
        self.assertEqual(fixture.commands, [])
        self.assertEqual(fixture.copy_attempts, [])

    def test_offline_returns_after_host_checks_before_target_or_side_effects(self):
        fixture = Fixture()
        del fixture.documents[TARGET]
        del fixture.documents[HOST]
        self.assertIsNone(fixture.invoke('--offline'))
        self.assert_before_target(fixture)
        self.assertEqual(fixture.stdout.getvalue(),
                         'PASS: offline iOS16 R19 package readiness (host checks only).\n')
        self.assertEqual(fixture.stderr.getvalue(), '')
        self.assertEqual(fixture.events, [
            ('read', 'config/manifest.json'), ('policy',), ('frozen_check',),
            ('read', 'project.json'), ('read', READY), ('read', CONTRACT),
            ('sha', 'reports/evidence.json'), ('sha', 'artifacts/boot.img'),
            ('read', PACKAGE), ('sha', 'scripts/package-hakios-ios16.py'),
            ('sha', 'scripts/test-hakios-ios16-package.py'),
            ('exists', str(fixture.root / GUARD)),
        ])

    def test_modes_are_mutually_exclusive_before_any_validation(self):
        for args in (('--offline', '--boot'), ('--boot', '--offline')):
            with self.subTest(args=args):
                fixture = Fixture()
                with self.assertRaises(SystemExit) as error:
                    fixture.invoke(*args)
                self.assertEqual(error.exception.code, 2)
                self.assertIn('not allowed with argument', fixture.stderr.getvalue())
                self.assertEqual(fixture.events, [])
                self.assert_before_target(fixture)

    def test_host_validation_failures_still_reject_every_mode(self):
        cases = [
            ('policy', lambda f: setattr(f, 'fail_stage', 'policy'), 'Fixture policy rejected'),
            ('frozen source', lambda f: setattr(f, 'fail_stage', 'frozen_check'), 'Fixture frozen_check rejected'),
            ('phase4', lambda f: f.documents['project.json']['migration'].update(phase4_approved=False), 'Phase4 not authorized'),
            ('passed', lambda f: f.documents[READY].update(passed=False), 'source/provenance differs'),
            ('bootable', lambda f: f.documents[READY].update(bootable_package_ready=False), 'source/provenance differs'),
            ('inputs', lambda f: f.documents[READY].update(inputs={}), 'source/provenance differs'),
            ('contract', lambda f: f.documents[CONTRACT].update(revision=18), 'Frozen package contract changed'),
            ('evidence', lambda f: f.hashes.update({'reports/evidence.json': 'stale'}), 'Frozen evidence changed'),
            ('image', lambda f: f.hashes.update({'artifacts/boot.img': 'stale'}), 'Boot image changed'),
            ('package passed', lambda f: f.documents[PACKAGE].update(passed=False), 'real-package rejection checks required'),
            ('package image', lambda f: f.documents[PACKAGE].update(image_sha256='stale'), 'real-package rejection checks required'),
            ('package script', lambda f: f.hashes.update({'scripts/package-hakios-ios16.py': 'stale'}), 'real-package rejection checks required'),
            ('package test', lambda f: f.hashes.update({'scripts/test-hakios-ios16-package.py': 'stale'}), 'real-package rejection checks required'),
        ]
        for name, corrupt, message in cases:
            for args in (('--offline',), (), ('--boot',)):
                with self.subTest(check=name, args=args):
                    fixture = Fixture()
                    corrupt(fixture)
                    with self.assertRaisesRegex(RuntimeError, message):
                        fixture.invoke(*args)
                    self.assert_before_target(fixture)
                    self.assertEqual(fixture.stdout.getvalue(), '')

    def test_consumed_revision_guard_remains_before_target_in_every_mode(self):
        for args in (('--offline',), (), ('--boot',)):
            with self.subTest(args=args):
                fixture = Fixture()
                fixture.consumed = True
                with self.assertRaisesRegex(RuntimeError, 'already invoked'):
                    fixture.invoke(*args)
                self.assert_before_target(fixture)

    def test_subprocess_tripwire_rejects_accidental_device_path(self):
        fixture = Fixture()
        with self.assertRaisesRegex(AssertionError, 'Real subprocess tripwire'):
            fixture.invoke()
        self.assertEqual(fixture.commands, [])

    def test_default_retains_all_checks_and_no_boot_gate(self):
        fixture = Fixture(simulate_device=True)
        self.assertIsNone(fixture.invoke())
        self.assertEqual([command[1:] for command in fixture.commands], [
            ('devices',), ('-s', 'fixture-serial', 'getvar', 'product'),
            ('-s', 'fixture-serial', 'oem', 'device-info'),
            ('-s', 'fixture-serial', 'getvar', 'partition-size:userdata'),
            ('-s', 'fixture-serial', 'getvar', 'battery-soc-ok'),
            ('-s', 'fixture-serial', 'getvar', 'max-download-size'),
        ])
        self.assertEqual(len(fixture.directories), 1)
        self.assertNotIn(('read', HOST), fixture.events)
        self.assertEqual(fixture.copy_attempts, [])
        report = fixture.writes[-1][1]
        self.assertTrue(report['identity_verified'])
        self.assertFalse(report['temporary_boot_issued'])
        self.assertIn('No boot issued.', fixture.stdout.getvalue())

    def test_disconnected_or_wrong_serial_never_reaches_identity_or_boot(self):
        for devices in ('', 'other-serial fastboot\n', 'fixture-serial adb\n'):
            for args in ((), ('--boot',)):
                with self.subTest(devices=devices, args=args):
                    fixture = Fixture(simulate_device=True)
                    fixture.responses[('devices',)] = (0, devices, '')
                    fixture.invoke(*args)
                    self.assertEqual(len(fixture.commands), 1)
                    self.assertEqual(fixture.writes[-1][1]['outcome'], 'waiting_fastboot')
                    self.assertFalse(fixture.writes[-1][1]['temporary_boot_issued'])
                    self.assertNotIn(('read', HOST), fixture.events)
                    self.assertEqual(fixture.copy_attempts, [])

    def test_identity_unlock_capacity_battery_and_window_fail_closed(self):
        cases = [
            (('getvar', 'product'), (0, '', 'product: other'), 'product differs', 2),
            (('getvar', 'product'), (1, '', 'product: fixture-product'), 'query failed', 2),
            (('getvar', 'product'), (0, '', ''), 'query failed', 2),
            (('oem', 'device-info'), (0, '', 'Device unlocked: false'), 'not unlocked', 3),
            (('oem', 'device-info'), (1, '', 'Device unlocked: true'), 'not unlocked', 3),
            (('getvar', 'partition-size:userdata'), (0, '', 'partition-size:userdata: 2000'), 'capacity changed', 4),
            (('getvar', 'battery-soc-ok'), (0, '', 'battery-soc-ok: no'), 'Battery not ready', 5),
            (('getvar', 'max-download-size'), (0, '', 'max-download-size: 100'), 'download window', 6),
        ]
        for query, response, message, count in cases:
            for args in ((), ('--boot',)):
                with self.subTest(query=query, response=response, args=args):
                    fixture = Fixture(simulate_device=True)
                    fixture.responses[query] = response
                    with self.assertRaisesRegex(RuntimeError, message):
                        fixture.invoke(*args)
                    self.assertEqual(len(fixture.commands), count)
                    self.assertNotIn(('read', HOST), fixture.events)
                    self.assertEqual(fixture.copy_attempts, [])
                    self.assertFalse(any(path == GUARD for path, value in fixture.writes))

    def test_boot_still_requires_decoder_checks_and_usb_environment(self):
        for failure in ('decoder passed', 'decoder source', 'USB environment'):
            with self.subTest(failure=failure):
                fixture = Fixture(simulate_device=True)
                if failure == 'decoder passed':
                    fixture.documents[HOST]['passed'] = False
                elif failure == 'decoder source':
                    fixture.hashes['scripts/capture-hakios-usb.py'] = 'stale'
                message = 'USB host environment missing' if failure == 'USB environment' else 'USB decoder checks required'
                with self.assertRaisesRegex(RuntimeError, message):
                    fixture.invoke('--boot')
                self.assertEqual(len(fixture.commands), 6)
                self.assertEqual(fixture.copy_attempts, [])
                self.assertFalse(fixture.writes[-1][1]['temporary_boot_issued'])

    def test_boot_reaches_copy_only_after_all_simulated_guards(self):
        fixture = Fixture(simulate_device=True)
        fixture.usb_available = True
        with self.assertRaises(CopyBoundary):
            fixture.invoke('--boot')
        self.assertEqual(len(fixture.commands), 6)
        self.assertIn(('read', HOST), fixture.events)
        self.assertEqual(len(fixture.copy_attempts), 1)
        self.assertFalse(fixture.writes[-1][1]['temporary_boot_issued'])
        self.assertFalse(any(path == GUARD for path, value in fixture.writes))


if __name__ == '__main__':
    unittest.main(verbosity=2)
