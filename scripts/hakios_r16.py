"""Exact display profile revision; retain every frozen R15 payload argument."""
from hakios_boot_observer import once

PORT_DELTA = {'patches/0016-lithium-display-profile.patch', 'diagnostics/r16.json',
    'diagnostics/display-profile.h', 'diagnostics/display-kernel.inc', 'diagnostics/display-userspace.inc'}


def framebuffer_source(text):
    text = once(text, '#include "../../../../devices/lithium/diagnostics/bgr24-pack.inc"',
        '#include "../../../../devices/lithium/diagnostics/bgr24-pack.inc"\n'
        '#include "../../../../devices/lithium/diagnostics/display-kernel.inc"')
    text = once(text, '\tif (gFBScale != 0 && gFBScale != kFBDefaultScale) {',
        '\tuint32_t scale = gFBScale, subtype = gFBSubtype;\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tlithium_display_dt_log("before");\n'
        '\tstruct lithium_display_profile profile = lithium_display_resolve(fWidth, fHeight, scale, subtype);\n'
        '\tscale = profile.scale; subtype = profile.subtype;\n'
        '#endif\n'
        '\tif (scale != 0 && scale != kFBDefaultScale) {')
    text = once(text, '\t\tuint32_t scale = gFBScale;\n', '')
    text = once(text, '\t\tif (gFBSubtype == 0) {', '\t\tif (subtype == 0) {')
    text = once(text, '\t\t\tuint32_t subtype = gFBSubtype;\n', '')
    text = once(text, '\tfColumn = IONewZero(uint32_t, fPanelWidth);',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tlithium_display_dt_log("after");\n'
        '\tIOLog("LITHIUM DISPLAY GEOMETRY render=%ux%u panel=%ux%u panel_stride=%u panel_bpp=%u auto_profile=%u requested_scale=%u requested_subtype=%u effective_scale=%u effective_subtype=%u map=full_panel_stretch\\n",\n'
        '\t    fWidth, fHeight, fPanelWidth, fPanelHeight, fRowBytes, fPixelBytes * 8, profile.automatic,\n'
        '\t    gFBScale, gFBSubtype, scale, subtype);\n'
        '#endif\n'
        '\tfColumn = IONewZero(uint32_t, fPanelWidth);')
    return text


def springboard_source(text):
    text = once(text, 'typedef struct { double x, y, width, height; } hk_rect;',
        'typedef struct { double x, y, width, height; } hk_rect;\n'
        '#include "../../devices/lithium/diagnostics/display-userspace.inc"')
    text = once(text, 'status_bar_fallback_is_split(void)\n{',
        'status_bar_fallback_is_split(void)\n{\n'
        '\tif (lithium_display_native_statusbar()) return;')
    return once(text, '\t\tlithium_sb_mark("main_queue_enter");',
        '\t\tlithium_sb_mark("main_queue_enter");\n'
        '\t\t/* UIKit is initialized here; no UIScreen access in the constructor. */\n'
        '\t\tdispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ lithium_display_dump(0); });\n'
        '\t\tdispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ lithium_display_dump(1); });\n'
        '\t\tdispatch_after(dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ lithium_display_dump(2); });')


def command_line(prior):
    from hakios_r15 import command_budget
    command_budget(prior, '2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty')
    if any(w.startswith(('fbscale=', 'fbsubtype=')) for w in prior.split()):
        raise RuntimeError('R16 requires the reviewed R15 implicit display profile')
    if prior.split().count('fbwidth=750') != 1 or prior.split().count('fbheight=1334') != 1:
        raise RuntimeError('R16 requires the frozen 750x1334 render canvas')
    return prior


def matches_prior(work, name, digest):
    import hakios
    path = work/'devices/lithium/diagnostics/r16.json'
    if path.exists():
        contract = hakios.read(path)
        if name in contract['before_sha256'] and contract['before_sha256'][name] == digest:
            digest = contract['after_sha256'][name]
    path = work/'devices/lithium/diagnostics/r18.json'
    if path.exists():
        c = hakios.read(path)
        if c['before_sha256'].get(name) == digest: digest = c['after_sha256'][name]
    return hakios.sha(work/name) == digest
