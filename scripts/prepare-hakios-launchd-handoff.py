"""Prepare bounded R27 descriptor/exec log retention on the complete HakiOS init."""
import difflib
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    prior=hakios.read(hakios.ROOT/'reports/hakios-device-r26.json')
    assert not prior['user_screen_observation']['observation_pending']
    base=hakios.ROOT/'artifacts/hakios/p2-ios14-launchd-stdio-ready/sources/upstream'
    patch=hakios.ROOT/spec['port_path']/'patches/0014-lithium-launchd-handoff.patch'
    assert not (hakios.ROOT/'reports/hakios-p2-ios14-launchd-handoff-ready.json').exists(), 'Do not overwrite frozen sources'
    diffs=[]
    old=(base/'xnuloader/loader.c').read_text()
    anchor='"LITHIUM P2 LAUNCHSTDIO ",'
    assert old.count(anchor)==1
    new=old.replace(anchor,anchor+' "LITHIUM P2 LAUNCHFD step=open ", "LITHIUM P2 LAUNCHFD step=dup0 ", '
        '"LITHIUM P2 LAUNCHFD step=dup1 ", "LITHIUM P2 LAUNCHFD step=close ", '
        '"LITHIUM P2 LAUNCHFD step=tty ", "LITHIUM P2 LAUNCHEXEC ",',1)
    diffs+=difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/xnuloader/loader.c',tofile='b/xnuloader/loader.c')
    old=(base/'userland/src/init.c').read_text()
    anchor='\t\texecve(argv[1], argv + 1, envp);\n\t\tprintf("%s: %s\\n", argv[1], strerror(errno));'
    assert old.count(anchor)==1
    new=old.replace(anchor,'''#if defined(HAKIOS_LITHIUM_LAUNCHD)
		int lithium_exec = flash_root_mode && getpid() == 1 && strcmp(argv[1], "/sbin/launchd") == 0;
		if (lithium_exec) dprintf(2, "LITHIUM P2 LAUNCHEXEC pid=1 attempted=1 path=/sbin/launchd\\n");
#endif
		execve(argv[1], argv + 1, envp);
#if defined(HAKIOS_LITHIUM_LAUNCHD)
		if (lithium_exec) {
			int err = errno;
			dprintf(2, "LITHIUM P2 LAUNCHEXEC failed=1 rc=-1 errno=%d\\n", err);
			errno = err;
			return;
		}
#endif
		printf("%s: %s\\n", argv[1], strerror(errno));''',1)
    diffs+=difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/userland/src/init.c',tofile='b/userland/src/init.c')
    patch.write_text(''.join(diffs),newline='\n')
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),
        device_test_only=True,kernel_policies_changed=False,apple_binaries_modified=False,
        generic_services_replaced=False,upstream_rc_exec_preserved=True,
        descriptor_steps=['open','dup0','dup1','close','tty'],exec_attempt_and_failure_retained=True,
        actual_target_mmioctl_errno=19,nonterminal_errno_allowed=[19,25],hardware_verified=False))
    print('Prepared R27 per-step descriptor and original exec diagnostics')

if __name__=='__main__':main()
