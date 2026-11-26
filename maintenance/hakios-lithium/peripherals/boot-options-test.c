#include "boot-options.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct lithium_peripheral_options
parse(const char *input, bool valid)
{
    struct lithium_peripheral_options options;
    assert(lithium_peripheral_options_parse(input, strlen(input), &options) == valid);
    if (!valid) {
        assert(!options.touch && !options.touch_log && !options.touch_poll);
        assert(!options.touch_stage_requested && options.touch_stage == 0);
        assert(!options.touch_clock_init);
        assert(!options.touch_id_probe);
        assert(!options.touch_snapshot);
        assert(!options.touch_qup_init);
        assert(!options.touch_qup_state);
        assert(!options.touch_qup_phase);
        assert(!options.touch_qup_transfer);
        assert(!options.gpu_probe && options.gpu_stage == 0 && !options.gpu_wd);
    }
    return options;
}

int
main(void)
{
    struct lithium_peripheral_options options = parse(
        "bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata "
        "var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 "
        "fbwidth=750 fbheight=1334 loader_return_after=10800", true);
    assert(!options.touch && !options.touch_stage_requested && !options.gpu_probe);

    options = parse("gpu_stage=0 touch_log=1 touch_poll=1 gpu_wd=1 touchscreen=1", true);
    assert(!options.touch && !options.touch_log && !options.touch_poll);
    assert(!options.touch_stage_requested && options.touch_stage == 0);
    assert(!options.gpu_probe && options.gpu_stage == 0 && !options.gpu_wd);

    options = parse("touch=1\tgpu_probe=1\ngpu_stage=0 gpu_wd=1 touch_poll=1\r", true);
    assert(options.touch && options.touch_poll && !options.touch_log);
    assert(!options.touch_stage_requested);
    assert(options.gpu_probe && options.gpu_stage == 0 && options.gpu_wd);

    options = parse("touch=1 touch_log=1 touch_poll=0 gpu_stage=1", true);
    assert(options.touch && options.touch_log && !options.touch_poll);
    assert(!options.touch_stage_requested);
    assert(options.gpu_probe && options.gpu_stage == 1 && !options.gpu_wd);
    assert(parse("gpu_stage=4", true).gpu_stage == 4);

    static const char *stage_args[] = {
        "touch_stage=0", "touch_stage=1", "touch_stage=2", "touch_stage=3",
    };
    for (unsigned stage = 0; stage <= 3; ++stage) {
        options = parse(stage_args[stage], true);
        assert(!options.touch && !options.touch_log && !options.touch_poll);
        assert(options.touch_stage_requested && options.touch_stage == stage);
        assert(!options.gpu_probe && options.gpu_stage == 0 && !options.gpu_wd);
    }
    options = parse("gpu_probe=1 gpu_stage=4 gpu_wd=1 touch_stage=0", true);
    assert(options.touch_stage_requested && options.touch_stage == 0);
    assert(options.gpu_probe && options.gpu_stage == 4 && options.gpu_wd);
    options = parse("touch_stage=1 touch_clock_init=1 touch_diag=1 ufs=0", true);
    assert(options.touch_clock_init && options.touch_stage == 1);
    options = parse("touch_stage=1 touch_clock_init=0", true);
    assert(!options.touch_clock_init && options.touch_stage == 1);
    options = parse("touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_id_probe && options.touch_clock_init);
    assert(!parse("touch_stage=1 touch_clock_init=1 touch_id_probe=0", true).touch_id_probe);
    options = parse("touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_snapshot && !options.touch_id_probe && options.touch_clock_init);
    options = parse("touch_stage=1 touch_clock_init=1 touch_snapshot=0", true);
    assert(!options.touch_snapshot && !options.touch_id_probe);
    options = parse("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_qup_init && !options.touch_snapshot && !options.touch_id_probe);
    assert(!parse("touch_stage=1 touch_clock_init=1 touch_qup_init=0", true).touch_qup_init);
    options = parse("touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_qup_state && !options.touch_qup_init && !options.touch_snapshot && !options.touch_id_probe);
    assert(!parse("touch_stage=1 touch_clock_init=1 touch_qup_state=0", true).touch_qup_state);
    options = parse("touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_qup_transfer && !options.touch_qup_phase && !options.touch_qup_state && !options.touch_qup_init && !options.touch_snapshot && !options.touch_id_probe);
    assert(!parse("touch_stage=1 touch_clock_init=1 touch_qup_transfer=0", true).touch_qup_transfer);
    const char *transfer_invalid[]={"touch_qup_transfer", "touch_qup_transfer=", "touch_qup_transfer=2", "touch_qup_transfer=01", "touch_qup_transfer=1", "touch_stage=2 touch_clock_init=1 touch_qup_transfer=1", "touch_stage=1 touch_clock_init=1 touch_qup_transfer=1", "touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 ufs=2 touch_diag=1 loader_return_after=180"};
    for(unsigned i=0;i<sizeof(transfer_invalid)/sizeof(transfer_invalid[0]);++i)parse(transfer_invalid[i],false);
    const char *transfer_conflicts[]={"touch_qup_phase=1","touch_qup_state=1","touch_qup_init=1","touch_snapshot=1","touch_id_probe=1","touch_qup_transfer=0","ufs_write=1","var_hfs=ufs_darwin_data","root_apfs=ufs_userdata"};
    for(unsigned i=0;i<sizeof(transfer_conflicts)/sizeof(transfer_conflicts[0]);++i){char line[512];snprintf(line,sizeof(line),"touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180 %s",transfer_conflicts[i]);parse(line,false);}
    options = parse("touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_qup_phase && !options.touch_qup_state && !options.touch_qup_init && !options.touch_snapshot && !options.touch_id_probe);
    assert(!parse("touch_stage=1 touch_clock_init=1 touch_qup_phase=0", true).touch_qup_phase);

    const char *invalid[] = {
        "touch_qup_phase", "touch_qup_phase=", "touch_qup_phase=2", "touch_qup_phase=01",
        "touch_qup_phase=1", "touch_qup_phase=0", "touch_stage=1 touch_qup_phase=1",
        "touch_stage=2 touch_clock_init=1 touch_qup_phase=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_qup_phase=0",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_qup_state=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_qup_init=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_id_probe=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch", "touch=", "touch=1=1", "touch=2", "touch=01", "touch=-1",
        "touch=1 touch=1", "touch=0 touch=1", "touch_log=3", "touch_poll=9",
        "gpu_stage=5", "gpu_stage=x", "gpu_stage= 1", "touch=1 gpu_wd=2",
        "gpu_probe=1 gpu_probe=0", "gpu_stage=1 gpu_stage=0",
        "touch_stage", "touch_stage=", "touch_stage=4", "touch_stage=x",
        "touch_stage=00", "touch_stage=-1", "touch_stage=1x",
        "touch_stage=0 touch_stage=0", "touch_stage=0 touch_stage=3",
        "touch=0 touch_stage=0", "touch_stage=1 touch=1",
        "touch_log=0 touch_stage=2", "touch_poll=1 touch_stage=3",
        "touch_clock_init", "touch_clock_init=", "touch_clock_init=2",
        "touch_clock_init=01", "touch_clock_init=1", "touch_clock_init=0",
        "touch_stage=0 touch_clock_init=1", "touch_stage=2 touch_clock_init=0",
        "touch_stage=3 touch_clock_init=1", "touch=1 touch_clock_init=1",
        "touch_stage=1 touch_clock_init=1 touch_clock_init=0",
        "touch_id_probe", "touch_id_probe=", "touch_id_probe=2", "touch_id_probe=01",
        "touch_id_probe=1", "touch_id_probe=0", "touch_stage=1 touch_id_probe=1",
        "touch_stage=1 touch_clock_init=0 touch_id_probe=1",
        "touch_stage=0 touch_clock_init=1 touch_id_probe=1",
        "touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_id_probe=0",
        "touch_snapshot", "touch_snapshot=", "touch_snapshot=2", "touch_snapshot=01",
        "touch_snapshot=1", "touch_snapshot=0", "touch_stage=1 touch_snapshot=1",
        "touch_stage=2 touch_clock_init=1 touch_snapshot=1",
        "touch_stage=1 touch_clock_init=1 touch_snapshot=1",
        "touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_snapshot=0",
        "touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_id_probe=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_qup_init", "touch_qup_init=", "touch_qup_init=2", "touch_qup_init=01",
        "touch_qup_init=1", "touch_qup_init=0", "touch_stage=1 touch_qup_init=1",
        "touch_stage=2 touch_clock_init=1 touch_qup_init=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_init=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_qup_init=0",
        "touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_id_probe=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_qup_state", "touch_qup_state=", "touch_qup_state=2", "touch_qup_state=01",
        "touch_qup_state=1", "touch_qup_state=0", "touch_stage=1 touch_qup_state=1",
        "touch_stage=2 touch_clock_init=1 touch_qup_state=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_state=1",
        "touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_qup_state=0",
        "touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_id_probe=1 ufs=0 touch_diag=1 loader_return_after=180",
        "touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_qup_init=1 ufs=0 touch_diag=1 loader_return_after=180",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        parse(invalid[i], false);
    }
    const char *ram_bad[] = {
        "", "ufs=2 touch_diag=1 loader_return_after=180",
        "ufs=0 touch_diag=0 loader_return_after=180",
        "ufs=0 touch_diag=1 loader_return_after=181",
        "ufs=0 touch_diag=1 loader_return_after=180 ufs=0",
        "ufs=0 touch_diag=1 loader_return_after=180 touch_diag=1",
        "ufs=0 touch_diag=1 loader_return_after=180 root_apfs=ufs_userdata",
        "ufs=0 touch_diag=1 loader_return_after=180 var_hfs=ufs_darwin_data",
        "ufs=0 touch_diag=1 loader_return_after=180 ufs_write=0",
        "ufs=0 touch_diag=1 loader_return_after=180 hfsdiag=0",
        "ufs=0 touch_diag=1 loader_return_after=180 rd=disk0",
        "ufs=0 touch_diag=1 loader_return_after=180 rd=md0 rd=md0",
        "ufs=0\ttouch_diag=1 loader_return_after=180",
        "ufs=0 touch_diag=1\tloader_return_after=180",
        "ufs=0 touch_diag=1 loader_return_after=180\n",
    };
    for (size_t i = 0; i < sizeof(ram_bad) / sizeof(ram_bad[0]); ++i) {
        char value[400];
        snprintf(value, sizeof(value), "touch_stage=1 touch_clock_init=1 touch_id_probe=1 %s", ram_bad[i]);
        parse(value, false);
        snprintf(value, sizeof(value), "touch_stage=1 touch_clock_init=1 touch_snapshot=1 %s", ram_bad[i]);
        parse(value, false);
        snprintf(value, sizeof(value), "touch_stage=1 touch_clock_init=1 touch_qup_init=1 %s", ram_bad[i]);
        parse(value, false);
        snprintf(value, sizeof(value), "touch_stage=1 touch_clock_init=1 touch_qup_state=1 %s", ram_bad[i]);
        parse(value, false);
        snprintf(value, sizeof(value), "touch_stage=1 touch_clock_init=1 touch_qup_phase=1 %s", ram_bad[i]);
        parse(value, false);
    }
    options = parse("debug=0x8 rd=md0 touch_stage=1 touch_clock_init=1 touch_id_probe=1 "
        "touch_diag=1 ufs=0 loader_return_after=180", true);
    assert(options.touch_id_probe);
    static const char hidden_args[] = "touch=1 \0gpu_stage=1";
    assert(!lithium_peripheral_options_parse(hidden_args, sizeof(hidden_args) - 1, &options));
    assert(!options.touch && !options.touch_stage_requested && options.touch_stage == 0);
    assert(!options.gpu_probe);
    assert(lithium_peripheral_options_parse(NULL, 0, &options));
    assert(!lithium_peripheral_options_parse(NULL, 1, &options));
    puts("PASS boot options: defaults, explicit touch stages, legacy conflicts, GPU and malformed overrides");
}
