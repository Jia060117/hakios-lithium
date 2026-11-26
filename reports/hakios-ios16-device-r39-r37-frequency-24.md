# R39 460.8 MHz 固定档筛选

单次接受：2026-11-21T23:27:53.815241+08:00。镜像 `143f4267447554382d12e17b65d34c582502d379e7f60baf9c88dfb3b2ebc691`。

启动计数 83/52。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=460800000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 48.6–58.6 | 134 |
| 0x1 | 45.1–56 | 134 |
| 0x100 | 42.6–61.8 | 134 |
| 0x101 | 42.9–62.7 | 134 |

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2441",
    "avg_us": "2453",
    "max_us": "2457",
    "ns_per_iteration": "2",
    "cntvct_min": "46883",
    "cntvct_avg": "47104",
    "cntvct_max": "47187",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "8972",
    "avg_us": "9350",
    "max_us": "9588",
    "MBps_min": "3499",
    "MBps_avg": "3588",
    "MBps_max": "3739",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "6521",
    "avg_us": "6530",
    "max_us": "6555",
    "ns_per_iteration": "6",
    "cntvct_min": "125205",
    "cntvct_avg": "125375",
    "cntvct_max": "125851",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "12394",
    "avg_us": "12418",
    "max_us": "12440",
    "MBps_min": "2697",
    "MBps_avg": "2701",
    "MBps_max": "2707",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "6520",
    "avg_us": "6524",
    "max_us": "6531",
    "ns_per_iteration": "6",
    "cntvct_min": "125182",
    "cntvct_avg": "125270",
    "cntvct_max": "125393",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "12367",
    "avg_us": "12397",
    "max_us": "12432",
    "MBps_min": "2698",
    "MBps_avg": "2706",
    "MBps_max": "2713",
    "warm_cache": "1"
  }
]
```

90 秒性能簇双核并发，引导核仅作 housekeeping:

```json
{
  "result": "1",
  "reason": "duration_complete",
  "created_mask": "0x7",
  "ready_mask": "0x7",
  "done_mask": "0x7",
  "joined": "1",
  "errors": "0",
  "hotstop": "0",
  "thermal_errors": "0",
  "samples": "91",
  "elapsed_ms": "90001",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 782 帧 / 192340 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
