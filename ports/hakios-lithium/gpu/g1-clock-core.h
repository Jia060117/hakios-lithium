/* SPDX-License-Identifier: BSD-2-Clause
 * MSM8996 Pro, one-shot 133MHz configuration/readback and reverse cleanup.
 * Source anchors: evidence/gpu-metal-20261123/clock/manifest.json.
 * No hardware entry point; no SPMI, RPM message, SMMU or GPU write here.
 */
#ifndef LITHIUM_GPU_G1_CLOCK_CORE_H
#define LITHIUM_GPU_G1_CLOCK_CORE_H
#include <stddef.h>
#include <stdint.h>

#define LG1C_CPU_HZ UINT64_C(614400000)
#define LG1C_XO_HZ UINT64_C(19200000)
#define LG1C_TARGET_HZ UINT64_C(133000000)
#define LG1C_PLL_HZ UINT64_C(532000000)
#define LG1C_PLL_L UINT32_C(27)
#define LG1C_PLL_ALPHA_LO UINT32_C(0x55555600)
#define LG1C_PLL_ALPHA_HI UINT32_C(0xb5)
#define LG1C_PLL_USER_MASK UINT32_C(0x01300f0f)
#define LG1C_PLL_USER UINT32_C(0x01200301) /* VCO2, alpha, postdiv4, main out */
#define LG1C_GFX_CFG UINT32_C(0x300) /* MMPLL2 postdiv / root bypass, no M/N */
#define LG1C_TIMER_CFG UINT32_C(1) /* XO /1, F_MM(19200000, xo, 1, 0, 0) */
#define LG1C_EXPECTED_DT_CHIP UINT32_C(0x05030004)

enum lg1c_status {
    LG1C_OK, LG1C_DISABLED, LG1C_ARGUMENT, LG1C_STATE, LG1C_DEPENDENCY,
    LG1C_IO, LG1C_HANDOFF, LG1C_READBACK, LG1C_TIMEOUT, LG1C_TIME,
    LG1C_ID_UNBOUND, LG1C_ID_MISMATCH, LG1C_CLEANUP
};
enum lg1c_phase {
    LG1C_PREPARE, LG1C_POWER, LG1C_BUS, LG1C_PLATFORM, LG1C_SNAPSHOT,
    LG1C_CX, LG1C_PLL, LG1C_ROOT, LG1C_GX, LG1C_CLOCKS, LG1C_VERIFY,
    LG1C_IDENTIFY, LG1C_REVERSE, LG1C_FINISHED
};
enum lg1c_dependency { LG1C_SUPPLY, LG1C_BUS_VOTE, LG1C_PLATFORM_LEASE, LG1C_DEP_COUNT };
enum lg1c_reg {
    LG1C_CX_CTL, LG1C_CX_STATUS, LG1C_GX_CTL, LG1C_CLAMP, LG1C_BCR,
    LG1C_GFX_CMD, LG1C_GFX_CONFIG, LG1C_GFX_BRANCH, LG1C_AHB_BRANCH,
    LG1C_TIMER_CMD, LG1C_TIMER_CONFIG, LG1C_TIMER_BRANCH, LG1C_ISENSE_BRANCH,
    LG1C_PLL_MODE, LG1C_PLL_L_REG, LG1C_PLL_A_LO, LG1C_PLL_A_HI, LG1C_PLL_USER_REG,
    LG1C_PLL_USER_HI, LG1C_PLL_CONFIG, LG1C_PLL_TEST_LO, LG1C_PLL_TEST_HI,
    LG1C_REG_COUNT
};
struct lg1c_reg_desc { uint32_t address, writable; };
extern const struct lg1c_reg_desc lg1c_regs[LG1C_REG_COUNT];

struct lg1c_request {
    uint64_t cpu_hz, xo_hz, gpu_hz;
    uint32_t gfx_corner, mx_corner, gpu_mx_corner;
    uint32_t bus_master, bus_slave, bus_index, ab_kbytes_s, ib_kbytes_s;
};
extern const struct lg1c_request lg1c_133_request;

struct lg1c_config {
    uint8_t enabled; /* zero default returns DISABLED without invoking callbacks */
    uint64_t cpu_hz;
    uint32_t run_budget_us, cleanup_budget_us; /* each 1000..200000 */
};
struct lg1c_identity {
    uint32_t raw, chip_id;
    uint64_t source_binding; /* source-backed physical register/decode receipt */
    uint8_t hardware_read;   /* must be 1; DT/catalog identity is not a read */
};
struct lg1c_ops {
    void *ctx;
    /* Acquires are SUPPLY -> BUS -> PLATFORM, releases reversed. A partial
     * acquire must return its nonzero token even on error, so it can be undone.
     * SUPPLY: external GFX settled + MX/GPU-MX votes, no voltage guessed here.
     * BUS: real master26->slave512 IB1600000KB/s vote/ack plus fabric clocks.
     * PLATFORM: live scoped lease, XO/PLL analog context, MMSS AHB dependency
     * chain, safe returned-access mapping and serialization. No demand that TZ
     * can never write: guard() validates this lease's current lifetime.
     */
    int (*dependency)(void *, enum lg1c_dependency, int acquire,
                      const struct lg1c_request *, uint64_t *token);
    int (*guard)(void *, const uint64_t tokens[LG1C_DEP_COUNT], int cleanup);
    int (*read32)(void *, uint32_t address, uint32_t *);
    int (*write32)(void *, uint32_t address, uint32_t);
    int (*barrier)(void *);
    int (*delay_us)(void *, uint32_t);
    uint64_t (*now_us)(void *); /* monotonic; 0/UINT64_MAX mean unavailable */
    /* Optional unresolved binding. Invoked ONCE only after fresh PWR_ON/root/
     * PLL/config/branch checks. Adapter must perform only its source-backed
     * minimum read. No RBBM_CHIP_ID address is invented by this core.
     */
    int (*identify)(void *, struct lg1c_identity *);
};
struct lg1c_result {
    enum lg1c_status error, cleanup_error;
    enum lg1c_phase phase, failure_phase;
    uint32_t reads, writes, delays, barriers, polls, last_address, last_raw;
    uint32_t chip_id, identity_raw;
    uint64_t configured_hz; /* arithmetic/readback, never measured MHz */
    uint8_t clock_readback_valid, chip_id_valid, cleanup_complete, used;
    uint32_t retained_dependencies, dirty_registers;
};
struct lg1c_context {
    struct lg1c_ops ops;
    struct lg1c_config config;
    struct lg1c_result result;
    uint64_t tokens[LG1C_DEP_COUNT], started, last_time;
    uint32_t saved[LG1C_REG_COUNT], shadow[LG1C_REG_COUNT], attempted[LG1C_REG_COUNT];
    uint32_t valid, dirty, uncertain;
    uint8_t cleanup, prepared, gx_attempted;
};
enum lg1c_status lg1c_prepare(struct lg1c_context *, const struct lg1c_ops *,
                             const struct lg1c_config *);
/* Always attempts dependency-safe reverse cleanup on returned failures.
 * First cleanup failure stops all lower-level power/clock removal and pins
 * remaining tokens. No automatic second run, retry, reset or re-acquire.
 *
 * Polling bounds only callbacks that RETURN. Neither poll counts nor software
 * deadlines can rescue a CPU stalled inside an MMIO bus transaction.
 */
enum lg1c_status lg1c_run(struct lg1c_context *);
#endif
