/* Read actual device-tree values; absent/malformed properties stay explicit. */
#include "display-profile.h"

static bool
lithium_display_dt_u32(const char *path, const char *key, uint32_t *value)
{
    IORegistryEntry *node = IORegistryEntry::fromPath(path, gIODTPlane);
    if (node == NULL) return false;
    OSData *data = OSDynamicCast(OSData, node->getProperty(key));
    bool valid = data != NULL && data->getLength() == sizeof(*value);
    if (valid) memcpy(value, data->getBytesNoCopy(), sizeof(*value));
    node->release();
    return valid;
}

static void
lithium_display_dt_log(const char *phase)
{
    uint32_t chosen = 0, artwork = 0, subtype = 0;
    bool a = lithium_display_dt_u32("/chosen", "display-scale", &chosen);
    bool b = lithium_display_dt_u32("/product", "artwork-scale-factor", &artwork);
    bool c = lithium_display_dt_u32("/product", "artwork-device-subtype", &subtype);
    IOLog("LITHIUM DISPLAY DT phase=%s chosen_scale=%u artwork_scale=%u subtype=%u valid=%u/%u/%u\n",
        phase, chosen, artwork, subtype, a, b, c);
}
