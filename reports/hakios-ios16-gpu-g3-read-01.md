# GPU G3 read 01 · 2026-11-26

单次隔离 RAM 镜像 `9e7feaa98371e9fee6ef31dc3d00603e808d99a42b78fb72940e37997bfa6e94`，接受于 `2026-11-26T05:49:52.824960+08:00`。
SMMU 完整发现通过：False；同机 Fastboot 终点：False。
CPU 保持 614.4 MHz 配置模型，CP、Metal 和 CoreAnimation 硬件执行尚未验证。

## 实际发现

```json
{
  "discover": [
    {
      "line": 283,
      "used": "1",
      "entry_rc": "10",
      "rc": "10",
      "complete": "0",
      "reads": "8",
      "samples": "4",
      "elapsed_us": "231",
      "cb_mask": "0x1",
      "cpu_pa": "44",
      "ias": "36",
      "oas": "36",
      "ubs": "48",
      "cap_pa": "36",
      "maps": "1",
      "unmaps": "1",
      "writes": "0",
      "installed": "0",
      "hold": "0"
    }
  ],
  "geometry": {
    "id0": 1208042498,
    "id1": 536903682,
    "id2": 29969,
    "id7": 0,
    "major": 0,
    "minor": 0,
    "register_page_shift": 12,
    "numpagendxb": 2,
    "num_smr": 2,
    "num_cb": 2,
    "num_s2cb": 0,
    "cb_read_mask": 1,
    "stage1_supported": true,
    "stream_match_supported": true,
    "coherent_walk_supported": true,
    "ptfs_4k": true,
    "extended_sid_encoding": false,
    "cpu_pa_bits": 44,
    "ias_bits": 36,
    "oas_bits": 36,
    "ubs_bits": 48,
    "effective_pa_bits": 36,
    "source_bound_profile_supported": true,
    "capability_checks_passed": false,
    "logged_widths_match_decode": true,
    "cpu_mmfr0": [
      {
        "line": 284,
        "valid": "1",
        "raw": "0x101124"
      }
    ]
  },
  "raw": [
    {
      "line": 288,
      "offset": 32,
      "valid": 3,
      "raw_a": 1208042498,
      "raw_b": 1208042498,
      "a": 1208042498,
      "b": 1208042498,
      "stable": true,
      "value": 1208042498
    },
    {
      "line": 289,
      "offset": 36,
      "valid": 3,
      "raw_a": 536903682,
      "raw_b": 536903682,
      "a": 536903682,
      "b": 536903682,
      "stable": true,
      "value": 536903682
    },
    {
      "line": 290,
      "offset": 40,
      "valid": 3,
      "raw_a": 29969,
      "raw_b": 29969,
      "a": 29969,
      "b": 29969,
      "stable": true,
      "value": 29969
    },
    {
      "line": 291,
      "offset": 60,
      "valid": 3,
      "raw_a": 0,
      "raw_b": 0,
      "a": 0,
      "b": 0,
      "stable": true,
      "value": 0
    }
  ],
  "pas_end": [
    {
      "line": 282,
      "error": "11",
      "phase": "3",
      "used": "1",
      "calls": "1",
      "pas_attempts": "1",
      "pas13_known": "0",
      "pas13_supported": "0",
      "metadata_writes": "1",
      "code_zeros": "0",
      "code_writes": "0",
      "authenticated": "0",
      "shutdown_ok": "0",
      "hold_dependencies": "1",
      "memory_pinned": "1",
      "resume": "0",
      "cp_commands": "0",
      "qualification_bits_manufactured": "0"
    }
  ],
  "interrupted": [
    {
      "line": 172,
      "index": "0",
      "callback": "0",
      "x0": "0x1",
      "x1": "0x82",
      "x2": "0xd",
      "x3": "0x1b8000000",
      "x4": "0x0",
      "x5": "0x0",
      "x6": "0x61900003",
      "x7": "0x0"
    }
  ]
}
```

只有 valid=3 且双遍相同的值参与解码。未采集路由、CB、页表和 fault 值保持 null。
本轮没有 SMMU 写入、页表分配、TLB 失效或 CP 提交。原始 ID7 与旧 capability 拒绝保留，
后续按固定 Linux compatible 选择版本的改动不会补认本轮完整通过。

SCM 的 interrupted 原值必须保留；本轮没有续接该调用，不能把它当作最终安全世界拒绝。
供电依赖是否恢复以 combined 原始记录为准，不移用上一轮成功结果。

## 退出与日志

180 秒正常 timer/drain：True；
源争用丢字节：[0]；stop 温度记录 4 条。
USB 289 帧、81011 raw / 71763 console 字节，独立重放 True。
接收器原始终态 `capture_failed` 保留。
唯一返回检查 `2026-11-26T05:54:33.518907+08:00`：Fastboot=False，不重查、不自动重试。
Fastboot 未找到不能单独证明物理终屏或复位失败原因。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=1 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_power_clock=1 gpu_zap=2 gpu_smmu_diag=1
```
