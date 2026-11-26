#!/usr/bin/env python3
"""Prepare 18H17 boot-descriptor repair and retention of existing HakiOS diagnostics."""
import difflib
import hakios


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    prior = hakios.read(hakios.ROOT / 'reports/hakios-device-r25.json')
    assert prior['runtime_acceptance']['panic_return_observed']
    base = hakios.ROOT / 'artifacts/hakios/p2-ios14-launchd-ready/sources/upstream/xnuloader/loader.c'
    old = base.read_text()
    patch = hakios.ROOT / spec['port_path'] / 'patches/0013-lithium-launchd-stdio-summary.patch'
    assert not patch.exists(), 'Do not overwrite prepared/frozen sources'
    helper = r'''
static void
lithium_show_launchd_diagnostics(const char *log, uint64_t bytes)
{
	/* Read existing HakiOS logs only. No new abort or syscall implementation. */
	const char *lines[3];
	unsigned count = 0;
	uint64_t remaining = bytes;
	while (count < 3) {
		const char *line = lithium_last_line(log, remaining, "systrace: launchd[");
		if (line == NULL) break;
		const char *end = line;
		while (end < log + remaining && *end != '\n') end++;
		if (end < log + remaining) lines[count++] = line;
		remaining = (uint64_t)(line - log);
	}
	while (count != 0) lithium_show_line(lines[--count], log + bytes);
	lithium_show_line(lithium_last_line(log, bytes, "userabort: launchd["), log + bytes);
	/* launchd's own messages need not have a fixed prefix. Keep the last
	 * complete matching line, with bounds independent of NUL termination. */
	static const char *errors[] = {"cannot be run directly", "boot task failure:",
	    "Boot task failed:", "Panicking in 3 seconds", "could not", "Could not"};
	const char *last = NULL;
	for (uint64_t start = 0; start < bytes;) {
		uint64_t end = start;
		while (end < bytes && log[end] != '\n') end++;
		if (end == bytes) break;
		for (unsigned k = 0; k < sizeof(errors)/sizeof(errors[0]); k++) {
			uint64_t width = str_len(errors[k]);
			for (uint64_t at = start; at + width <= end; at++) {
				if (memcmp(log + at, errors[k], width) == 0) { last = log + start; break; }
			}
		}
		start = end + 1;
	}
	lithium_show_line(last, log + bytes);
}
'''
    anchor = 'static void\nlithium_return_screen('
    assert old.count(anchor) == 1
    new = old.replace(anchor, helper.lstrip() + '\n' + anchor, 1)
    new = new.replace('"LITHIUM P2 LAUNCHHANDOFF ",',
        '"LITHIUM P2 LAUNCHHANDOFF ", "LITHIUM P2 LAUNCHBOOT ", "LITHIUM P2 LAUNCHSTDIO ",', 1)
    new = new.replace('\tlithium_show_line(panic_line, log + bytes);',
        '\tlithium_show_line(panic_line, log + bytes);\n\tlithium_show_launchd_diagnostics(log, bytes);', 1)
    patch.write_text(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/xnuloader/loader.c', tofile='b/xnuloader/loader.c')), newline='\n')
    details = dict(baseline=hakios.PIN, patch_sha256=hakios.sha(patch),
        generic_services_replaced=False, apple_binaries_modified=False, kernel_policies_changed=False,
        existing_upstream_diagnostics=['applog=1 applog_name=launchd', 'user_abort_trace', 'systrace'],
        stdout_nonterminal_contract=True, stderr_console_before_exec=True, rd_md0_checked=True,
        hardware_verified=False)
    hakios.write(patch.with_suffix('.json'), details)
    details.update(original_launchd_sha256=prior['package']['dynamic_test_build']['original_programs']['sbin/launchd']['sha256']
        if 'dynamic_test_build' in prior['package'] else hakios.read(hakios.ROOT/'reports/hakios-ios14-dynamic-build.json')['original_programs']['sbin/launchd']['sha256'],
        original_disassembly=dict(isatty_stdout='0x10001079c-0x1000107c0',
            direct_run_error='0x1000107ec', exit_decimal78='0x1000107fc-0x100010800',
            rd_md0_test='0x10003751c-0x100037574'),
        loader_adds_rd_md0_automatically=True, suspected_cause_not_hardware_confirmed=True,
        exit_status_decimal=78, exit_status_hex='0x4e',
        exit_status_decode='kern_exit.c panic uses %d and WEXITSTATUS(rv); signal field is separately 6',
        independent_partition_changes_required=False)
    hakios.write(hakios.ROOT/'reports/hakios-launchd-stdio-preparation.json', details)
    print('Prepared boot-descriptor contract and bounded existing-log retention; no device operation')


if __name__ == '__main__':
    main()
