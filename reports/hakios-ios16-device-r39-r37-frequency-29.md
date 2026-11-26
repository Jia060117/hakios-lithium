# R39 691.2 MHz 固定档筛选

单次接受：2026-11-22T00:01:00.911604+08:00。镜像 `c71594dba773379a63e26933509db862a7507b45dd4acb85f2486a3d18b80214`。

启动计数 87/56。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=691200000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 49.6–65.3 | 134 |
| 0x1 | 46.1–62.4 | 134 |
| 0x100 | 43.8–72.3 | 134 |
| 0x101 | 43.8–73.3 | 134 |

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
    "max_us": "2458",
    "ns_per_iteration": "2",
    "cntvct_min": "46877",
    "cntvct_avg": "47061",
    "cntvct_max": "47207",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9441",
    "avg_us": "9562",
    "max_us": "9668",
    "MBps_min": "3470",
    "MBps_avg": "3508",
    "MBps_max": "3554",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "4340",
    "avg_us": "4354",
    "max_us": "4363",
    "ns_per_iteration": "4",
    "cntvct_min": "83336",
    "cntvct_avg": "83603",
    "cntvct_max": "83766",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "10723",
    "avg_us": "10769",
    "max_us": "10815",
    "MBps_min": "3102",
    "MBps_avg": "3115",
    "MBps_max": "3129",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "4340",
    "avg_us": "4348",
    "max_us": "4357",
    "ns_per_iteration": "4",
    "cntvct_min": "83334",
    "cntvct_avg": "83487",
    "cntvct_max": "83661",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "10659",
    "avg_us": "10705",
    "max_us": "10792",
    "MBps_min": "3108",
    "MBps_avg": "3134",
    "MBps_max": "3147",
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

USB 783 帧 / 192329 console 字节；正常 timer/drain：True；Fastboot 同设备终点：False。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
