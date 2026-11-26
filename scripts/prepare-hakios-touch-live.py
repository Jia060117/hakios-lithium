#!/usr/bin/env python3
"""Add the live touch producer to the existing HakiOS build, host only."""
import difflib
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
PATCH = ROOT/'ports/hakios-lithium/patches/0024-lithium-touch-live.patch'


def main():
    if not PATCH.exists():
        name = 'xnu/files/osfmk/arm64/loader_return.c'
        before = (WORK/name).read_text()
        old = '\t\tlithium_lsdb_finalize();\n'
        assert before.count(old) == 1
        after = before.replace(old, '\t\textern void hakios_lithium_touch_stop(void);\n'
            '\t\thakios_lithium_touch_stop();\n'+old)
        chunks = [''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
            fromfile='a/'+name, tofile='b/'+name))]
        name = 'userland/src/init.c'
        before = (WORK/name).read_text()
        old = ('\t*reason = "touch_stage_requires_exact_1";\n'
            '\tif (lithium_touch_diag_arg(args, "touch_stage", &value, &length) != 1 ||\n'
            '\t    !lithium_touch_diag_is(value, length, "1")) {\n\t\treturn 1;\n\t}\n')
        assert before.count(old) == 1
        new = ('\t*reason = "touch_stage_or_live_required";\n'
            '\tunsigned stages = lithium_touch_diag_arg(args, "touch_stage", &value, &length);\n'
            '\tint stage_one = stages == 1 && lithium_touch_diag_is(value, length, "1");\n'
            '\tunsigned lives = lithium_touch_diag_arg(args, "touch", &value, &length);\n'
            '\tint live_one = lives == 1 && lithium_touch_diag_is(value, length, "1");\n'
            '\tif (!((stage_one && lives == 0) || (live_one && stages == 0))) {\n'
            '\t\treturn 1;\n\t}\n')
        after = before.replace(old, new)
        chunks.append(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
            fromfile='a/'+name, tofile='b/'+name)))
        PATCH.write_text(''.join(chunks))
    spec = hakios.read(hakios.MANIFEST)
    hakios.prepare(spec, hakios.policy(spec))
    print('Live touch sources synchronized; existing frozen images retained.')


if __name__ == '__main__':
    main()
