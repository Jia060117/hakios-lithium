#!/usr/bin/env python3
"""Prepare bounded HFSX/18H17 integration of complete HakiOS init and loader."""
import difflib
from pathlib import Path
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST); hakios.policy(spec)
    passed=hakios.read(hakios.ROOT/'reports/hakios-p2-dynamic-closure.json')
    hakios.require(passed['dynamic_tools_passed'] and not passed['P2_overall_passed'], 'R24 dynamic closure required')
    work=hakios.verify_workspace(spec)
    patch=hakios.ROOT/spec['port_path']/'patches/0012-lithium-ios14-launchd.patch'
    hakios.require(not patch.exists(), 'Launchd patch already exists; do not overwrite')
    changes=[]
    for name in ('userland/src/init.c','xnuloader/loader.c'):
        old=(work/name).read_text()
        if name.endswith('init.c'):
            new=old.replace('#include "../../devices/lithium/tests/ios14_dynamic.h"',
                '#include "../../devices/lithium/tests/ios14_dynamic.h"\n#if defined(HAKIOS_LITHIUM_LAUNCHD)\n#include "../../devices/lithium/tests/ios14_launchd.h"\n#endif',1)
            new=new.replace('\tif (getpid() != 1) {',
                '\tif (getpid() != 1) {\n#if defined(HAKIOS_LITHIUM_LAUNCHD)\n'
                '\t\tif (argc == 2 && strcmp(argv[1], "--lithium-launchd-observer") == 0)\n'
                '\t\t\treturn lithium_ios14_launchd_observer();\n#endif',1)
            new=new.replace('\tif (boot_arg("lithium_ios14_test") != NULL) return lithium_ios14_dynamic();',
                '#if defined(HAKIOS_LITHIUM_LAUNCHD)\n'
                '\tif (boot_arg("lithium_launchd_test") != NULL) return lithium_ios14_launchd();\n#endif\n'
                '\tif (boot_arg("lithium_ios14_test") != NULL) return lithium_ios14_dynamic();',1)
        else:
            anchor='\tstatic const char *dynamic[] = '
            pos=old.index(anchor)
            new=old[:pos]+('\tstatic const char *launch[] = {"LITHIUM P2 LAUNCHSTART ", "LITHIUM P2 LAUNCHROOT ",\n'
                '\t    "LITHIUM P2 DYNFILE LAUNCHD ", "LITHIUM P2 LAUNCHMOUNTS ", "LITHIUM P2 LAUNCHHANDOFF ",\n'
                '\t    "LITHIUM P2 LAUNCHJOB ", "LITHIUM P2 LAUNCHPROC PID1 ",\n'
                '\t    "LITHIUM P2 LAUNCHPROC NOTIFYD ", "LITHIUM P2 LAUNCHPROC CFPREFSD ", "LITHIUM P2 LAUNCHRESULT "};\n'
                '\tfor (unsigned i=0; i<sizeof(launch)/sizeof(launch[0]); i++)\n'
                '\t    lithium_show_line(lithium_last_line(log, bytes, launch[i]), log + bytes);\n')+old[pos:]
        hakios.require(new!=old,'Source anchor missing: '+name)
        changes+=list(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
            fromfile='a/'+name,tofile='b/'+name))
    patch.write_text(''.join(changes),encoding='utf-8',newline='\n')
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),
        upstream_init_handoff_and_mounts_reused=True, generic_services_replaced=False,
        root_physical_readonly=True,data_ramfs_uses_upstream=True,hardware_verified=False))
    spec['test_profile']='p2-ios14-launchd'
    hakios.write(hakios.MANIFEST,spec)
    print('Prepared original launchd PID1 branch; HFSX + upstream bindfs/ramfs/rc; no device operation')

if __name__=='__main__': main()
