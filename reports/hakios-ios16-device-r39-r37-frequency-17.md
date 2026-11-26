# R39 384 MHz 固定档筛选

单次接受：2026-11-21T17:59:14.851214+08:00。镜像 `dfbb32a42cf37863761408a79ef576c4484b66eb8137c2e25ec97e95046363a3`。

启动计数 80/49。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=384000000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_freq_diag=1
```

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|
| 0x0 | 45.1–57.6 | 134 |
| 0x1 | 41.9–67.5 | 133 |
| 0x100 | 39.7–54.4 | 134 |
| 0x101 | 39.7–55 | 133 |

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
    "max_us": "2456",
    "ns_per_iteration": "2",
    "cntvct_min": "46878",
    "cntvct_avg": "47099",
    "cntvct_max": "47169",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "0",
    "count": "5",
    "errors": "0",
    "min_us": "9109",
    "avg_us": "9355",
    "max_us": "9618",
    "MBps_min": "3488",
    "MBps_avg": "3586",
    "MBps_max": "3683",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "7844",
    "avg_us": "7859",
    "max_us": "7897",
    "ns_per_iteration": "7",
    "cntvct_min": "150594",
    "cntvct_avg": "150890",
    "cntvct_max": "151611",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "1",
    "count": "5",
    "errors": "0",
    "min_us": "13870",
    "avg_us": "13906",
    "max_us": "13937",
    "MBps_min": "2407",
    "MBps_avg": "2412",
    "MBps_max": "2419",
    "warm_cache": "1"
  },
  {
    "test": "integer",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "7830",
    "avg_us": "7844",
    "max_us": "7862",
    "ns_per_iteration": "7",
    "cntvct_min": "150324",
    "cntvct_avg": "150600",
    "cntvct_max": "150953",
    "checksum": "0x1ccc1ea74481e979"
  },
  {
    "test": "memcpy",
    "logical": "2",
    "count": "5",
    "errors": "0",
    "min_us": "13827",
    "avg_us": "13867",
    "max_us": "13931",
    "MBps_min": "2408",
    "MBps_avg": "2419",
    "MBps_max": "2426",
    "warm_cache": "1"
  }
]
```

90 秒轮换逐核负载结果：

```json
{
  "result": "1",
  "reason": "duration_complete",
  "elapsed_ms": "90000",
  "errors": "0",
  "hotstop": "0",
  "thermal_errors": "0",
  "samples": "91",
  "peak_present": "1",
  "peak_native_mC": "67500",
  "original_binding_restored": "1",
  "original_unbound": "1",
  "timer_changed": "0",
  "clocks_changed": "0",
  "voltage_changed": "0",
  "simultaneous_load": "0",
  "hard_wall_bound": "0"
}
```

USB 783 帧 / 192955 console 字节；正常 timer/drain：True；Fastboot 同设备终点：False。

轮换负载不是三个核同时负载；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。

## 返回终点与后续修订

90秒轮换三核正确性筛选通过，Fastboot终点未验证。保存日志末尾明确 `writer_contention_dropped_bytes=32`；现有 `return-fastboot/kernel.inc` 在非零源争用丢字节时返回reason0，足以阻断正常Fastboot握手。该结论是源码与同次字段的确定条件；固件具体留屏原因仍需当前照片，不把无Fastboot断言为频率失稳。此次只执行一次返回枚举空，无product、重查、重启或再boot。USB传输独立连续不等于原始printf无丢失。

主机修正新增默认关闭 `lithium_log_retry=1`，仅将有限原子锁重试64次增至4096次，保留失败计数和自动返回门禁。未放宽证明。实际rate03内核已链接；并行负载候选19未上机，其worker侧温度保护正在补强，最终包会用新独立编号。手机需先保存当前结束屏，再人工返回Fastboot。频率目标仍384MHz；460.8候选18已准备但未启动。
