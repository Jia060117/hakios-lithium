# R39 384 MHz 固定档筛选

单次接受：2026-11-21T23:16:51.671592+08:00。镜像 `0fb0ed316f9c24c38ed45dc77301d393a26fc3ea0586032c1cb1021234ca6319`。

启动计数 81/50。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=384000000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 45.4–65.6 | 74 |
| 0x1 | 42.2–75.5 | 74 |
| 0x100 | 39.7–62.7 | 74 |
| 0x101 | 40–64 | 74 |

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
    "cntvct_min": "46876",
    "cntvct_avg": "47045",
    "cntvct_max": "47174",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9481",
    "avg_us": "9604",
    "max_us": "9673",
    "MBps_min": "3468",
    "MBps_avg": "3493",
    "MBps_max": "3538",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "7843",
    "avg_us": "7849",
    "max_us": "7863",
    "ns_per_iteration": "7",
    "cntvct_min": "150584",
    "cntvct_avg": "150706",
    "cntvct_max": "150973",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "13830",
    "avg_us": "13904",
    "max_us": "13949",
    "MBps_min": "2405",
    "MBps_avg": "2413",
    "MBps_max": "2426",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "7830",
    "avg_us": "7849",
    "max_us": "7866",
    "ns_per_iteration": "7",
    "cntvct_min": "150329",
    "cntvct_avg": "150710",
    "cntvct_max": "151036",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "13696",
    "avg_us": "13835",
    "max_us": "13945",
    "MBps_min": "2406",
    "MBps_avg": "2425",
    "MBps_max": "2449",
    "warm_cache": "1"
  }
]
```

90 秒三核并发负载结果:

```json
{
  "result": "3",
  "reason": "hotstop",
  "created_mask": "0x7",
  "ready_mask": "0x7",
  "done_mask": "0x7",
  "joined": "1",
  "errors": "2",
  "hotstop": "1",
  "thermal_errors": "0",
  "samples": "30",
  "elapsed_ms": "29668",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 484 帧 / 119740 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

三核绑定并发负载通过不等于长期稳定；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
