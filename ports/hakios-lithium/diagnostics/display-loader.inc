/* Set the exact 750x1334 profile before the flattened tree is published.
 * No framebuffer addresses, boot arguments or persistent caches are changed. */
#include "display-profile.h"

static struct lithium_display_profile lithium_loader_display;

/* Reject duplicates/malformed values rather than disagreeing with the kernel. */
static int
lithium_display_arg(const char *args, const char *key, uint32_t *value)
{
    unsigned found = 0;
    size_t length = str_len(key);
    for (const char *p = args; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        const char *begin = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t count = (size_t)(p - begin);
        if (count <= length || memcmp(begin, key, length) || begin[length] != '=') continue;
        if (++found != 1 || count == length + 1) return -1;
        uint32_t n = 0;
        for (size_t i = length + 1; i < count; i++) {
            unsigned digit = (unsigned)(unsigned char)begin[i] - '0';
            if (digit > 9 || n > (0xffffffffU - digit) / 10) return -1;
            n = n * 10 + digit;
        }
        *value = n;
    }
    return (int)found;
}

static int
lithium_display_identity_u32(const char *key, uint32_t *value)
{
    unsigned found = 0;
    for (unsigned i = 0; i < sizeof(identity_product) / sizeof(identity_product[0]); i++) {
        const struct identity_prop *p = &identity_product[i];
        if (!str_eq(p->name, key)) continue;
        if (++found != 1 || p->length != sizeof(*value) ||
            p->offset > sizeof(identity_product_data) ||
            p->length > sizeof(identity_product_data) - p->offset) return 0;
        memcpy(value, identity_product_data + p->offset, sizeof(*value));
    }
    return found == 1;
}

static void
lithium_display_loader_configure(const char *args)
{
    uint32_t width = 0, height = 0, scale = 0, subtype = 0, before_scale = 0, before_subtype = 0;
    int w = lithium_display_arg(args, "fbwidth", &width);
    int h = lithium_display_arg(args, "fbheight", &height);
    int s = lithium_display_arg(args, "fbscale", &scale);
    int t = lithium_display_arg(args, "fbsubtype", &subtype);
    int identity_valid = lithium_display_identity_u32("artwork-scale-factor", &before_scale) &&
        lithium_display_identity_u32("artwork-device-subtype", &before_subtype);
    int valid = w == 1 && h == 1 && s == 0 && t == 0 && identity_valid;
    lithium_loader_display = lithium_display_resolve(0, 0, 0, 0);
    if (valid) lithium_loader_display = lithium_display_resolve(width, height, scale, subtype);
    print("LITHIUM DISPLAY LOADER phase=before chosen_scale=3 artwork_scale=%d subtype=%d valid=%d\n",
        (uint64_t)before_scale, (uint64_t)before_subtype, (uint64_t)identity_valid);
    print("LITHIUM DISPLAY LOADER phase=after chosen_scale=%d artwork_scale=%d subtype=%d auto_profile=%d render=%dx%d\n",
        (uint64_t)(lithium_loader_display.automatic ? lithium_loader_display.scale : 3),
        (uint64_t)(lithium_loader_display.automatic ? lithium_loader_display.scale : before_scale),
        (uint64_t)(lithium_loader_display.automatic ? lithium_loader_display.subtype : before_subtype),
        (uint64_t)lithium_loader_display.automatic, (uint64_t)width, (uint64_t)height);
}

/* Replace each existing product property, never emit duplicate DT keys. */
static int
lithium_display_loader_identity(struct dt_writer *w, const struct identity_prop *props,
    const struct identity_prop *p)
{
    if (!lithium_loader_display.automatic || props != identity_product) return 0;
    if (str_eq(p->name, "artwork-scale-factor")) {
        dt_prop_u32(w, p->name, lithium_loader_display.scale);
        return 1;
    }
    if (str_eq(p->name, "artwork-device-subtype")) {
        dt_prop_u32(w, p->name, lithium_loader_display.subtype);
        return 1;
    }
    return 0;
}
