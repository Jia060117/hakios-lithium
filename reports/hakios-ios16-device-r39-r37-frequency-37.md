# R39 614.4 MHz 固定档筛选

单次接受：2026-11-22T00:49:08.449592+08:00。镜像 `cf35e2269a4383450d6d897da042b05cfeeab2272a98a3fb061eab8c3274584f`。

启动计数 90/59。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=720 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=614400000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 cpu_rate_soak_long=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 47.7–67.9 | 390 |
| 0x1 | 44.2–64.7 | 390 |
| 0x100 | 41.9–74.3 | 390 |
| 0x101 | 41.9–75.2 | 390 |

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
    "avg_us": "2454",
    "max_us": "2459",
    "ns_per_iteration": "2",
    "cntvct_min": "46878",
    "cntvct_avg": "47115",
    "cntvct_max": "47218",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9142",
    "avg_us": "9451",
    "max_us": "9691",
    "MBps_min": "3462",
    "MBps_avg": "3550",
    "MBps_max": "3670",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "4883",
    "avg_us": "4895",
    "max_us": "4902",
    "ns_per_iteration": "4",
    "cntvct_min": "93754",
    "cntvct_avg": "93985",
    "cntvct_max": "94117",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "10775",
    "avg_us": "10894",
    "max_us": "10947",
    "MBps_min": "3064",
    "MBps_avg": "3080",
    "MBps_max": "3113",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "4883",
    "avg_us": "4894",
    "max_us": "4899",
    "ns_per_iteration": "4",
    "cntvct_min": "93752",
    "cntvct_avg": "93965",
    "cntvct_max": "94059",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "10732",
    "avg_us": "10781",
    "max_us": "10845",
    "MBps_min": "3093",
    "MBps_avg": "3112",
    "MBps_max": "3126",
    "warm_cache": "1"
  }
]
```

600 秒性能簇双核并发，引导核仅作 housekeeping:

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
  "samples": "238",
  "elapsed_ms": "237112",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 1963 帧 / 482876 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
