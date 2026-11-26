#include <assert.h>
#include <stdio.h>
#include "../../../ports/hakios-lithium/peripherals/touch-mmio-policy.h"

int main(void)
{
    const uint32_t allowed[] = {
        0x00325004, 0x00352004, 0x00330000, 0x00330008, 0x00330020, 0x00330024,
        0x01067000, 0x01068000, 0x01079004, 0x0108d004,
        0x075ba004, 0x075ba030, 0x075ba404
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        assert(lithium_touch_mmio_observation_allowed(allowed[i]));
        for (unsigned j = 1; j <= 3; ++j)
            assert(!lithium_touch_mmio_observation_allowed(allowed[i] + j));
    }
    assert(!lithium_touch_mmio_observation_allowed(0));
    assert(!lithium_touch_mmio_observation_allowed(UINT32_MAX));
    assert(!lithium_touch_mmio_observation_allowed(0x01069000)); /* reset GPIO89 */
    assert(!lithium_touch_mmio_observation_allowed(0x075ba00c)); /* SW_RESET */
    assert(!lithium_touch_mmio_observation_allowed(0x075ba110)); /* FIFO */
    struct lithium_touch_mmio_guard g = {
        .address = UINT64_C(0xffffffe010000004), .owner = 0x1000,
        .instruction = UINT64_C(0xffffffe020000000),
        .recovery = UINT64_C(0xffffffe020000020), .physical = 0x075ba004,
        .armed = true
    };
    struct lithium_touch_mmio_fault f = {
        .address = g.address, .owner = g.owner, .instruction = g.instruction,
        .affinity = 1, .esr = 0x96000010, .max_cpu = 0, .thread_mode = true
    };
    assert(lithium_touch_mmio_fault_matches(&g, &f));
    assert(!lithium_touch_mmio_fault_matches(NULL, &f));
    assert(!lithium_touch_mmio_fault_matches(&g, NULL));
    for (unsigned ec = 0; ec < 64; ++ec) {
        f.esr = (ec << 26) | 0x02000010;
        assert(lithium_touch_mmio_fault_matches(&g, &f) == (ec == 0x25));
    }
    for (unsigned dfsc = 0; dfsc < 64; ++dfsc) {
        f.esr = 0x96000000 | dfsc;
        assert(lithium_touch_mmio_fault_matches(&g, &f) == (dfsc == 0x10));
    }
    for (unsigned bit = 0; bit < 32; ++bit) {
        f.esr = 0x96000010 ^ (1u << bit);
        const bool constrained = (UINT32_C(0xfe0005ff) & (1u << bit)) != 0;
        assert(lithium_touch_mmio_fault_matches(&g, &f) == !constrained);
    }
    f.esr = 0x96000010;
#define REJECT_G(field, value) do { \
    struct lithium_touch_mmio_guard changed = g; changed.field = (value); \
    assert(!lithium_touch_mmio_fault_matches(&changed, &f)); \
} while (0)
#define REJECT_F(field, value) do { \
    struct lithium_touch_mmio_fault changed = f; changed.field = (value); \
    assert(!lithium_touch_mmio_fault_matches(&g, &changed)); \
} while (0)
    REJECT_G(armed, false); REJECT_G(address, 0); REJECT_G(address, g.address + 1);
    REJECT_G(owner, 0); REJECT_G(owner, g.owner + 1); REJECT_G(instruction, 0);
    REJECT_G(instruction, g.instruction + 4); REJECT_G(recovery, 0);
    REJECT_G(recovery, g.instruction); REJECT_G(physical, 0x075ba00c);
    REJECT_F(address, f.address + 4); REJECT_F(owner, f.owner + 1);
    REJECT_F(instruction, f.instruction + 4); REJECT_F(affinity, 0x100);
    REJECT_F(affinity, 0x101); REJECT_F(max_cpu, 1); REJECT_F(max_cpu, -1);
    REJECT_F(thread_mode, false);
    assert(lithium_touch_mmio_fault_matches(&g, &f));
    puts("PASS touch MMIO policy: 13 read addresses; exact PC/FAR/thread/single-CPU/ESR");
    return 0;
}
