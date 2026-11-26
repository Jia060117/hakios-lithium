# R39 537.6 MHz 固定档筛选

单次接受：2026-11-21T23:32:22.830509+08:00。镜像 `7476c025e1e734f0b6205cec22e2201a685623cbd17c43936755a2c659465e2d`。

启动计数 84/53。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=537600000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 49.6–61.5 | 134 |
| 0x1 | 46.1–58.6 | 134 |
| 0x100 | 43.8–65.9 | 134 |
| 0x101 | 43.8–66.9 | 134 |

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2441",
    "avg_us": "2455",
    "max_us": "2459",
    "ns_per_iteration": "2",
    "cntvct_min": "46884",
    "cntvct_avg": "47145",
    "cntvct_max": "47227",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9511",
    "avg_us": "9569",
    "max_us": "9626",
    "MBps_min": "3485",
    "MBps_avg": "3506",
    "MBps_max": "3527",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "5580",
    "avg_us": "5596",
    "max_us": "5606",
    "ns_per_iteration": "5",
    "cntvct_min": "107141",
    "cntvct_avg": "107451",
    "cntvct_max": "107642",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "11255",
    "avg_us": "11289",
    "max_us": "11328",
    "MBps_min": "2961",
    "MBps_avg": "2972",
    "MBps_max": "2981",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "5581",
    "avg_us": "5596",
    "max_us": "5607",
    "ns_per_iteration": "5",
    "cntvct_min": "107148",
    "cntvct_avg": "107449",
    "cntvct_max": "107667",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "11151",
    "avg_us": "11194",
    "max_us": "11248",
    "MBps_min": "2982",
    "MBps_avg": "2997",
    "MBps_max": "3008",
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

USB 782 帧 / 192346 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
