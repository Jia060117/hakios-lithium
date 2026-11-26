/* SPDX-License-Identifier: MIT
 * APID write-owner observation only. No PMIC transactions or GPU aperture.
 */
static struct lgpr_result lg1_access_retained;
static __attribute__((noinline)) void lithium_gpu_g1_access_probe(void)
{
    printf("LITHIUM GPU access begin fields=13 passes=2 read_cap=26 writes=0 pmic_commands=0\n");
    enum lgpr_error error = lithium_gpu_g1_power_capture(&lg1_access_retained);
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned field = 0; field < LGPR_COUNT; ++field) {
            printf("LITHIUM GPU access pass=%u field=%u physical=0x%llx raw=0x%08x valid=%u\n",
                pass, field, (unsigned long long)lgpr_pa[field],
                lg1_access_retained.raw[pass][field],
                (lg1_access_retained.valid[pass] >> field) & 1u);
        }
    }
    printf("LITHIUM GPU access end error=%u reads=%u valid0=0x%x valid1=0x%x stable=0x%x gfx_write_ee=%u mx_owner=%u eligible=%u rpm_bus_verified=0 gpu_power_writes=0\n",
        error, lg1_access_retained.reads, lg1_access_retained.valid[0],
        lg1_access_retained.valid[1], lg1_access_retained.stable,
        lg1_access_retained.write_ee_gfx, lg1_access_retained.owner_mx,
        lg1_access_retained.rail_enable_eligible);
}
