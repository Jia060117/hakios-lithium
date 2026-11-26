#!/usr/bin/env python3
"""Create a bounded MSM8996/lithium delta against the complete pinned HakiOS.

Sources stay in upstream; this emits reviewable patches and provenance only.
"""
import difflib
import re
import subprocess
import hakios

def replace(text, before, after):
    hakios.require(text.count(before) == 1, 'Upstream anchor differs: ' + before[:80])
    return text.replace(before, after, 1)

def hunk_counts(text):
    lines = text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        match = re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)', line)
        if not match:
            continue
        old = new = 0
        for entry in lines[index + 1:]:
            if entry.startswith(('@@ ', 'diff --git ', '--- a/')):
                break
            old += entry.startswith((' ', '-'))
            new += entry.startswith((' ', '+'))
        lines[index] = f'@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n'
    return ''.join(lines)

def main():
    spec = hakios.read(hakios.MANIFEST)
    upstream = hakios.policy(spec)
    sources = {}
    modified = {}
    for name in ('xnu/hakios.patch', 'xnu/files/osfmk/arm64/gic_v3_irq.c', 'xnuloader/loader.c', 'xnuloader/build.sh'):
        sources[name] = subprocess.check_output(['git', '-C', str(upstream), 'show', hakios.PIN + ':' + name]).decode()
    patch = replace(sources['xnu/hakios.patch'],
        '+MACHINE_FLAGS_ARM64_BCM2837 = -DARM64_BOARD_CONFIG_BCM2837 -Xclang -target-feature -Xclang +pan',
        '+MACHINE_FLAGS_ARM64_BCM2837 = -DARM64_BOARD_CONFIG_BCM2837 $(if $(filter msm8996,$(HAKIOS_SOC)),-DLITHIUM_ARMV8_0=1 -march=armv8-a,-Xclang -target-feature -Xclang +pan)')
    patch = replace(patch, '+#define __ARM_PAN_AVAILABLE__     1',
        '+#if !defined(LITHIUM_ARMV8_0)\n+#define __ARM_PAN_AVAILABLE__     1\n+#endif')
    modified['xnu/hakios.patch'] = hunk_counts(patch)

    # Reuse the same-generation upstream's serial-console component, adapted
    # only for the MIX's packed BGR24 scanout. No legacy kernel implementation.
    component = hakios.ROOT / 'upstream/research/ios-sailfish/xnu/patches/0003-sweet-framebuffer-serial-console.patch'
    component_text = component.read_text(encoding='utf-8')
    added = []
    for line in component_text.splitlines(keepends=True):
        if line.startswith(' #if HIBERNATION'):
            break
        if line.startswith('+') and not line.startswith('+++'):
            added.append(line[1:])
    console = ''.join(added).replace('sweet', 'lithium').replace('SWEET', 'LITHIUM').replace('32bpp', '24bpp')
    console = console.replace('volatile uint32_t *', 'volatile uint8_t *')
    console = console.replace('(fbcon_stride / 4)', 'fbcon_stride').replace(' + x0;', ' + x0 * 3;')
    console = console.replace('line[x] = FBCON_BG;', 'line[x * 3] = 0; line[x * 3 + 1] = 0; line[x * 3 + 2] = 0;')
    console = console.replace('line[gx * FBCON_SCALE + sx] = px;',
        'uint32_t off = (gx * FBCON_SCALE + sx) * 3;\n'
        '\t\t\t\t\tline[off] = (uint8_t)px; line[off + 1] = (uint8_t)(px >> 8); line[off + 2] = (uint8_t)(px >> 16);')
    console = console.replace('fbcon_width * 4', 'fbcon_width * 3')
    console = console.replace('if (!fbcon_width || !fbcon_height || fbcon_stride < fbcon_width * 3)',
        'if (fbcon_width < 16 || fbcon_width > 1080 || fbcon_height < 32 || fbcon_height > 2040 ||\n'
        '\t    fbcon_stride < fbcon_width * 3 || fbcon_stride > 3240 ||\n'
        '\t    reg[0] != 0x83401000ULL || reg[1] < (uint64_t)fbcon_stride * fbcon_height)')
    console = '#if defined(LITHIUM_ARMV8_0)\n' + console + '#endif\n\n'
    for name in ('pexpert/arm/pe_serial.c', 'pexpert/pexpert/arm/protos.h'):
        original = subprocess.check_output(['git', '-C', str(hakios.ROOT / 'artifacts/hakios/sources/xnu'), 'show',
            spec['baseline']['apple_xnu_commit'] + ':' + name]).decode()
        if name.endswith('pe_serial.c'):
            updated = replace(original, '#if HIBERNATION\n/**\n * Transitions the serial driver', console + '#if HIBERNATION\n/**\n * Transitions the serial driver')
            updated = replace(updated, '\tPE_parse_boot_argn("disable-uart-irq", &disable_uart_irq, sizeof(disable_uart_irq));',
                '\tPE_parse_boot_argn("disable-uart-irq", &disable_uart_irq, sizeof(disable_uart_irq));\n'
                '#if defined(LITHIUM_ARMV8_0)\n\tfbcon_probe();\n#endif')
        else:
            updated = replace(original, '\tSERIAL_DCC_UART=0x10', '\tSERIAL_DCC_UART=0x10,\n\tSERIAL_LITHIUM_FBCON=0x20')
        modified['xnu/hakios.patch'] += 'diff --git a/' + name + ' b/' + name + '\n' + ''.join(
            difflib.unified_diff(original.splitlines(keepends=True), updated.splitlines(keepends=True), fromfile='a/' + name, tofile='b/' + name))

    gic = sources['xnu/files/osfmk/arm64/gic_v3_irq.c']
    gic = replace(gic, '#define GICR_FRAME_SIZE         0x20000',
        'static uint32_t gicr_frame_stride = 0x20000; /* optional ADT redistributor-stride */\n#define GICR_FRAME_SIZE         0x20000')
    gic = replace(gic, 'frame += GICR_FRAME_SIZE', 'frame += gicr_frame_stride')
    gic = replace(gic, '\t\tgicr_size = reg[3];',
        '\t\tgicr_size = reg[3];\n'
        '\t\tif (SecureDTGetProperty(entry, "redistributor-stride", &prop, &prop_size) == kSuccess) {\n'
        '\t\t\tif (prop_size != sizeof(uint32_t)) panic("gic: bad redistributor-stride size");\n'
        '\t\t\tuint32_t stride = *(const uint32_t *)prop;\n'
        '\t\t\tif (stride < GICR_FRAME_SIZE || stride > gicr_size || (stride & 0xffff))\n'
        '\t\t\t\tpanic("gic: bad redistributor-stride value");\n'
        '\t\t\tgicr_frame_stride = stride;\n'
        '\t\t}')
    modified['xnu/files/osfmk/arm64/gic_v3_irq.c'] = gic

    loader = sources['xnuloader/loader.c']
    loader = replace(loader, '#define PAYLOAD_BASE        0xF0000000ULL',
        '#if defined(HAKIOS_LITHIUM)\n#define PAYLOAD_BASE        0x120000000ULL\n#else\n#define PAYLOAD_BASE        0xF0000000ULL\n#endif')
    loader = replace(loader, '#define MAX_MEM_SIZE        0x180000000ULL',
        '#if defined(HAKIOS_LITHIUM)\n#define MAX_MEM_SIZE        0x20000000ULL\n#else\n#define MAX_MEM_SIZE        0x180000000ULL')
    loader = replace(loader, '/* 6 GB; the kernel\'s bootstrap page tables cover 8 */',
        '/* 6 GB; the kernel\'s bootstrap page tables cover 8 */\n#endif')
    loader = replace(loader, 'static const struct platform platform_redwood = {',
        '#if defined(HAKIOS_LITHIUM)\n'
        'static const struct platform platform_lithium = {\n'
        '\t.name = "lithium", .io_base = 0x00400000, .io_size = 0x3fc00000,\n'
        '\t.gic_dist = 0x09bc0000, .gic_redist = 0x09c00000, .uart = 0,\n'
        '\t.cores = 4, .core_shift = 8, .psci_hvc = 0,\n'
        '};\n'
        '#define XL_CLEAR_PAN_UAO "" /* Kryo has neither PAN nor UAO */\n'
        '#else\n'
        '#define XL_CLEAR_PAN_UAO "  .inst 0xd500409f\\n" "  .inst 0xd500407f\\n"\n'
        '#endif\n\nstatic const struct platform platform_redwood = {')
    loader, pairs = re.subn(r'^[ \t]*"  \.inst 0xd500409f\\n"[^\n]*\n[ \t]*"  \.inst 0xd500407f\\n"[^\n]*\n',
        '        XL_CLEAR_PAN_UAO\n', loader, flags=re.M)
    hakios.require(pairs == 2, 'PAN/UAO return stubs differ')
    loader = replace(loader, '\t\tba->video.v_depth = 32;',
        '\t\tba->video.v_depth = 32;\n#if defined(HAKIOS_LITHIUM)\n\t\tba->video.v_depth = 24;\n#endif')
    loader = replace(loader, '\tdt_prop(&w, "reg", gic_reg, sizeof(gic_reg));',
        '\tdt_prop(&w, "reg", gic_reg, sizeof(gic_reg));\n#if defined(HAKIOS_LITHIUM)\n'
        '\tdt_prop_u32(&w, "redistributor-stride", 0x40000);\n#endif')
    loader = replace(loader, '\tprint("largest free block %x size %x\\n", best_base, best_size);',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\t/* Mu reserves this entire range with an EfiLoaderData allocation HOB.\n'
        '\t * It belongs to this loader and cannot be used by resident UEFI. */\n'
        '\tint owned = 0;\n'
        '\tfor (uint64_t off = 0; off < map_size; off += desc_size) {\n'
        '\t\tefi_memory_descriptor *d = (efi_memory_descriptor *)(map_buf + off);\n'
        '\t\tif (d->type == EfiLoaderData && d->phys_start == 0x100000000ULL &&\n'
        '\t\t    (d->pages << 12) == MAX_MEM_SIZE) owned = 1;\n'
        '\t}\n'
        '\tif (!owned || pi->forced_base != 0) { print("lithium kernel allocation contract missing\\n"); return 0; }\n'
        '\twin_base = 0x100000000ULL; win_size = MAX_MEM_SIZE;\n'
        '\tprint("lithium owned window %x size %x\\n", win_base, win_size);\n'
        '\treturn 1;\n'
        '#endif\n\tprint("largest free block %x size %x\\n", best_base, best_size);')
    loader = replace(loader, '\tdt_begin(&w, "arm-io");',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tdt_begin(&w, "lithium-fbcon");\n'
        '\tuint64_t fbcon_reg[2] = { SCREEN.base, (uint64_t)SCREEN.stride * SCREEN.height };\n'
        '\tdt_prop(&w, "reg", fbcon_reg, sizeof(fbcon_reg));\n'
        '\tdt_prop_u32(&w, "width", SCREEN.width);\n'
        '\tdt_prop_u32(&w, "height", SCREEN.height);\n'
        '\tdt_prop_u32(&w, "stride", SCREEN.stride);\n'
        '\tdt_end(&w);\n#endif\n\tdt_begin(&w, "arm-io");')
    loader = replace(loader, '\tprint("platform %s\\n", PLAT->name);',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tPLAT = &platform_lithium;\n'
        '\tSCREEN.base = 0x83401000; SCREEN.width = 1080; SCREEN.height = 2040; SCREEN.stride = 3240;\n'
        '#endif\n\tprint("platform %s\\n", PLAT->name);')
    loader = replace(loader, '\treboot();\n\treturn 0;\n}',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\t/* Keep the returned log visible. userdata is iOS HFSX: an automatic\n'
        '\t * stock Android reset is not part of this platform test. */\n'
        '\tprint("lithium test ended; use power + volume-down for Fastboot\\n");\n'
        '\tfor (;;) BS->stall(1000000);\n'
        '#else\n\treboot();\n#endif\n\treturn 0;\n}')
    modified['xnuloader/loader.c'] = loader
    modified['xnuloader/build.sh'] = replace(sources['xnuloader/build.sh'],
        '  -mgeneral-regs-only -O1', '  ${LOADER_CFLAGS:-} -mgeneral-regs-only -O1')
    diffs = []
    for name in sources:
        diffs.extend(difflib.unified_diff(sources[name].splitlines(keepends=True), modified[name].splitlines(keepends=True),
            fromfile='a/' + name, tofile='b/' + name))
    dest = hakios.ROOT / 'ports/hakios-lithium/patches/0001-msm8996-platform-loader.patch'
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(''.join(diffs), encoding='utf-8', newline='\n')
    hakios.write(dest.with_suffix('.json'), dict(baseline=hakios.PIN, scope='ARMv8.0 board capability, GIC stride and lithium loader parameters only',
        original_files={name: __import__('hashlib').sha256(sources[name].encode()).hexdigest() for name in sources},
        patch_sha256=hakios.sha(dest), sources=['HakiOS complete pinned port', 'MIX original DTB/entry/display records',
        'ios-sailfish 0079 redistributor-stride interface reference',
        'ios-sailfish 0003 framebuffer serial-console component adapted for BGR24'],
        framebuffer_component_sha256=hakios.sha(component), hardware_verified=False))
    print('Generated bounded HakiOS lithium platform/loader patch')

if __name__ == '__main__':
    main()
