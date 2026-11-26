#!/usr/bin/env python3
"""Regression checks for route isolation. No retired script is executed."""
from copy import deepcopy
from pathlib import Path
import hashlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import hakios

class RouteIsolation(unittest.TestCase):
    def setUp(self):
        self.spec = hakios.read(hakios.MANIFEST)

    def test_other_baseline_rejected_before_git_or_execution(self):
        self.spec['baseline']['project'] = 'UniXNU'
        with patch.object(hakios, 'git') as git, patch.object(hakios.subprocess, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'Only the approved'):
                hakios.policy(self.spec)
            git.assert_not_called()
            run.assert_not_called()

    def test_pin_change_rejected_before_git(self):
        self.spec['baseline']['commit'] = '0' * 40
        with patch.object(hakios, 'git') as git:
            with self.assertRaises(RuntimeError):
                hakios.policy(self.spec)
            git.assert_not_called()

    def test_userspace_change_rejected(self):
        self.spec['target_userspace']['version'] = '16.5'
        with patch.object(hakios, 'git') as git:
            with self.assertRaisesRegex(RuntimeError, 'userspace target'):
                hakios.policy(self.spec)
            git.assert_not_called()

    def test_p0_rejects_build_without_launching_process(self):
        self.spec['readiness']['dependency_closure_verified'] = False
        with patch.object(hakios.subprocess, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'P0/loading gates unresolved'):
                hakios.build_kernel(self.spec)
            run.assert_not_called()

    def test_check_does_not_inherit_old_runtime_success(self):
        state = hakios.read(hakios.ROOT / 'project.json')
        historical = hakios.read(hakios.HISTORY / 'project.json')
        self.assertTrue(historical['latest_native_attempt']['launchd_started'])
        # Hardware success must come from the new baseline's own device evidence.
        # R7 has now executed HakiOS; the archived launchd flag cannot supply it.
        observations = [hakios.read(path) for path in (hakios.ROOT / 'reports').glob('hakios-device-r[0-9]*.json')]
        verified = [item for item in observations if item['baseline']['project'] == 'HakiOS'
            and item['baseline']['commit'] == hakios.PIN and item['verified_stages']['kernel']
            and item.get('screen_evidence', {}).get('photos')]
        self.assertEqual(state['active_kernel']['hardware_started'], bool(verified))
        self.assertFalse(state['active_kernel']['ios14_compatibility_verified'])
        self.assertNotIn('latest_native_attempt', state)
        self.assertTrue(state['device_state']['userdata_contains_ios14_hfsx'])

    def test_frozen_sources_and_inert_legacy_stubs(self):
        result = hakios.frozen_check()
        self.assertEqual(result['frozen_reference_files_checked'], 98)
        self.assertEqual(result['legacy_archives_checked'], 75)
        self.assertEqual(result['archived_documents_checked'], 6)

    def test_prepare_preserves_unrecorded_device_edit(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / '.git').mkdir()
            edited = work / 'devices/lithium/device.conf'
            edited.parent.mkdir(parents=True)
            edited.write_text('unrecorded edit', encoding='utf-8')
            before = hakios.file_map(work)
            with patch.object(hakios, 'resource_check'), patch.object(hakios, 'workspace', return_value=work), \
                 patch.object(hakios, 'git', return_value=hakios.PIN), patch.object(hakios, 'common_diff', return_value=b''), \
                 patch.object(hakios.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'overwrite an edited device file'):
                    hakios.prepare(deepcopy(self.spec), hakios.ROOT / self.spec['baseline']['path'])
                run.assert_not_called()
            self.assertEqual(hakios.file_map(work), before)

    def test_stale_workspace_inputs_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / '.git').mkdir()
            stale = hakios.inputs(self.spec)
            stale['manifest_sha256'] = '0' * 64
            hakios.write(work / 'ios-mix1-workspace.json', {'inputs': stale})
            with patch.object(hakios, 'workspace', return_value=work), patch.object(hakios, 'git', return_value=hakios.PIN), \
                 patch.object(hakios, 'common_diff', return_value=b''):
                with self.assertRaisesRegex(RuntimeError, 'Device layer changed'):
                    hakios.verify_workspace(self.spec)

    def test_unrecorded_common_edit_rejected_before_apply(self):
        with patch.object(hakios, 'git', return_value=hakios.PIN), \
             patch.object(hakios, 'common_diff', return_value=b'unrecorded change'), \
             patch.object(hakios.subprocess, 'run') as run:
            with self.assertRaisesRegex(RuntimeError, 'unrecorded edits'):
                hakios.sync_common(Path('unused'), self.spec, {})
            run.assert_not_called()

    def test_dependent_patch_stack_and_failed_update_restore_prior_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp) / 'work'
            work.mkdir()
            def git(*args):
                return subprocess.check_output(['git', '-C', str(work), *args], stderr=subprocess.STDOUT)
            git('init', '-q')
            source = work / 'sample.c'
            source.write_text('begin\nvalue0\nend\n', encoding='utf-8', newline='\n')
            git('add', 'sample.c')
            git('-c', 'user.name=Workflow Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')
            pin = git('rev-parse', 'HEAD').decode().strip()
            patches = []
            for i, (before, after) in enumerate((('value0', 'value1'), ('value1', 'value2'), ('absent', 'value3'))):
                path = Path(tmp) / (str(i) + '.patch')
                path.write_text('--- a/sample.c\n+++ b/sample.c\n@@ -1,3 +1,3 @@\n begin\n-' + before + '\n+' + after + '\n end\n', encoding='utf-8', newline='\n')
                patches.append(path)
            with patch.object(hakios, 'PIN', pin), patch.object(hakios, 'ROOT', Path(tmp)), patch.object(hakios, 'common_patches', return_value=patches[:2]):
                marker = {'common_diff_sha256': hashlib.sha256(b'').hexdigest()}
                result = hakios.sync_common(work, self.spec, marker)
                self.assertEqual(source.read_text(encoding='utf-8'), 'begin\nvalue2\nend\n')
            prior = source.read_bytes()
            with patch.object(hakios, 'PIN', pin), patch.object(hakios, 'ROOT', Path(tmp)), patch.object(hakios, 'common_patches', return_value=patches):
                with self.assertRaises(subprocess.CalledProcessError):
                    hakios.sync_common(work, self.spec, result)
            self.assertEqual(source.read_bytes(), prior)

if __name__ == '__main__':
    unittest.main()
