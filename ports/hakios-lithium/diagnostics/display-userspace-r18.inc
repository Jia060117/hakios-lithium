/* R18: retained 750x1334@2 profile; actual runtime measurements only. */
static bool
lithium_display_user_dt_u32(const char *path, const char *key, uint32_t *value)
{
    io_registry_entry_t node = IORegistryEntryFromPath(0, path);
    if (node == 0) return false;
    CFStringRef name = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
    CFTypeRef data = name ? IORegistryEntryCreateCFProperty(node, name, NULL, 0) : NULL;
    bool valid = data && CFGetTypeID(data) == CFDataGetTypeID() &&
        CFDataGetLength((CFDataRef)data) == sizeof(*value);
    if (valid) memcpy(value, CFDataGetBytePtr((CFDataRef)data), sizeof(*value));
    if (data) CFRelease(data);
    if (name) CFRelease(name);
    IOObjectRelease(node);
    return valid;
}

static bool
lithium_display_native_statusbar(void)
{
    uint32_t scale = 0, subtype = 0;
    bool chosen = lithium_display_user_dt_u32("IODeviceTree:/chosen", "display-scale", &scale);
    bool product = lithium_display_user_dt_u32("IODeviceTree:/product", "artwork-device-subtype", &subtype);
    bool native = chosen && product && scale == 2 && subtype == 1334;
    say("LITHIUM DISPLAY STATUSBAR scale=%u subtype=%u valid=%u/%u native_profile=%u\n",
        scale, subtype, chosen, product, native);
    return native;
}

static bool
lithium_display_selector(id object, const char *name)
{
    return object && class_respondsToSelector(object_getClass(object), sel_registerName(name));
}

static void
lithium_display_mg(void)
{
    void *library = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_LAZY);
    CFTypeRef (*copy)(CFStringRef) = library ? (CFTypeRef (*)(CFStringRef))dlsym(library, "MGCopyAnswer") : NULL;
    if (!copy) {
        say("LITHIUM DISPLAY MG available=0\n");
        if (library) dlclose(library);
        return;
    }
    const char *keys[] = { "ScreenDimensions", "main-screen-width", "main-screen-height",
        "main-screen-scale", "main-screen-class", "ArtworkDeviceSubType" };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
        CFTypeRef value = key ? copy(key) : NULL;
        CFStringRef description = value ? CFCopyDescription(value) : NULL;
        char text[768] = "(unavailable)";
        bool complete = description && CFStringGetCString(description, text, sizeof(text), kCFStringEncodingUTF8);
        if (description && !complete) snprintf(text, sizeof(text), "(description exceeds log bound)");
        for (char *p = text; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
        say("LITHIUM DISPLAY MG key=%s available=%u complete=%u value=%s\n", keys[i], value != NULL, complete, text);
        if (description) CFRelease(description);
        if (value) CFRelease(value);
        if (key) CFRelease(key);
    }
    dlclose(library);
}

/* Called only from the running main queue, never from the injection constructor. */
static void
lithium_display_dump(unsigned sample)
{
    id screen = SEND(id, objc_getClass("UIScreen"), "mainScreen");
    CGRect bounds = {0}, native = {0};
    double scale = 0, native_scale = 0;
    bool b = lithium_display_selector(screen, "bounds"), n = lithium_display_selector(screen, "nativeBounds");
    bool s = lithium_display_selector(screen, "scale"), ns = lithium_display_selector(screen, "nativeScale");
    if (b) bounds = ((CGRect (*)(id, SEL))objc_msgSend)(screen, sel_registerName("bounds"));
    if (n) native = ((CGRect (*)(id, SEL))objc_msgSend)(screen, sel_registerName("nativeBounds"));
    if (s) scale = ((double (*)(id, SEL))objc_msgSend)(screen, sel_registerName("scale"));
    if (ns) native_scale = ((double (*)(id, SEL))objc_msgSend)(screen, sel_registerName("nativeScale"));
    say("LITHIUM DISPLAY UISCREEN sample=%u bounds=%g,%g,%g,%g scale=%g native_bounds=%g,%g,%g,%g native_scale=%g valid=%u/%u/%u/%u\n",
        sample, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height, scale,
        native.origin.x, native.origin.y, native.size.width, native.size.height, native_scale, b, s, n, ns);
    id app = SEND(id, objc_getClass("UIApplication"), "sharedApplication");
    id windows = app ? SEND(id, app, "windows") : NULL;
    unsigned long count = windows ? SEND(unsigned long, windows, "count") : 0;
    say("LITHIUM DISPLAY WINDOWS sample=%u count=%lu logged_max=6\n", sample, count);
    for (unsigned long i = 0; i < count && i < 6; i++) {
        id window = ((id (*)(id, SEL, unsigned long))objc_msgSend)(windows, sel_registerName("objectAtIndex:"), i);
        if (!lithium_display_selector(window, "frame") || !lithium_display_selector(window, "bounds")) continue;
        CGRect frame = ((CGRect (*)(id, SEL))objc_msgSend)(window, sel_registerName("frame"));
        CGRect rect = ((CGRect (*)(id, SEL))objc_msgSend)(window, sel_registerName("bounds"));
        say("LITHIUM DISPLAY WINDOW sample=%u index=%lu class=%s frame=%g,%g,%g,%g bounds=%g,%g,%g,%g\n",
            sample, i, object_getClassName(window), frame.origin.x, frame.origin.y, frame.size.width, frame.size.height,
            rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
    }
    say("LITHIUM DISPLAY PROFILE sample=%u expected=375x667@2 actual=%gx%g@%g native=%gx%g@%g match=%u\n",
        sample, bounds.size.width, bounds.size.height, scale, native.size.width, native.size.height, native_scale,
        b && s && n && ns && bounds.size.width == 375 && bounds.size.height == 667 && scale == 2 &&
        native.size.width == 750 && native.size.height == 1334 && native_scale == 2);
    if (sample == 0) lithium_display_mg();
}
