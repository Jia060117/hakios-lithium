"""Prepare narrowly gated18H17 legacy Mach messaging on complete HakiOS8796."""
import difflib
from pathlib import Path
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    work=hakios.workspace(spec);tree=work/'xnu-src/xnu-8796.121.2';port=hakios.ROOT/spec['port_path']
    r=hakios.read(hakios.ROOT/'reports/hakios-device-r22.json')
    hakios.require(r['failure']['guard_name']=='kGUARD_EXC_INVALID_OPTIONS' and r['failure']['parent_kill_excluded'], 'R22 evidence required')
    hakios.require(hakios.read(hakios.ROOT/'reports/hakios-dependencies.json')['kernel_source_fingerprint']==hakios.kernel_source_fingerprint(work), 'Actual source changed')
    name='osfmk/ipc/mach_msg.c';path=tree/name;before=path.read_text();after=before
    # The original binary LC_BUILD_VERSION inputs are all exactly iOS14.8.
    original=hakios.read(hakios.ROOT/'reports/hakios-ios14-dynamic-build.json')['original_programs']
    hakios.require(all(p['build_version']['platform']==2 and p['build_version']['sdk']=='0xe0800' for p in original.values()), 'Original ABI identity differs')
    anchor='#if XNU_TARGET_OS_OSX || XNU_TARGET_OS_IOS\n#if DEVELOPMENT || DEBUG'
    declaration='''#if XNU_TARGET_OS_OSX || XNU_TARGET_OS_IOS
#if defined(LITHIUM_ARMV8_0)
static TUNABLE(bool, lithium_ios14_legacy_msg, "lithium_ios14_test", false);
#endif
#if DEVELOPMENT || DEBUG'''
    hakios.require(after.count(anchor)==1,'Tunable anchor differs');after=after.replace(anchor,declaration)
    anchor='\ttask_t task = current_task();\n\n#if __x86_64__'
    block='''	task_t task = current_task();

#if defined(LITHIUM_ARMV8_0)
	/* 18H17 libSystem uses mach_msg_overwrite_trap, predating mach_msg2.
	 * Keep all upstream copyin, rights, size, descriptor and send checks.
	 * This exception is limited to the explicit iOS14 test and exact SDK14.8. */
	if (lithium_ios14_legacy_msg && platform == PLATFORM_IOS && sdk == 0x000e0800) {
		if (header->msgh_id == 200) {
			char line[128];
			extern void loader_log_line(const char *line);
			snprintf(line, sizeof(line), "LITHIUM P2 MACH14 platform=%u sdk=%x id=%d legacy=1\\n",
			    platform, sdk, header->msgh_id);
			loader_log_line(line);
		}
		return true;
	}
#endif

#if __x86_64__'''
    hakios.require(after.count(anchor)==1,'Legacy gate anchor differs');after=after.replace(anchor,block)
    patch=port/'compatibility/ios14-mach-msg.patch';hakios.require(not patch.exists(),'Do not repeat this preparation')
    patch.write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)))
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),before_files={name:hakios.sha(path)},
        target_platform=2,target_sdk='0x000e0800',gate='LITHIUM_ARMV8_0 and lithium_ios14_test=1 and PLATFORM_IOS and SDK14.8',
        upstream_IPC_validation_preserved=True,global_legacy_allow_flag_unchanged=True,signature_policy_unchanged=True,
        observed_failure_report='reports/hakios-device-r22.json',hardware_verified=False))
    # Retain the compatibility marker in the existing summary, using a separate common patch.
    name='xnuloader/loader.c';before=(work/name).read_text();anchor='\t    "LITHIUM P2 DYNCS ", "LITHIUM P2 DYNCSFILE ",'
    hakios.require(before.count(anchor)==1,'Loader summary anchor differs')
    after=before.replace(anchor,'\t    "LITHIUM P2 MACH14 ",\n'+anchor)
    patch=port/'patches/0010-lithium-mach14-summary.patch'
    patch.write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)))
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),scope='Retain exact14.8 legacy IPC gate marker'))
    print('Prepared exact14.8 legacy Mach entry compatibility; full upstream IPC validation remains')

if __name__=='__main__':main()
