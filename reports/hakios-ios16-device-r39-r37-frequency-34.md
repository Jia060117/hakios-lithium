# R39 537.6 MHz 固定档筛选

单次接受：2026-11-22T01:02:34.692049+08:00。镜像 `497868ccfaad78ab1f9e7ce52d865f1b1330f75c8270426de42df7cd5957bdbc`。

启动计数 91/60。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=720 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=537600000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 cpu_rate_soak_long=1 lithium_log_retry=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 51.5–69.1 | 463 |
| 0x1 | 48–65.9 | 463 |
| 0x100 | 45.8–73.9 | 463 |
| 0x101 | 45.8–75.2 | 463 |

本轮因温度上限提前停止；不能把未完成的负载记为稳定性通过，也不能将 hotstop 计数当作运算错误。各核校验原始记录保留在对应 JSON。

短基准原始字段：

```json
[
  {
    "test": "integer",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "2442",
    "avg_us": "2454",
    "max_us": "2459",
    "ns_per_iteration": "2",
    "cntvct_min": "46899",
    "cntvct_avg": "47124",
    "cntvct_max": "47220",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9303",
    "avg_us": "9531",
    "max_us": "9697",
    "MBps_min": "3460",
    "MBps_avg": "3520",
    "MBps_max": "3606",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "5580",
    "avg_us": "5596",
    "max_us": "5607",
    "ns_per_iteration": "5",
    "cntvct_min": "107142",
    "cntvct_avg": "107451",
    "cntvct_max": "107649",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "9998",
    "avg_us": "10071",
    "max_us": "10160",
    "MBps_min": "3302",
    "MBps_avg": "3331",
    "MBps_max": "3355",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "5592",
    "avg_us": "5600",
    "max_us": "5620",
    "ns_per_iteration": "5",
    "cntvct_min": "107378",
    "cntvct_avg": "107521",
    "cntvct_max": "107907",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "9970",
    "avg_us": "9991",
    "max_us": "10020",
    "MBps_min": "3348",
    "MBps_avg": "3358",
    "MBps_max": "3365",
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
  "samples": "311",
  "elapsed_ms": "310214",
  "active_mask": "0x6",
  "parent_binding_changed": "0",
  "hard_wall_bound": "0"
}
```

USB 2329 帧 / 570522 console 字节；正常 timer/drain：True；Fastboot 同设备终点：True。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。
