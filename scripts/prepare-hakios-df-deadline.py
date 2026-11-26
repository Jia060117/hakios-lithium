#!/usr/bin/env python3
"""Prepare bounded DF budget and retain existing upstream syscall traces."""
import difflib
import hakios

def once(text, old, new):
    hakios.require(text.count(old) == 1, 'DF integration anchor differs: ' + old[:100])
    return text.replace(old, new, 1)

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    deps = hakios.read(hakios.ROOT / 'reports/hakios-dependencies.json')
    hakios.require(deps['kernel_source_fingerprint'] == hakios.kernel_source_fingerprint(work),
        'Actual complete kernel tree has unrecorded changes')
    prior = hakios.read(hakios.ROOT / 'reports/hakios-device-r23.json')
    hakios.require(prior['runtime_acceptance']['original_mount_exit0_verified'] and
        prior['failure']['parent_time_limit_and_successful_kill_observed'], 'R23 observation required')
    port = hakios.ROOT / spec['port_path']
    patch = port / 'patches/0011-lithium-df-trace-summary.patch'
    hakios.require(not patch.exists(), 'Do not overwrite an integrated DF summary patch')
    target = port / 'tests/ios14_dynamic.h'
    text = target.read_text()
    text = once(text, 'lithium_run_original(const char *label, char **argv)',
        'lithium_run_original(const char *label, char **argv, int timeout_seconds)')
    text = once(text, '    pid_t pid = fork(), result = 0;',
        '    printf("LITHIUM P2 DYNBUDGET %s seconds=%d\\n", label, timeout_seconds);\n'
        '    pid_t pid = fork(), result = 0;')
    text = once(text, 'for (int i = 0; i < 300; i++)',
        'for (int i = 0; i < timeout_seconds * 10; i++)')
    text = once(text, '>= 30000000LL)', '>= timeout_seconds * 1000000LL)')
    text = once(text, 'lithium_run_original("DF", df)', 'lithium_run_original("DF", df, 60)')
    text = once(text, 'lithium_run_original("MOUNT", mnt)', 'lithium_run_original("MOUNT", mnt, 30)')
    name = 'xnuloader/loader.c'
    old = (work / name).read_text()
    helper = '''static void
lithium_show_df_trace(const char *log, uint64_t bytes)
{
	/* Reuse HakiOS systrace; retain at most three completed BSD calls from df. */
	const char *lines[3];
	unsigned count = 0;
	uint64_t remaining = bytes;
	while (count < 3) {
		const char *line = lithium_last_line(log, remaining, "systrace: df[");
		if (line == NULL) break;
		const char *end = line;
		while (end < log + remaining && *end != '\\n') end++;
		if (end < log + remaining) lines[count++] = line;
		remaining = (uint64_t)(line - log);
	}
	while (count != 0) lithium_show_line(lines[--count], log + bytes);
}

'''
    new = once(old, 'static void\nlithium_return_screen(', helper + 'static void\nlithium_return_screen(')
    new = once(new, '\tstatic const char *dynamic[] = {"LITHIUM P2 DYNSTART ",',
        '\tstatic const char *dynamic[] = {"LITHIUM P2 DYNBUDGET DF ", "LITHIUM P2 DYNBUDGET MOUNT ", "LITHIUM P2 DYNSTART ",')
    new = once(new, '\tstatic const char *p2[] = {"LITHIUM P2 START ",',
        '\tlithium_show_df_trace(log, bytes);\n\tstatic const char *p2[] = {"LITHIUM P2 START ",')
    # The three-line DF helper replaces the old single generic systrace line.
    new = once(new, '"exectrace: ", "systrace: "', '"exectrace: "')
    delta = ''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/' + name, tofile='b/' + name))
    target.write_text(text, encoding='utf-8', newline='\n')
    patch.write_text(delta, encoding='utf-8', newline='\n')
    hakios.write(patch.with_suffix('.json'), dict(baseline=hakios.PIN, patch_sha256=hakios.sha(patch),
        scope='Retain budgets and last three existing HakiOS df BSD syscall trace lines',
        df_seconds=60, mount_seconds=30, kernel_return_seconds=120,
        generic_services_replaced=False, kernel_policy_changed=False, hardware_verified=False))
    hakios.write(hakios.ROOT / 'reports/hakios-df-deadline-preparation.json', dict(
        parent_observation='reports/hakios-device-r23.json', inputs=hakios.inputs(spec),
        df_seconds=60, mount_seconds=30, kernel_return_seconds=120,
        cleanup_max_seconds_each=2, child_budgets_and_cleanup_max_seconds=94,
        upstream_trace_boot_args='systrace=2 systrace_name=df', retained_df_calls=3,
        kernel_compatibility_changes=False, original_programs_modified=False,
        generic_services_replaced=False, hardware_verified=False, temporary_boot_issued=False))
    print('Prepared DF60/MOUNT30 budgets and three upstream trace lines; kernel policy unchanged; no boot')

if __name__ == '__main__':
    main()
