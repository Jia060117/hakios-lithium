# R39 460.8 MHz 固定档筛选

单次接受：2026-11-22T01:16:06.358816+08:00。镜像 `be2e047bfb1cd440101a31b39d58e9fd2a1689c863488682384d2c50b1f7ee04`。

启动计数 92/61。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=720 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=460800000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 cpu_rate_soak_long=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 52.8–69.1 | 752 |
| 0x1 | 49.3–66.6 | 752 |
| 0x100 | 46.7–73.3 | 752 |
| 0x101 | 46.7–73.9 | 752 |

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2441",
    "avg_us": "2447",
    "max_us": "2456",
    "ns_per_iteration": "2",
    "cntvct_min": "46876",
    "cntvct_avg": "46991",
    "cntvct_max": "47169",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9222",
    "avg_us": "9498",
    "max_us": "9666",
    "MBps_min": "3471",
    "MBps_avg": "3532",
    "MBps_max": "3638",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "6521",
    "avg_us": "6530",
    "max_us": "6553",
    "ns_per_iteration": "6",
    "cntvct_min": "125211",
    "cntvct_avg": "125381",
    "cntvct_max": "125814",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "12457",
    "avg_us": "12613",
    "max_us": "12708",
    "MBps_min": "2640",
    "MBps_avg": "2660",
    "MBps_max": "2693",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "6521",
    "avg_us": "6527",
    "max_us": "6540",
    "ns_per_iteration": "6",
    "cntvct_min": "125200",
    "cntvct_avg": "125321",
    "cntvct_max": "125561",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "12509",
    "avg_us": "12554",
    "max_us": "12599",
    "MBps_min": "2663",
    "MBps_avg": "2672",
    "MBps_max": "2682",
    "warm_cache": "1"
  }
]
```

600 秒性能簇双核并发，引导核仅作 housekeeping:

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
  "samples": "601",
  "elapsed_ms": "600000",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 3774 帧 / 924241 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
