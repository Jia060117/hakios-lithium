"""Exercise the R18 loader's real display selection and flattened DT writer."""
import subprocess
import tempfile
from pathlib import Path

import hakios
import hakios_r18

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static size_t str_len(const char *s) { return strlen(s); }
static int str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static void print(const char *format, ...) { (void)format; }
struct identity_prop { const char *name; unsigned offset, length; };
static const struct identity_prop identity_product[] = {
    {"artwork-scale-factor", 0, 4}, {"artwork-device-subtype", 4, 4},
    {"artwork-display-gamut", 8, 3}, {"product-name", 11, 4},
};
static const unsigned char identity_product_data[] = {
    3, 0, 0, 0, 0x84, 0x09, 0, 0, 'P', '3', 0, 'M', 'I', 'X', 0,
};
'''

TEST = r'''
struct expected_property { const char *name; const void *value; uint32_t size; };
static void check_node(const uint8_t **cursor, const uint8_t *end,
    const struct expected_property *expected, unsigned count) {
    const uint8_t *p = *cursor;
    uint32_t header[2];
    assert((size_t)(end - p) >= sizeof(header));
    memcpy(header, p, sizeof(header));
    assert(header[0] == count && header[1] == 0);
    p += sizeof(header);
    for (unsigned i = 0; i < count; i++) {
        uint32_t bytes;
        assert(end - p >= 36);
        memcpy(&bytes, p + 32, sizeof(bytes));
        assert(bytes == expected[i].size && (size_t)(end - p) >= 36 + ((bytes + 3) & ~3U));
        assert(!strcmp((const char *)p, expected[i].name));
        assert(!memcmp(p + 36, expected[i].value, bytes));
        p += 36 + ((bytes + 3) & ~3U);
    }
    *cursor = p;
}
static void check(const char *args, uint32_t scale, uint32_t subtype) {
    _Alignas(8) uint8_t blob[1024] = {0};
    struct dt_writer w = { .base = blob, .cur = blob, .end = blob + sizeof(blob) };
    lithium_display_loader_configure(args);
    dt_begin(&w, "chosen");
    WRITE_CHOSEN_SCALE
    dt_end(&w);
    dt_begin(&w, "product");
    dt_identity(&w, identity_product, sizeof(identity_product) / sizeof(identity_product[0]),
        identity_product_data);
    dt_end(&w);
    assert(!w.overflow && w.depth == 0);
    const struct expected_property chosen[] = {{"name", "chosen", 7}, {"display-scale", &scale, 4}};
    const struct expected_property product[] = {{"name", "product", 8},
        {"artwork-scale-factor", &scale, 4}, {"artwork-device-subtype", &subtype, 4},
        {"artwork-display-gamut", "sRGB", 5}, {"product-name", "MIX", 4}};
    const uint8_t *p = blob;
    check_node(&p, w.cur, chosen, sizeof(chosen) / sizeof(chosen[0]));
    check_node(&p, w.cur, product, sizeof(product) / sizeof(product[0]));
    assert(p == w.cur);
}
int main(void) {
    check("bootcpu=1 cpus=1 fbwidth=750 fbheight=1334", 2, 1334);
    check("  fbheight=1334\tfbwidth=750\t ", 2, 1334);
    check("fbwidth=1125 fbheight=2436", 3, 2436);
    check("fbwidth=750 fbheight=1334 fbscale=3", 3, 2436);
    check("fbwidth=750 fbheight=1334 fbsubtype=2436", 3, 2436);
    check("fbwidth=750 fbwidth=750 fbheight=1334", 3, 2436);
    check("fbwidth=750 fbheight=1334 fbscale=", 3, 2436);
    check("fbwidth=4294968046 fbheight=1334", 3, 2436);
    check("fbwidth=750x fbheight=1334", 3, 2436);
    check("x-fbwidth=750 fbheight=1334", 3, 2436);
    puts("PASS: 10 flattened DT cases, exact properties, no duplicate keys");
}
'''


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    port = work / 'devices/lithium'
    contract = hakios.read(port / 'diagnostics/r18.json')
    prior = hakios.read(hakios.ROOT / 'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')
    hakios.require(hakios_r18.command_line(prior['cmdline']) == contract['payload_boot_args'],
                   'R18 does not preserve the approved boot arguments')
    for name, digest in contract['after_sha256'].items():
        hakios.require(hakios.sha(work / name) == digest, 'R18 source drift: ' + name)
        hakios_r18.historical_text(work, name, (work / name).read_text())

    loader = (work / 'xnuloader/loader.c').read_text()
    writer_start = loader.index('struct dt_writer {')
    writer_end = loader.index('\nstatic void\ndt_prop_u64(', writer_start)
    identity_start = loader.index('static int gamut_p3;')
    identity_end = loader.index('/* ------------------------------------------------------------------------ */',
                                loader.index('\nstatic void\ndt_end(', identity_start))
    identity = loader[identity_start:identity_end].replace(
        '../devices/lithium/diagnostics/display-loader.inc', 'display-loader.inc')
    chosen_start = loader.index('#if defined(HAKIOS_LITHIUM)\n\tdt_prop_u32(&w, "display-scale"')
    chosen_end = loader.index('\n#endif', chosen_start) + len('\n#endif')
    chosen = loader[chosen_start:chosen_end]
    with tempfile.TemporaryDirectory(prefix='r18-display-') as tmp:
        harness = Path(tmp) / 'display.c'
        executable = Path(tmp) / 'display'
        harness.write_text(HARNESS + loader[writer_start:writer_end] + '\n' + identity +
                           TEST.replace('    WRITE_CHOSEN_SCALE', chosen))
        subprocess.run(['clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-DHAKIOS_LITHIUM=1',
                        '-fsanitize=address,undefined', '-I', str(port / 'diagnostics'),
                        str(harness), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
    print(result.stdout, end='')
    sources = [Path(__file__), Path(hakios_r18.__file__),
               *(port / p for p in sorted(hakios_r18.PORT_DELTA)),
               port / 'diagnostics/display-profile.h', *(work / p for p in contract['after_sha256'])]
    hakios.write(hakios.ROOT / 'reports/hakios-ios16-r18-checks.json', dict(
        passed=True, revision=18, inputs=hakios.inputs(spec), actual_production_functions=True,
        cases=10, sanitizers=['ASan', 'UBSan'], stdout=result.stdout, hardware_verified=False,
        evidence_sha256={str(path): hakios.sha(path) for path in sources}))


if __name__ == '__main__':
    main()
