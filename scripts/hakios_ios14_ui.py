"""Bounded 18H17 adaptation of the pinned HakiOS UI injection libraries."""
from hakios_bgr24 import replace_once


def uuid_guard(uuid):
    assert len(uuid) == 32
    values = ', '.join('0x' + uuid[i:i + 2] for i in range(0, 32, 2))
    return '''
/* Only the original audited arm64/18H17 cache may use these private hooks. */
static int
lithium_ui_cache_matches(void)
{
    static const unsigned char expected[16] = { ''' + values + ''' };
    unsigned char actual[16];
    bool (*get_uuid)(unsigned char *) = (bool (*)(unsigned char *))dlsym(RTLD_DEFAULT, "_dyld_get_shared_cache_uuid");
    if (get_uuid == NULL || !get_uuid(actual) || memcmp(actual, expected, sizeof(actual)) != 0) {
        say("LITHIUM UI ABI cache_uuid=MISMATCH hooks=REFUSED\\n");
        return 0;
    }
    say("LITHIUM UI ABI cache_uuid=18H17 hooks=ENABLED\\n");
    return 1;
}
'''


def window_server(source, abi):
    begin = source.index('static int\nmy_is_ready(')
    end = source.index('\n/*\n * The EAGLServer', begin) if '\n/*\n * The EAGLServer' in source[begin:] else source.index('static void *\nmy_renderer(', begin)
    # Do not carry iOS16 C++ field offsets into 14. Only call the checked original methods.
    source = source[:begin] + '''static int
my_is_ready(void *display)
{
    int ready = orig_is_ready(display);
    if (ready != last_ready || calls_ready < 3) say("LITHIUM UI READY result=%d call=%u\\n", ready, calls_ready);
    last_ready = ready; calls_ready++;
    return ready;
}

static int
my_ignore_update_p(void *display, void *update)
{
    int ignore = orig_ignore_update_p(display, update) & 1;
    if (ignore != last_ignore || calls_ignore < 3) say("LITHIUM UI IGNORE result=%d call=%u\\n", ignore, calls_ignore);
    last_ignore = ignore; calls_ignore++;
    return ignore;
}

static uintptr_t
my_begin_update(void *display, void *update)
{
    uintptr_t result = orig_begin_update(display, update);
    if (calls_begin++ < 6) say("LITHIUM UI BEGIN result=%#lx\\n", result);
    return result;
}

static uintptr_t
my_finish_update(void *display, void *update, unsigned flags)
{
    uintptr_t result = orig_finish_update(display, update, flags);
    if (calls_finish++ < 6) say("LITHIUM UI FINISH result=%#lx flags=%#x\\n", result, flags);
    return result;
}

''' + source[end:]
    begin = source.index('static void\nwindow_server_fixes(')
    end = source.index('\n/*', begin)
    selected = [('AppleCLCDDisplay', 'IOMFBDisplay8is_readyEv', 'orig_is_ready', 'my_is_ready'),
        ('AppleCLCDDisplay', 'IOMFBDisplay15ignore_update_p', 'orig_ignore_update_p', 'my_ignore_update_p'),
        ('AppleCLCDDisplay', 'IOMFBDisplay12begin_update', 'orig_begin_update', 'my_begin_update'),
        ('AppleCLCDDisplay', 'IOMFBDisplay13finish_update', 'orig_finish_update', 'my_finish_update'),
        ('EAGLServer', 'EAGLServer8rendererEv', 'orig_renderer', 'my_renderer'),
        ('EAGLServer', 'EAGLServer13render_update', 'orig_render_update', 'my_render_update')]
    body = 'static void\nwindow_server_fixes(void)\n{\n'
    for table, name, original, hook in selected:
        entries = [entry for entry in abi['vtables'][table]['entries'] if any(name in n for n in entry['symbols'])]
        assert len(entries) == 1
        row = entries[0]
        body += f'    {original} = replace_virtual("{hook}", {row["slot"]}UL, {row["expected"]}UL, {hook});\n'
    body += '    say("LITHIUM UI HOOKS display=%d renderer=%d; fields=VERSION_INDEPENDENT\\n",\n'
    body += '        !!(orig_is_ready && orig_ignore_update_p && orig_begin_update && orig_finish_update),\n'
    body += '        !!(orig_renderer && orig_render_update));\n}\n'
    source = source[:begin] + body + source[end:]
    # Constructor keeps the upstream software renderer and HID implementation. GPU-specific
    # shader hooks and all 16.5-only optional patches are not executed in this profile.
    begin = source.index('__attribute__((constructor))\nstatic void\nhakios_init(')
    source = source[:begin] + uuid_guard(abi['cache_uuid']) + '''
__attribute__((constructor))
static void
hakios_init(void)
{
    const char *name = getprogname();
    size_t length = 0;
    const void *cache = _dyld_get_shared_cache_range(&length);
    if (name == NULL || cache == NULL || !lithium_ui_cache_matches()) return;
    slide = (uintptr_t)cache - CACHE_BASE;
    if (strcmp(name, "backboardd") == 0) {
        say("LITHIUM UI BACKBOARD software_candidate=1 a530_acceleration=0\\n");
        window_server_fixes();
        unsigned reached = 1;
        sysctlbyname("kern.hakios.lithium_ui_backboard", NULL, NULL, &reached, sizeof(reached));
        input_start();
    }
}
'''
    return source


def springboard(source, abi):
    rows = abi['images']['/System/Library/PrivateFrameworks/SpringBoard.framework/SpringBoard']['selected']
    addresses = {name:address for name,address,kind in rows}
    activation = addresses['-[SBLockdownManager _setupActivationState]']
    setup = addresses['-[SBSetupManager _setSetupRequiredReason:]']
    begin = source.index('__attribute__((constructor))\nstatic void\nhakios_springboard_init(')
    # Keep upstream activation/setup and software-vibrancy helpers. Other private methods,
    # wallpaper layouts and battery objects were audited for 16.5 only and remain unused.
    source = source[:begin] + uuid_guard(abi['cache_uuid']) + '''
__attribute__((constructor))
static void
hakios_springboard_init(void)
{
    const char *name = getprogname();
    size_t length = 0;
    const void *cache = _dyld_get_shared_cache_range(&length);
    if (name == NULL || strcmp(name, "SpringBoard") != 0 || cache == NULL || !lithium_ui_cache_matches()) return;
    slide = (uintptr_t)cache - CACHE_BASE;
    IMP activation = replace_method("SBLockdownManager", "_setupActivationState", ''' + activation + '''UL, (IMP)setup_activation_state);
    original_set_setup_required_reason = (void (*)(id, SEL, long))replace_method("SBSetupManager",
        "_setSetupRequiredReason:", ''' + setup + '''UL, (IMP)set_setup_required_reason);
    plain_vibrancy();
    say("LITHIUM UI SPRINGBOARD activation_hook=%d setup_hook=%d software_vibrancy=1\\n",
        activation != NULL, original_set_setup_required_reason != NULL);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        unsigned reached = 1;
        sysctlbyname("kern.hakios.lithium_ui_sbqueue", NULL, NULL, &reached, sizeof(reached));
        say("LITHIUM UI SPRINGBOARD main_queue=REPLIED desktop_visible=UNVERIFIED\\n");
    });
}
'''
    return source
