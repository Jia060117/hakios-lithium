# R39 384 MHz 固定档筛选

单次接受：2026-11-21T23:23:25.100793+08:00。镜像 `075b911faa26d32f9bfdf80a683a35221c87bdf29d453e8efddf15ad4bd15e0a`。

启动计数 82/51。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=384000000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 46.7–55.7 | 134 |
| 0x1 | 43.5–52.8 | 134 |
| 0x100 | 41–57.6 | 134 |
| 0x101 | 41–58.2 | 134 |

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
    "max_us": "2457",
    "ns_per_iteration": "2",
    "cntvct_min": "46876",
    "cntvct_avg": "46997",
    "cntvct_max": "47178",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9409",
    "avg_us": "9488",
    "max_us": "9625",
    "MBps_min": "3486",
    "MBps_avg": "3536",
    "MBps_max": "3565",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "7824",
    "avg_us": "7837",
    "max_us": "7863",
    "ns_per_iteration": "7",
    "cntvct_min": "150229",
    "cntvct_avg": "150463",
    "cntvct_max": "150960",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "14084",
    "avg_us": "14119",
    "max_us": "14155",
    "MBps_min": "2370",
    "MBps_avg": "2376",
    "MBps_max": "2382",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "7825",
    "avg_us": "7838",
    "max_us": "7843",
    "ns_per_iteration": "7",
    "cntvct_min": "150241",
    "cntvct_avg": "150487",
    "cntvct_max": "150594",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "13989",
    "avg_us": "14041",
    "max_us": "14125",
    "MBps_min": "2375",
    "MBps_avg": "2389",
    "MBps_max": "2398",
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
  "elapsed_ms": "90000",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 783 帧 / 192367 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
