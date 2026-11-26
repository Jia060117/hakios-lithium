#!/usr/bin/env python3
"""Activate bounded UI additions through the complete HakiOS port patch series."""
import difflib
import re
import hakios
import hakios_bgr24


def diff(name, before, after):
    return ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='a/' + name if before else '/dev/null', tofile='b/' + name))


def main():
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    port = hakios.ROOT / spec['port_path']
    target = port / 'patches/0016-lithium-integrated-ui.patch'
    assert not target.exists(), 'UI activation already exists; use the normal prepare/check flow'
    fb_name = 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    before = (work / fb_name).read_text()
    fb = hakios_bgr24.adapt(before)
    block = '''
/* 18H17 _kern_SwapEnd sends 0x280 bytes, starting at its object+0x18.
 * Translate only fields consumed by the existing HakiOS compositor. */
static bool
lithium_normalize_swap(uint8_t *out, const void *input, size_t bytes)
{
    if (input == NULL) return false;
    if (bytes == SWAP_SIZE) { memcpy(out, input, SWAP_SIZE); return true; }
    if (bytes != 0x280) return false;
    const uint8_t *in = (const uint8_t *)input;
    bzero(out, SWAP_SIZE);
    memcpy(out + SWAP_ID, in + 0x40, 4);
    memcpy(out + SWAP_SURFACE(0), in + 0x44, 3 * 4);
    memcpy(out + SWAP_SOURCE(0), in + 0x50, 3 * 16);
    memcpy(out + SWAP_ENABLED(0), in + 0x80, 3 * 4);
    memcpy(out + SWAP_DEST(0), in + 0x98, 3 * 16);
    uint32_t masks[2];
    memcpy(masks, in + 0xc8, sizeof(masks));
    masks[0] &= 7; masks[1] &= 7;
    memcpy(out + SWAP_LAYERS_SET, masks, sizeof(masks));
    return true;
}

extern "C" {
uint32_t lithium_ui_console_quiet, lithium_ui_layers;
uint32_t lithium_ui_backboard, lithium_ui_sbqueue, lithium_ui_svc, lithium_ui_surf;
}
SYSCTL_UINT(_kern_hakios, OID_AUTO, lithium_ui_backboard, CTLFLAG_RW | CTLFLAG_LOCKED, &lithium_ui_backboard, 0, "backboard injection reached");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lithium_ui_sbqueue, CTLFLAG_RW | CTLFLAG_LOCKED, &lithium_ui_sbqueue, 0, "SpringBoard main queue replied, not desktop acceptance");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lithium_ui_svc, CTLFLAG_RW | CTLFLAG_LOCKED, &lithium_ui_svc, 0, "upstream svc_test exit plus one");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lithium_ui_surf, CTLFLAG_RW | CTLFLAG_LOCKED, &lithium_ui_surf, 0, "upstream surf_test exit plus one");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lithium_ui_layers, CTLFLAG_RD | CTLFLAG_LOCKED, &lithium_ui_layers, 0, "completed layer pixel copies, not whole frames");

static void
lithium_layer_copied(void)
{
    uint32_t integrated = 0;
    if (PE_parse_boot_argn("lithium_ui", &integrated, sizeof(integrated)) && integrated) {
        lithium_ui_console_quiet = 1;
        lithium_ui_layers++;
    }
}
'''
    # Namespace declaration precedes the added sysctls.
    at = fb.index('#define SWAP_QUEUE')
    fb = fb[:at] + block + '\n' + fb[at:]
    old = '''\tcase kFBMethodSwapEnd:
\t\tif (args->structureInput == NULL || args->structureInputSize < SWAP_SIZE) {'''
    new = '''\tcase kFBMethodSwapEnd: {
        uint8_t normalized[SWAP_SIZE];
\t\tif (!lithium_normalize_swap(normalized, args->structureInput, args->structureInputSize)) {'''
    fb = hakios_bgr24.replace_once(fb, old, new)
    fb = hakios_bgr24.replace_once(fb, 'fOwner->present((const uint8_t *) args->structureInput);', 'fOwner->present(normalized);')
    fb = hakios_bgr24.replace_once(fb, '\n\tcase kFBMethodSwapWait:', '\n    }\n\tcase kFBMethodSwapWait:')
    fb = hakios_bgr24.replace_once(fb, 'drawRows(&job, job.ry0, job.ry1, fBlendRow);\n\t\treturn;',
        'drawRows(&job, job.ry0, job.ry1, fBlendRow);\n        lithium_layer_copied();\n\t\treturn;')
    fb = hakios_bgr24.replace_once(fb, '\tIOLockUnlock(fHelperLock);\n}\n\n/*\n * How evenly',
        '\tIOLockUnlock(fHelperLock);\n    lithium_layer_copied();\n}\n\n/*\n * How evenly')
    patch = diff(fb_name, before, fb)
    lr_name = 'xnu/files/osfmk/arm64/loader_return.c'
    before = (work / lr_name).read_text()
    lr = before.replace('static void\nloader_return_timer(', '''extern uint32_t lithium_ui_console_quiet, lithium_ui_layers;
extern uint32_t lithium_ui_backboard, lithium_ui_sbqueue, lithium_ui_svc, lithium_ui_surf;
static thread_call_t lithium_ui_return_call;
static uint32_t lithium_ui_extensions, lithium_ui_last_layers, lithium_ui_last_signals;

static void
lithium_ui_save(void)
{
    char line[256];
    snprintf(line, sizeof(line), "\\nLITHIUM UI FINAL layers_copied=%u bb_hook=%u sb_queue=%u svc_exit_plus1=%u surf_exit_plus1=%u extensions=%u physical_touch=NOT_CONNECTED gpu=SOFTWARE_CANDIDATE\\n",
        lithium_ui_layers, lithium_ui_backboard, lithium_ui_sbqueue, lithium_ui_svc, lithium_ui_surf, lithium_ui_extensions);
    loader_log_line(line);
}

static void
loader_return_timer(''', 1)
    needle = '\tstatic const char banner[] = "\\n--- loader_return_after expired ---\\n";'
    lr = hakios_bgr24.replace_once(lr, needle, '''    uint32_t integrated = 0;
    if (PE_parse_boot_argn("lithium_ui", &integrated, sizeof(integrated)) && integrated) {
        lithium_ui_save();
        /* Keep a candidate UI alive. Pixel copies plus the SB queue are evidence
         * of progress only: the user still has to observe a visible desktop. */
        if (lithium_ui_layers && lithium_ui_sbqueue) {
            loader_log_line("LITHIUM UI RETAIN candidate=1 desktop=UNVERIFIED timer=DISARMED\\n");
            return;
        }
        uint32_t signals = !!lithium_ui_backboard + !!lithium_ui_sbqueue + !!lithium_ui_svc + !!lithium_ui_surf;
        if (lithium_ui_extensions < 3 && (lithium_ui_layers > lithium_ui_last_layers || signals > lithium_ui_last_signals)) {
            uint64_t next;
            lithium_ui_last_layers = lithium_ui_layers;
            lithium_ui_last_signals = signals;
            lithium_ui_extensions++;
            clock_interval_to_deadline(300, NSEC_PER_SEC, &next);
            thread_call_enter_delayed(lithium_ui_return_call, next);
            return;
        }
        loader_log_line("LITHIUM UI DEADLINE progress_stalled_or_budget_ended=1 desktop=UNVERIFIED\\n");
    }
''' + needle)
    lr = hakios_bgr24.replace_once(lr, 'thread_call_t call = thread_call_allocate(loader_return_timer, NULL);',
        'thread_call_t call = thread_call_allocate(loader_return_timer, NULL);\n    lithium_ui_return_call = call;')
    # Panic also gets the compact metrics, before the loader jump.
    lr = hakios_bgr24.replace_once(lr, 'void\nloader_return(void)', 'static void lithium_ui_save(void);\n\nvoid\nloader_return(void)')
    lr = hakios_bgr24.replace_once(lr, '\tloader_jump(0, 0);', '\tlithium_ui_save();\n\tloader_jump(0, 0);')
    patch += diff(lr_name, before, lr)
    # Keep custom BGR24 serial text off a framebuffer owned by the compositor.
    # This is an existing hakios.patch addition; update its declared hunk size
    # and sync the matching full overlay rather than editing the compiled tree.
    hp_name = 'xnu/hakios.patch'
    hp_before = (work / hp_name).read_text()
    hp = hakios_bgr24.replace_once(hp_before, '+fbcon_transmit_data(uint8_t c)\n+{',
        '+fbcon_transmit_data(uint8_t c)\n+{\n+    extern uint32_t lithium_ui_console_quiet;\n+    if (lithium_ui_console_quiet) return;')
    # Only the pe_serial hunk gained two lines. Recalculate all hunk lengths.
    parts = hp.splitlines(True)
    for i, line in enumerate(parts):
        if not line.startswith('@@ '): continue
        j = i + 1
        while j < len(parts) and not parts[j].startswith(('@@ ', 'diff --git ', '--- a/')): j += 1
        h = re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)', line.rstrip('\n'))
        assert h
        old = sum(x.startswith((' ', '-')) for x in parts[i+1:j])
        new = sum(x.startswith((' ', '+')) for x in parts[i+1:j])
        parts[i] = f'@@ -{h[1]},{old} +{h[2]},{new} @@{h[3]}\n'
    hp = ''.join(parts)
    patch += diff(hp_name, hp_before, hp)
    pe_name = 'xnu/files/pexpert/arm/pe_serial.c'
    pe_before = (work / 'xnu-src/xnu-8796.121.2/pexpert/arm/pe_serial.c').read_text()
    pe = hakios_bgr24.replace_once(pe_before, 'fbcon_transmit_data(uint8_t c)\n{',
        'fbcon_transmit_data(uint8_t c)\n{\n    extern uint32_t lithium_ui_console_quiet;\n    if (lithium_ui_console_quiet) return;')
    patch += diff(pe_name, '', pe)
    loader_name = 'xnuloader/loader.c'
    loader_before = (work / loader_name).read_text()
    loader = hakios_bgr24.replace_once(loader_before, '\tprint("P2 STORAGE KERNEL:\\n");', '''    if (lithium_last_line(log, bytes, "LITHIUM UI FINAL ") != NULL) {
        print("COMBINED IOS14 UI ATTEMPT (NOT DESKTOP ACCEPTANCE)\\n");
        static const char *ui[] = {"LITHIUM P2 LAUNCHROOT ", "LITHIUM P2 LAUNCHEXEC ",
            "LITHIUM UI FINAL ", "LITHIUM UI DEADLINE ", "LITHIUM UI ABI ",
            "LITHIUM UI BACKBOARD ", "LITHIUM UI HOOKS ", "LITHIUM UI SPRINGBOARD ",
            "LITHIUM UI SVC ", "LITHIUM UI SURF ",
            "LITHIUM P2 FAULT ", "LITHIUM P2 STATE ", "LITHIUM P2 ABORT ", "LITHIUM P2 PAGEIN "};
        for (unsigned i = 0; i < sizeof(ui)/sizeof(ui[0]); i++)
            lithium_show_line(lithium_last_line(log, bytes, ui[i]), log + bytes);
        print("PHYSICAL TOUCH NOT CONNECTED; A530 ACCELERATION UNVERIFIED\\n");
    }
\tprint("P2 STORAGE KERNEL:\\n");''')
    patch += diff(loader_name, loader_before, loader)
    target.write_text(patch)
    # This header is copied through the normal device-file sync.
    header = port / 'tests/ios14_launchd.h'
    content = header.read_text()
    content = hakios_bgr24.replace_once(content,
        '    printf("LITHIUM P2 LAUNCHROOT switch_rc=%d errno=%d readonly=1 selected_jobs=3\\n", rc, rc < 0 ? errno : 0);',
        '''#if defined(LITHIUM_UI_JOB_COUNT)
    printf("LITHIUM P2 LAUNCHROOT switch_rc=%d errno=%d readonly=1 selected_jobs=%d\\n", rc, rc < 0 ? errno : 0, LITHIUM_UI_JOB_COUNT);
#else
    printf("LITHIUM P2 LAUNCHROOT switch_rc=%d errno=%d readonly=1 selected_jobs=3\\n", rc, rc < 0 ? errno : 0);
#endif''')
    header.write_text(content)
    spec['test_profile'] = 'p2-ios14-ui'
    hakios.write(hakios.MANIFEST, spec)
    hakios.write(hakios.ROOT / 'reports/hakios-ui-activation.json', dict(
        patch_sha256=hakios.sha(target), framebuffer_selector=5, original14_swap_bytes=640,
        original16_swap_bytes=1132, original14_layers=3, normalization='consumed fields only',
        source_abi_report='reports/hakios-ios14-ui-abi.json',
        deadline_seconds=900, progress_extension_seconds=300, max_extensions=3,
        keep_candidate_if='completed layer copy and SpringBoard main-queue reply',
        candidate_is_desktop_acceptance=False, physical_touch_connected=False,
        original_pe_source_sha256=hakios.sha(work/'xnu-src/xnu-8796.121.2/pexpert/arm/pe_serial.c'),
        full_kernel_compiled=False, hardware_verified=False))
    print('Activated integrated UI profile and normal port patch; run prepare/check/deps next')


if __name__ == '__main__': main()
