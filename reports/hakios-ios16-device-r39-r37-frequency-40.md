# R39 460.8 MHz 固定档筛选

单次接受：2026-11-22T01:39:00.577088+08:00。镜像 `37893c281b2832225f2e00d6d0eafee0f70ea6886eaf31091432ac94dd162e8a`。

启动计数 93/62。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=1320 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=460800000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 cpu_rate_soak_long=2 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 50.6–70.4 | 1472 |
| 0x1 | 47–67.9 | 1472 |
| 0x100 | 44.5–73.9 | 1472 |
| 0x101 | 44.5–74.9 | 1472 |

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2441",
    "avg_us": "2451",
    "max_us": "2459",
    "ns_per_iteration": "2",
    "cntvct_min": "46878",
    "cntvct_avg": "47061",
    "cntvct_max": "47210",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9025",
    "avg_us": "9345",
    "max_us": "9604",
    "MBps_min": "3493",
    "MBps_avg": "3590",
    "MBps_max": "3717",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "6519",
    "avg_us": "6531",
    "max_us": "6555",
    "ns_per_iteration": "6",
    "cntvct_min": "125167",
    "cntvct_avg": "125399",
    "cntvct_max": "125865",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "12195",
    "avg_us": "12236",
    "max_us": "12296",
    "MBps_min": "2728",
    "MBps_avg": "2742",
    "MBps_max": "2751",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "6525",
    "avg_us": "6538",
    "max_us": "6556",
    "ns_per_iteration": "6",
    "cntvct_min": "125271",
    "cntvct_avg": "125530",
    "cntvct_max": "125883",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "12120",
    "avg_us": "12180",
    "max_us": "12245",
    "MBps_min": "2740",
    "MBps_avg": "2754",
    "MBps_max": "2768",
    "warm_cache": "1"
  }
]
```

1200 秒性能簇双核并发，引导核仅作 housekeeping:

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
  "samples": "1201",
  "elapsed_ms": "1200000",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 7265 帧 / 1782361 console 字节；正常 timer/drain：True；Fastboot 同设备终点：False。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
