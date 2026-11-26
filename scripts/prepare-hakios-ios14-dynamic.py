#!/usr/bin/env python3
"""Integrate the reviewed legacy ABI and a bounded full-init 18H17 test branch."""
import difflib
import hashlib
import subprocess
import hakios

def delta(name, old, new):
    return ''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/'+name, tofile='b/'+name))

def once(text, old, new):
    hakios.require(text.count(old) == 1, 'Integration anchor differs: '+old[:80])
    return text.replace(old,new,1)

def main():
    spec=hakios.read(hakios.MANIFEST); hakios.policy(spec)
    # Explicitly verify the previously recorded workspace before changing inputs.
    work=hakios.workspace(spec)
    marker=hakios.read(work/'ios-mix1-workspace.json')
    hakios.check_common(work,marker)
    deps=hakios.read(hakios.ROOT/'reports/hakios-dependencies.json')
    hakios.require(deps['kernel_source_fingerprint']==hakios.kernel_source_fingerprint(work),
        'Actual complete kernel tree has unrecorded changes')
    draft=hakios.ROOT/'reports/local/ios14-shared-region-438-draft'
    reviewed=hakios.read(hakios.ROOT/'reports/hakios-ios14-shared-region-438-draft.json')
    hakios.require(hakios.sha(draft/'kernel-compatibility.patch')==reviewed['kernel_patch_sha256'] and
        hakios.sha(draft/'shared-region-438.c')==reviewed['adapter_sha256'], 'Reviewed ABI draft differs')
    port=hakios.ROOT/spec['port_path']; target=port/'compatibility/ios14-shared-region.patch'
    hakios.require(not target.exists(), 'Do not overwrite the integrated compatibility patch')
    target.parent.mkdir(parents=True,exist_ok=True)
    original=(draft/'shared-region-438.c').read_text()
    adapter=once(original,'    error = _shared_region_map_and_slide(p, 1, &file, count, maps);',
        '''    error = _shared_region_map_and_slide(p, 1, &file, count, maps);
    {
        char line[128];
        extern void loader_log_line(const char *line);
        snprintf(line, sizeof(line), "LITHIUM P2 SR438 count=%u slide=%u rc=%d\\n",
            count, uap->slide, error);
        loader_log_line(line);
    }''')
    source=work/'xnu-src/xnu-8796.121.2'
    before=(source/'bsd/vm/vm_unix.c').read_text()
    master=(source/'bsd/kern/syscalls.master').read_text()
    new=once(before,'\nint\nshared_region_map_and_slide_2_np(',adapter+'\nint\nshared_region_map_and_slide_2_np(')
    old_table='438\tAUE_NULL\tALL\t{ int nosys(void); } { old shared_region_map_and_slide_np }'
    new_table='438\tAUE_NULL\tALL\t{ int shared_region_map_and_slide_np(int fd, uint32_t count, const struct shared_file_mapping_np *mappings, uint32_t slide, uint64_t* slide_start, uint32_t slide_size) NO_SYSCALL_STUB; }'
    patch=delta('bsd/vm/vm_unix.c',before,new)+delta('bsd/kern/syscalls.master',master,once(master,old_table,new_table))
    target.write_text(patch,encoding='utf-8',newline='\n')
    subprocess.run(['git','apply','--check',str(target)],cwd=source,check=True)
    (port/'compatibility/shared-region-438.c').write_text(adapter,encoding='utf-8',newline='\n')
    # Run the same reviewed boundary contract against the actual integrated adapter.
    harness=once((draft/'host-contract.c').read_text(),original,adapter)
    harness=once(harness,adapter,'void loader_log_line(const char *line) { (void)line; }\n'+adapter)
    (port/'tests/shared-region-438-host.c').write_text(harness,encoding='utf-8',newline='\n')
    outer=''
    name='userland/src/init.c'; old=(work/name).read_text()
    new=once(old,'int\nmain(int argc, char **argv, char **envp)',
        '#if defined(HAKIOS_LITHIUM)\n#include "../../devices/lithium/tests/ios14_dynamic.h"\n#endif\n\nint\nmain(int argc, char **argv, char **envp)')
    new=once(new,'\t{\n\t\tconst char *dev = boot_arg("root_apfs");',
        '#if defined(HAKIOS_LITHIUM)\n\tif (boot_arg("lithium_ios14_test") != NULL) return lithium_ios14_dynamic();\n#endif\n\t{\n\t\tconst char *dev = boot_arg("root_apfs");')
    outer+=delta(name,old,new)
    name='xnuloader/loader.c';old=(work/name).read_text()
    new=once(old,'\tstatic const char *p2[] = {"LITHIUM P2 START ",',
        '\tstatic const char *dynamic[] = {"LITHIUM P2 DYNSTART ", "LITHIUM P2 DYNROOT ",\n'
        '\t    "LITHIUM P2 DYNCACHE ", "LITHIUM P2 SR438 ", "LITHIUM P2 DYNEXEC ",\n'
        '\t    "LITHIUM P2 DYNRUN DF ", "LITHIUM P2 DYNRUN MOUNT ", "LITHIUM P2 DYNRESULT "};\n'
        '\tfor (unsigned i=0; i<sizeof(dynamic)/sizeof(dynamic[0]); i++)\n'
        '\t    lithium_show_line(lithium_last_line(log, bytes, dynamic[i]), log + bytes);\n'
        '\tstatic const char *p2[] = {"LITHIUM P2 START ",')
    outer+=delta(name,old,new)
    name='xnu/files/osfmk/arm64/user_exception_trace.c';old=(work/name).read_text()
    new=once(old,'if (proc != NULL && proc_pid(proc) == 1) {','if (proc != NULL && proc_pid(proc) > 0) {')
    new=once(new,'"LITHIUM P2 FAULT pid=1 exc=%d code=%llx sub=%llx\\n",\n\t\t    exception,',
        '"LITHIUM P2 FAULT pid=%d exc=%d code=%llx sub=%llx\\n",\n\t\t    proc_pid(proc), exception,')
    outer+=delta(name,old,new)
    path=port/'patches/0007-lithium-ios14-dynamic.patch'
    hakios.require(not path.exists(),'Do not overwrite reviewed full-init branch')
    path.write_text(outer,encoding='utf-8',newline='\n')
    hakios.write(path.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(path),
        scope='Full upstream init hook for HFSX/18H17 dynamic process test, retained summary and child exception telemetry',
        generic_services_replaced=False,hardware_verified=False))
    hakios.write(target.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(target),
        adapter_sha256=hakios.sha(port/'compatibility/shared-region-438.c'),
        before_files={'bsd/vm/vm_unix.c':hashlib.sha256(before.encode()).hexdigest(),
            'bsd/kern/syscalls.master':hashlib.sha256(master.encode()).hexdigest()},
        reviewed_draft_sha256=reviewed['kernel_patch_sha256'],requires_define='LITHIUM_ARMV8_0',
        shared_region_core='_shared_region_map_and_slide',hardware_verified=False))
    spec['test_profile']='p2-ios14-dynamic'
    hakios.write(hakios.MANIFEST,spec)
    state=hakios.read(hakios.ROOT/'project.json')
    state['current_device_status']='User confirmed Fastboot for original 18H17 dynamic test; host USB observed authorized target'
    state['device_state'].update(current_physical_state='Fastboot_USB_observed',
        current_physical_state_source='user_confirmation_and_host_fastboot_devices')
    state['stage']='P2: original 18H17 dynamic program integration after verified read-only storage'
    state['next_test_ready_report']=None
    state['active_kernel']['latest_kernel_binary_hardware_verified']=False
    state['work_status']['latest_compatibility_draft'].update(integrated=True,new_kernel_compiled=False,hardware_verified=False)
    hakios.write(hakios.ROOT/'project.json',state)
    print('Integrated bounded full-init HFSX branch and legacy parameter adapter; no build or boot yet')

if __name__=='__main__':main()
