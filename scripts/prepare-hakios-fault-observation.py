#!/usr/bin/env python3
"""Retain existing HakiOS PID1 exception state for the MIX storage experiment."""
import difflib
import subprocess
import hakios

def once(text, old, new):
    hakios.require(text.count(old) == 1, 'Fault-observation anchor differs: ' + old[:80])
    return text.replace(old, new, 1)

def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    changes = {}
    path = 'xnu/files/osfmk/arm64/user_exception_trace.c'
    original = subprocess.check_output(['git', '-C', str(source), 'show', hakios.PIN + ':' + path]).decode()
    anchor = '\t\treturn;\n\t}\n\tregion = vm_shared_region_get(task);'
    addition = '''		return;
	}
#if defined(LITHIUM_ARMV8_0)
	/* Reuse this upstream exception hook and return log before any diagnostic copyin. */
	proc = get_bsdtask_info(task);
	if (proc != NULL && proc_pid(proc) == 1) {
		char line[192];
		extern void loader_log_line(const char *line);
		snprintf(line, sizeof(line), "LITHIUM P2 FAULT pid=1 exc=%d code=%llx sub=%llx\\n",
		    exception, count > 0 ? (unsigned long long)code[0] : 0,
		    count > 1 ? (unsigned long long)code[1] : 0);
		loader_log_line(line);
		snprintf(line, sizeof(line), "LITHIUM P2 STATE pc=%llx lr=%llx sp=%llx\\n",
		    (unsigned long long)get_saved_state_pc(state), (unsigned long long)get_saved_state_lr(state),
		    (unsigned long long)get_saved_state_sp(state));
		loader_log_line(line);
		snprintf(line, sizeof(line), "LITHIUM P2 ABORT esr=%x far=%llx\\n",
		    get_saved_state_esr(state), (unsigned long long)get_saved_state_far(state));
		loader_log_line(line);
	}
#endif
	region = vm_shared_region_get(task);'''
    # Both trace and abort helpers have a similar anchor; only the exception helper is changed.
    start = original.index('void\nuser_exception_trace(')
    end = original.index('\n/*\n * A program calls abort()', start)
    modified = original[:start] + once(original[start:end], anchor, addition) + original[end:]
    changes[path] = (original, modified)

    path = 'xnu/files/bsd/miscfs/apfs/apfs_vnops.c'
    original = subprocess.check_output(['git', '-C', str(source), 'show', hakios.PIN + ':' + path]).decode()
    start = original.index('static int\napfs_vnop_pagein(')
    end = original.index('\n/* ------------------------------------------------------------------ directories */', start)
    function = original[start:end]
    # Error-only telemetry; the filesystem's validation, reads and UPL handling are unchanged.
    log = '''#if defined(LITHIUM_ARMV8_0)
		{
			char line[192];
			extern void loader_log_line(const char *line);
			snprintf(line, sizeof(line), "LITHIUM P2 PAGEIN error=%d offset=%llx size=%llx flags=%x\\n",
			    error, (unsigned long long)offset, (unsigned long long)ap->a_size, ap->a_flags);
			loader_log_line(line);
		}
#endif
'''
    function = once(function, '\tif (error) {\n\t\tif (!(ap->a_flags & UPL_NOCOMMIT)) {',
        '\tif (error) {\n' + log + '\t\tif (!(ap->a_flags & UPL_NOCOMMIT)) {')
    function = once(function, '\treturn error;\n}', '\tif (error) {\n' + log + '\t}\n\treturn error;\n}')
    changes[path] = (original, original[:start] + function + original[end:])

    path = 'xnuloader/loader.c'
    original = (work / path).read_text()
    modified = once(original, '\tconst char *panic_line = lithium_last_line(log, bytes, "panic(cpu ");',
        '\tstatic const char *faults[] = {"LITHIUM P2 PAGEIN ", "LITHIUM P2 FAULT ",\n'
        '\t    "LITHIUM P2 STATE ", "LITHIUM P2 ABORT ", "exectrace: ", "systrace: "};\n'
        '\tfor (unsigned i=0; i<sizeof(faults)/sizeof(faults[0]); i++)\n'
        '\t    lithium_show_line(lithium_last_line(log, bytes, faults[i]), log + bytes);\n'
        '\tconst char *panic_line = lithium_last_line(log, bytes, "panic(cpu ");')
    changes[path] = (original, modified)
    patch = hakios.ROOT / spec['port_path'] / 'patches/0005-msm8996-pid1-fault-observation.patch'
    hakios.require(not patch.exists(), 'Do not overwrite a generated reviewed fault patch')
    patch.write_text(''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/' + path, tofile='b/' + path)) for path, (old, new) in changes.items()), encoding='utf-8', newline='\n')
    hakios.write(patch.with_suffix('.json'), dict(baseline=hakios.PIN, patch_sha256=hakios.sha(patch),
        scope='MIX-only PID1 register capture at existing HakiOS user_exception_trace plus APFS error-only telemetry and retained loader summary',
        original_files={path: __import__('hashlib').sha256(old.encode()).hexdigest() for path,(old,new) in changes.items()},
        requires_boot_arg='systrace=1', changes_fault_or_filesystem_behavior=False,
        hardware_verified=False, relates_to='reports/hakios-device-r14.json'))
    print('Generated bounded MIX PID1 fault observation patch')

if __name__ == '__main__': main()
