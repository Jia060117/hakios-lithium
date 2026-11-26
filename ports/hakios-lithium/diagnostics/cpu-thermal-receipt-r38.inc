/* SPDX-License-Identifier: MIT */
#include <pexpert/device_tree.h>
#include "cpu-tsens-receipt-r38.h"
#include "cpu-perf-fixed-r38.h"

static void
lithium_cpu_thermal_report_uefi(void)
{
    static const unsigned affinities[] = {0, 1, 0x100, 0x101};
    static const unsigned clients[] = {4, 6, 9, 11};
    unsigned enabled = 0, ufs = 99, smp = 0;
    DTEntry chosen;
    const void *data;
    unsigned int size;
    struct r38_temp_receipt receipt;

    PE_parse_boot_argn("cpu_thermal_diag", &enabled, sizeof(enabled));
    if (enabled != 1) return;
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("ram_smp_return", &smp, sizeof(smp));
    if (ufs || smp != 1) return;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, R38_TSENS_RECEIPT_PROPERTY, &data, &size) != kSuccess ||
        size != sizeof(receipt)) {
        printf("LITHIUM CPU TEMP source=uefi receipt=unavailable reason=missing_or_size\n");
        return;
    }
    memcpy(&receipt, data, sizeof(receipt));
    if (receipt.magic != R38_TSENS_RECEIPT_MAGIC || !receipt.generation ||
        !r39_perf_profile_for_hz(receipt.target_hz) || receipt.phase_mask != 3 ||
        receipt.observed_us[0] > receipt.observed_us[1]) {
        printf("LITHIUM CPU TEMP source=uefi receipt=unavailable reason=invalid\n");
        return;
    }
    for (unsigned phase = 0; phase < 2; ++phase) {
        for (unsigned sensor = 0; sensor < 4; ++sensor) {
            const struct r38_temp_row *row = &receipt.row[phase][sensor];
            const char *tag = phase ? "after_pll_low" : "before_pll_low";
            unsigned valid = row->result == R38_TSENS_OK &&
                row->status_reads >= 1 && row->status_reads <= 3 &&
                (row->ctrl & R38_TSENS_ENABLE) && row->status != UINT32_MAX &&
                (row->status & R38_TSENS_VALID);
            char temperature[16] = "unavailable";
            char raw[16] = "UNREAD";
            if (row->status_reads >= 1 && row->status_reads <= 3)
                snprintf(raw, sizeof(raw), "0x%08x", row->status);
            if (valid)
                snprintf(temperature, sizeof(temperature), "%d",
                    r38_tsens8996_native_deci_c(row->status) * 100);
            printf("LITHIUM CPU TEMP source=uefi phase=%s generation=0x%llx observed_us=%llu affinity=0x%x id=%u valid=%u native_mC=%s ctrl=0x%08x trdy=0x%08x raw=%s status_reads=%u result=%u calibration_verified=0 freshness_verified=0\n",
                tag, receipt.generation, receipt.observed_us[phase],
                affinities[sensor], clients[sensor], valid, temperature,
                row->ctrl, row->trdy, raw, row->status_reads, row->result);
        }
    }
}
