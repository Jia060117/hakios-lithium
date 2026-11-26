"""Prepare bounded R21 termination diagnostics from the actual complete HakiOS tree."""
import difflib
from pathlib import Path
import hakios

def patch_for(path, before, after):
    return ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='a/'+path, tofile='b/'+path))

def main():
    spec=hakios.read(hakios.MANIFEST); hakios.policy(spec)
    work=hakios.workspace(spec); port=hakios.ROOT/spec['port_path']
    observed=hakios.read(hakios.ROOT/'reports/hakios-device-r21.json')
    hakios.require(observed['failure']['signal']=='SIGKILL' and not observed['failure']['source_identified'],
        'This diagnostic requires the archived R21 evidence')
    tree=work/'xnu-src/xnu-8796.121.2'
    deps=hakios.read(hakios.ROOT/'reports/hakios-dependencies.json')
    hakios.require(deps['kernel_source_fingerprint']==hakios.kernel_source_fingerprint(work), 'Actual source changed')
    pieces=[]; before_files={}
    name='bsd/kern/kern_exit.c'; src=(tree/name).read_text()
    anchor='\tproc_prepareexit(p, rv, perf_notify);'
    block='''#if defined(LITHIUM_ARMV8_0)
	/* Preserve the existing committed reason; do not alter exit or signature policy. */
	int lithium_dynamic = 0;
	if (proc_getpid(p) > 1 && proc_getpid(p) <= 3 &&
	    PE_parse_boot_argn("lithium_ios14_test", &lithium_dynamic, sizeof(lithium_dynamic)) && lithium_dynamic) {
		char line[192];
		extern void loader_log_line(const char *line);
		snprintf(line, sizeof(line), "LITHIUM P2 DYNEXIT pid=%d signal=%d namespace=%u code=%llx csflags=%llx\\n",
		    proc_getpid(p), WTERMSIG(rv), exit_reason ? exit_reason->osr_namespace : 0,
		    exit_reason ? (unsigned long long)exit_reason->osr_code : 0,
		    (unsigned long long)proc_getcsflags(p));
		loader_log_line(line);
	}
#endif

'''
    hakios.require(src.count(anchor)==1, 'Exit anchor differs')
    before_files[name]=hakios.sha(tree/name); pieces.append(patch_for(name,src,src.replace(anchor,block+anchor)))
    name='osfmk/vm/vm_fault.c'; src=(tree/name).read_text()
    anchor='\t\tif (cs_killed) {\n\t\t\tKDBG('
    block='''#if defined(LITHIUM_ARMV8_0)
			int lithium_dynamic = 0;
			if (pid > 1 && pid <= 3 &&
			    PE_parse_boot_argn("lithium_ios14_test", &lithium_dynamic, sizeof(lithium_dynamic)) && lithium_dynamic) {
				char line[256];
				extern void loader_log_line(const char *line);
				snprintf(line, sizeof(line), "LITHIUM P2 DYNCS pid=%d address=%llx offset=%llx validated=%d tainted=%d\\n",
				    pid, (unsigned long long)vaddr, (unsigned long long)file_offset,
				    VMP_CS_VALIDATED(m, fault_page_size, fault_phys_offset),
				    VMP_CS_TAINTED(m, fault_page_size, fault_phys_offset));
				loader_log_line(line);
				snprintf(line, sizeof(line), "LITHIUM P2 DYNCSFILE pid=%d %s\\n", pid, pathname ? pathname : "UNKNOWN");
				loader_log_line(line);
			}
#endif
'''
    hakios.require(src.count(anchor)==1, 'Invalid-page anchor differs')
    before_files[name]=hakios.sha(tree/name)
    changed=src.replace(anchor,'\t\tif (cs_killed) {\n'+block+'\t\t\tKDBG(')
    first_include=changed.index('#include ')
    changed=changed[:first_include]+'#if defined(LITHIUM_ARMV8_0)\n#include <pexpert/pexpert.h>\n#endif\n'+changed[first_include:]
    pieces.append(patch_for(name,src,changed))
    patch=port/'compatibility/ios14-exit-observation.patch'
    hakios.require(not patch.exists(), 'Do not repeat this preparation over reviewed inputs')
    patch.write_text(''.join(pieces))
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),
        before_files=before_files,gate='LITHIUM_ARMV8_0 and lithium_ios14_test=1 and PID2/3',
        policy_unchanged=True,source='Actual HakiOS8796 exit_with_reason and vm_fault_enter',hardware_verified=False))
    name='xnuloader/loader.c'; src=(work/name).read_text()
    anchor='\t    "LITHIUM P2 DYNRUN DF ", "LITHIUM P2 DYNRUN MOUNT ", "LITHIUM P2 DYNRESULT "};'
    replacement='''	    "LITHIUM P2 DYNCS ", "LITHIUM P2 DYNCSFILE ",
	    "LITHIUM P2 DYNEXIT pid=2 ", "LITHIUM P2 DYNEXIT pid=3 ",
	    "LITHIUM P2 DYNWAIT DF ", "LITHIUM P2 DYNWAIT MOUNT ",
	    "LITHIUM P2 DYNTIME DF ", "LITHIUM P2 DYNTIME MOUNT ",
	    "LITHIUM P2 DYNRUN DF ", "LITHIUM P2 DYNRUN MOUNT ", "LITHIUM P2 DYNRESULT "};'''
    hakios.require(src.count(anchor)==1, 'Return screen anchor differs')
    loader_patch=port/'patches/0009-lithium-exit-summary.patch'
    loader_patch.write_text(patch_for(name,src,src.replace(anchor,replacement)))
    hakios.write(loader_patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(loader_patch),
        scope='Retain actual parent cleanup and upstream kernel exit/codesigning evidence; no runtime policy changes'))
    print('Prepared exit-reason/invalid-page observation and retained summary from complete HakiOS sources')

if __name__=='__main__': main()
