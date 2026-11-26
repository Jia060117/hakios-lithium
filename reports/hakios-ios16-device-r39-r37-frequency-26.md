# R39 614.4 MHz 固定档筛选

单次接受：2026-11-21T23:37:02.431390+08:00。镜像 `414b5feb024dd2ce699b22f5f550a242276411c5076e09d09b183d1d2eb576d1`。

启动计数 85/54。隔离 RAM，三颗 XNU 核，共享 S11 数字设定保持 905 mV，无电压写入；不访问物理 UFS。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=614400000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

接收器未取得内核 USB 日志。本轮 PLL/MUX 实际值、XNU 是否启动、温度、负载、timer/drain 及返回机制均未知；不能将零日志当作零错误或频率不稳定的证明。应先保留当前屏幕再恢复 Fastboot。

本轮筛选：未通过。这不是最终甜点频率或桌面长稳结论。

| 物理核 | 原生温度 min–max °C | 样本 |
|---|---:|---:|

短基准原始字段：

```json
[]
```

90 秒性能簇双核并发，引导核仅作 housekeeping:

```json
{}
```

USB 0 帧 / 0 console 字节；正常 timer/drain：False；Fastboot 同设备终点：False。

本轮未给引导核施加持续合成负载，不是三核满载验收；PMU 实际 MHz、模拟电压、校准、最终桌面体验和高档长期稳定性仍未由本轮证明。接收器 Pipe error 与完整保存字节、返回终点分开记录。

照片补证（23:47后主机归档）：phase8/valid7ff，MODEc0118000/L20/USER109/全部PLL字段及MUX5已匹配，CLKCTL50050071是旧最终谓词唯一不接受的值；锁读poll1/51us/mismatch0，11写均已返回。加载器主动retained/launch_aborted1，未进入XNU；不把该软件收尾拒绝当作频率稳定性失败。高位字段名未由已读MiCode或Linux源建立，修正版只加MUX5精确完整字，不修改硬件配置或写电压。
