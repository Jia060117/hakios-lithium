# R39 825.6 MHz 固定档筛选

单次接受：2026-11-22T00:34:47.235520+08:00。镜像 `e3bacb2dcce7c7cd1161e2c41a8de836674d1d538d6cd23375f70223622e9f0b`。

启动计数 89/58。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=825600000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 49.9–64.7 | 88 |
| 0x1 | 46.4–62.1 | 88 |
| 0x100 | 43.8–73.6 | 88 |
| 0x101 | 44.2–75.2 | 88 |

本轮因温度上限提前停止；不能把未完成的负载记为稳定性通过，也不能将 hotstop 计数当作运算错误。各核校验原始记录保留在对应 JSON。

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2441",
    "avg_us": "2450",
    "max_us": "2457",
    "ns_per_iteration": "2",
    "cntvct_min": "46878",
    "cntvct_avg": "47038",
    "cntvct_max": "47176",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9032",
    "avg_us": "9452",
    "max_us": "9697",
    "MBps_min": "3460",
    "MBps_avg": "3549",
    "MBps_max": "3714",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "3633",
    "avg_us": "3646",
    "max_us": "3661",
    "ns_per_iteration": "3",
    "cntvct_min": "69767",
    "cntvct_avg": "70007",
    "cntvct_max": "70291",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "9644",
    "avg_us": "9768",
    "max_us": "9829",
    "MBps_min": "3413",
    "MBps_avg": "3434",
    "MBps_max": "3479",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "3634",
    "avg_us": "3652",
    "max_us": "3676",
    "ns_per_iteration": "3",
    "cntvct_min": "69768",
    "cntvct_avg": "70120",
    "cntvct_max": "70572",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "9643",
    "avg_us": "9783",
    "max_us": "9847",
    "MBps_min": "3407",
    "MBps_avg": "3429",
    "MBps_max": "3479",
    "warm_cache": "1"
  }
]
```

90 秒性能簇双核并发，引导核仅作 housekeeping:

```json
{
  "result": "3",
  "reason": "hotstop",
  "created_mask": "0x7",
  "ready_mask": "0x7",
  "done_mask": "0x7",
  "joined": "1",
  "errors": "1",
  "hotstop": "1",
  "thermal_errors": "0",
  "samples": "44",
  "elapsed_ms": "43023",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 550 帧 / 135917 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
