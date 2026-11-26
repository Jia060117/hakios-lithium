"""Exact R12 raw-storage diagnostic delta; no performance policy changes."""
from hakios_boot_observer import once

PORT_DELTA={'patches/0013-lithium-raw-storage.patch','diagnostics/r12.json',
    'diagnostics/raw-storage.inc','diagnostics/raw-storage-core.h',
    'firmware/lithiumPkg/Include/LithiumStorage.h'}

def storage_source(text):
    head,tail=text.split('void\nhakios_ufs_init(void)',1)
    if tail.count('\t\treturn;') != 3:raise RuntimeError('UFS initialization failure exits changed')
    tail=tail.replace('\t\treturn;',
        '#if defined(LITHIUM_ARMV8_0)\n\t\tlithium_raw_storage_if_requested();\n#endif\n\t\treturn;')
    text=head+'void\nhakios_ufs_init(void)'+tail
    text=once(text,'/* ---- bringing it up ---- */',
        '#if defined(LITHIUM_ARMV8_0)\n#include "../../../../devices/lithium/diagnostics/raw-storage.inc"\n#endif\n\n/* ---- bringing it up ---- */')
    return once(text,'\tif (PE_parse_boot_argn("ufs_bench", &check, sizeof(check)) && check != 0) {',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tlithium_raw_storage_if_requested();\n'
        '#endif\n\tif (PE_parse_boot_argn("ufs_bench", &check, sizeof(check)) && check != 0) {')

def loader_source(text):
    text=once(text,'#define BLOCK_LITHIUM_END 5242880ULL',
        '#define BLOCK_LITHIUM_END 5242880ULL\n'
        '#include "../devices/lithium/firmware/lithiumPkg/Include/LithiumStorage.h"\n'
        'static int block_rawbench;')
    text=once(text,'\t\t\t    || lba >= BLOCK_LITHIUM_END || count > BLOCK_LITHIUM_END - lba',
        '\t\t\t    || ((lba >= BLOCK_LITHIUM_END || count > BLOCK_LITHIUM_END - lba) &&\n'
        '\t\t\t        !(block_rawbench && lithium_scratch_range(lba,count)))')
    text=once(text,'\thc_requested = 0;\n\tblock_probe_requested = 0;',
        '#if defined(HAKIOS_LITHIUM)\n\tblock_rawbench=0;\n#endif\n\thc_requested = 0;\n\tblock_probe_requested = 0;')
    return once(text,'\t\thc_requested |= starts_with(c, "ufs_native=")',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\t\tblock_rawbench |= starts_with(c, "ufs_rawbench=1") && (c == pi.cmdline || c[-1] == \' \') &&\n'
        '\t\t    (c[14] == 0 || c[14] == \' \');\n#endif\n'
        '\t\thc_requested |= starts_with(c, "ufs_native=")')
