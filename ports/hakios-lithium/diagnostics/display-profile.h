/* Lithium display defaults. Explicit upstream profile arguments take precedence. */
#ifndef LITHIUM_DISPLAY_PROFILE_H
#define LITHIUM_DISPLAY_PROFILE_H

struct lithium_display_profile {
    uint32_t scale, subtype;
    int automatic;
};

static inline struct lithium_display_profile
lithium_display_resolve(uint32_t width, uint32_t height, uint32_t scale, uint32_t subtype)
{
    struct lithium_display_profile profile = { scale, subtype, 0 };
    if (width == 750 && height == 1334 && scale == 0 && subtype == 0) {
        profile.scale = 2;
        profile.subtype = 1334;
        profile.automatic = 1;
    }
    return profile;
}

#endif
