#!/usr/bin/env python3
"""Prepare R28 retained observer/PS diagnostics against immutable R27 sources."""
import difflib
import hakios
import hakios_observer


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    prior = hakios.read(hakios.ROOT / 'reports/hakios-device-r27.json')
    assert not prior['user_screen_observation']['observation_pending']
    assert not (hakios.ROOT / 'reports/hakios-p2-ios14-launchd-observer-ready.json').exists(), 'Do not overwrite frozen sources'
    base = hakios.ROOT / 'artifacts/hakios/p2-ios14-launchd-handoff-ready/sources/upstream/xnuloader/loader.c'
    old = base.read_text()
    anchor = '\tfor (unsigned i=0; i<sizeof(launch)/sizeof(launch[0]); i++)\n\t    lithium_show_line(lithium_last_line(log, bytes, launch[i]), log + bytes);'
    assert old.count(anchor) == 1
    new = old.replace(anchor, '''	/* A started observer proves the preceding descriptor/exec handoff.
	 * Keep its result and the current PS phase compact; pre-job failures
	 * still use the complete legacy handoff diagnostics above. */
	const char *budget = lithium_last_line(log, bytes, "LITHIUM P2 LAUNCHBUDGET ");
	if (budget == NULL) {
		for (unsigned i=0; i<sizeof(launch)/sizeof(launch[0]); i++)
		    lithium_show_line(lithium_last_line(log, bytes, launch[i]), log + bytes);
	} else {
		static const char *observer[] = {"LITHIUM P2 LAUNCHROOT ", "LITHIUM P2 LAUNCHJOB ",
		    "LITHIUM P2 LAUNCHBUDGET ", "LITHIUM P2 LAUNCHOBS phase=start ",
		    "LITHIUM P2 LAUNCHOBS phase=ps_begin ", "LITHIUM P2 LAUNCHPS stage=capture ",
		    "LITHIUM P2 DYNBUDGET PS ", "LITHIUM P2 DYNRUN PS ",
		    "LITHIUM P2 DYNWAIT PS ", "LITHIUM P2 DYNTIME PS ",
		    "LITHIUM P2 LAUNCHPS stage=complete ", "LITHIUM P2 LAUNCHPROC PID1 ",
		    "LITHIUM P2 LAUNCHPROC NOTIFYD ", "LITHIUM P2 LAUNCHPROC CFPREFSD ",
		    "LITHIUM P2 LAUNCHRESULT "};
		for (unsigned i=0; i<sizeof(observer)/sizeof(observer[0]); i++)
		    lithium_show_line(lithium_last_line(log, bytes, observer[i]), log + bytes);
		const char *last = lithium_last_line(log, bytes, "LITHIUM P2 LAUNCHOBS ");
		if (last != lithium_last_line(log, bytes, "LITHIUM P2 LAUNCHOBS phase=start ") &&
		    last != lithium_last_line(log, bytes, "LITHIUM P2 LAUNCHOBS phase=ps_begin "))
		    lithium_show_line(last, log + bytes);
	}''', 1)
    patch = hakios.ROOT / spec['port_path'] / 'patches/0015-lithium-launchd-observer.patch'
    patch.write_text(''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
        fromfile='a/xnuloader/loader.c', tofile='b/xnuloader/loader.c')), newline='\n')
    hakios.write(patch.with_suffix('.json'), dict(baseline=hakios.PIN,
        patch_sha256=hakios.sha(patch), preimage_sha256=hakios.sha(base),
        device_test_only=True, kernel_policies_changed=False, apple_binaries_modified=False,
        generic_services_replaced=False, budget=hakios_observer.contract(),
        retained_observer_phases=True, retained_original_ps_runner=True, hardware_verified=False))
    print('Prepared R28 observer timing, stderr diagnostics and retained PS summary')


if __name__ == '__main__': main()
