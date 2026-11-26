# GPU G2 PAS13 03 · 2026-11-26

PAS 认证并关闭：True；内存准备通过：True。
单次 RAM 接受时间 `2026-11-26T05:37:18.567325+08:00`，镜像 `5256c30541f9a34ef5cbb92b49b34819bde20c9f28dc84533d94329a2ac062d6`。
CPU 保持614.4 MHz配置模型；未提交CP、未运行Metal。

## 实际调用和准备

```json
{
  "prepare": [
    {
      "line": 127,
      "error": "0"
    }
  ],
  "reservation": [
    {
      "line": 292,
      "requested": "1",
      "prepared": "1",
      "prep_error": "0",
      "cookie": "0x60d081b18",
      "code_pa": "0x92300000",
      "owned_bytes": "8192",
      "va_map_bytes": "16384",
      "metadata_pa": "0x1b8000000",
      "metadata_bytes": "16384",
      "code_nc": "1",
      "metadata_nc": "1",
      "metadata_reserved": "1",
      "managed_retype": "0",
      "memory_release": "0"
    }
  ],
  "smc_before": [
    {
      "line": 167,
      "index": "0",
      "x0": "0x42000201",
      "x1": "0x82",
      "x2": "0xd",
      "x3": "0x1b8000000",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 174,
      "index": "1",
      "x0": "0x42000202",
      "x1": "0x3",
      "x2": "0xd",
      "x3": "0x92300000",
      "x4": "0x2000",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 176,
      "index": "2",
      "x0": "0x42000205",
      "x1": "0x1",
      "x2": "0xd",
      "x3": "0x0",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 180,
      "index": "3",
      "x0": "0x42000206",
      "x1": "0x1",
      "x2": "0xd",
      "x3": "0x0",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    }
  ],
  "smc_after": [
    {
      "line": 173,
      "index": "0",
      "callback": "0",
      "x0": "0x0",
      "x1": "0x0",
      "x2": "0x0",
      "x3": "0x0",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 175,
      "index": "1",
      "callback": "0",
      "x0": "0x0",
      "x1": "0x0",
      "x2": "0x0",
      "x3": "0x0",
      "x4": "0x2000",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 179,
      "index": "2",
      "callback": "0",
      "x0": "0x0",
      "x1": "0x0",
      "x2": "0x0",
      "x3": "0x0",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    },
    {
      "line": 181,
      "index": "3",
      "callback": "0",
      "x0": "0x0",
      "x1": "0x0",
      "x2": "0x0",
      "x3": "0x0",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x0",
      "x7": "0x0"
    }
  ],
  "pas_end": [
    {
      "line": 297,
      "error": "0",
      "phase": "8",
      "used": "1",
      "calls": "4",
      "pas_attempts": "4",
      "pas13_known": "0",
      "pas13_supported": "0",
      "metadata_writes": "1",
      "code_zeros": "1",
      "code_writes": "1",
      "authenticated": "1",
      "shutdown_ok": "1",
      "hold_dependencies": "0",
      "memory_pinned": "1",
      "resume": "0",
      "cp_commands": "0",
      "qualification_bits_manufactured": "0"
    }
  ],
  "combined_end": [
    {
      "line": 343,
      "phase": "power_clock_cycle",
      "error": "0",
      "blocker": "0",
      "blocker_name": "none",
      "mx_plan": "1",
      "mx_required_uv": "850000",
      "bus_stable": "1",
      "bus_branches": "0",
      "ahb_valid": "1",
      "ahb_config_hz": "19200000",
      "rail_ready": "1",
      "mx_ready": "0x81",
      "cx_on": "1",
      "consumer_safe": "1",
      "original_verified": "1",
      "restore_ack": "1",
      "pmic_commands": "2",
      "retain_power": "0",
      "uncertain": "0",
      "clock_rc": "10",
      "clock_loads": "160",
      "clock_stores": "42",
      "native_maps": "4",
      "native_unmaps": "4",
      "vbif_valid": "1",
      "vbif_raw": "0x20040001",
      "vbif_pa": "0xb0c000",
      "chip_id_valid": "0",
      "hardware_chip_id_valid": "0",
      "inherited_bus_vote_ack": "0",
      "same_boot_bimc_rate_measured": "0",
      "bke_read": "0",
      "elapsed_us": "128571",
      "total_us": "200000",
      "gx_untouched": "0",
      "pll_untouched": "0",
      "smmu": "unread",
      "rpm_votes": "0",
      "mx_writes": "0"
    }
  ],
  "diagnostic": [
    {
      "line": 109,
      "property_rc": "1",
      "property_bytes": "128",
      "present": "1",
      "format_valid": "1",
      "cookie": "0x60d081b18",
      "stage": "12",
      "detail": "0xf3f",
      "fingerprint": "0xa9c892bab8956430",
      "cover_type": "0x0",
      "cover_pa": "0x1b4000000",
      "cover_pages": "0x4004",
      "cover_attribute": "0x100f",
      "overlaps": "1",
      "par0": "0x44000001b8000b00",
      "par1": "0x44000001b8001b00",
      "par2": "0x44000001b8002b00",
      "par3": "0x44000001b8003b00"
    }
  ]
}
```

未进入时的零初始化 PAS end 不代表认证成功。元数据未通过只代表准备拒绝，不推断安全世界返回。

## 运行终点

正常timer/drain：True；stop温度记录4条；源争用丢字节[0]。
USB独立重放True，295帧 / 82803 raw / 73363 console字节。
接收器原始终态 `capture_failed` 保留；唯一同机Fastboot终点True。
生命周期通过True，不等同PAS或GPU加速通过。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=1 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_power_clock=1 gpu_zap=2
```
