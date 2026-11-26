# R38 clock-power05：S11 真机采集

单次 RAM 临时启动：2026-11-21T00:02:24.398851+08:00。镜像 `ec7f2b5dd80b7d0c468fcb6005b35c48a63b087fa8ffa77a311b07d6d7c01deb`。正常 180 秒 timer/drain，2026-11-21T00:06:29.923686+08:00 一次 devices/product 核实同设备回到 Fastboot。

| pass | 字段 | valid | raw | error | status |
|---|---|---|---|---|---|
| 0 | TYPE | 1 | 0x1c | 0 | 0x1 |
| 0 | SUBTYPE | 1 | 0x09 | 0 | 0x1 |
| 0 | RANGE | 1 | 0x00 | 0 | 0x1 |
| 0 | VSET | 1 | 0xa5 | 0 | 0x1 |
| 0 | MODE | 1 | 0x40 | 0 | 0x1 |
| 0 | STEP | 1 | 0x82 | 0 | 0x1 |
| 1 | TYPE | 1 | 0x1c | 0 | 0x1 |
| 1 | SUBTYPE | 1 | 0x09 | 0 | 0x1 |
| 1 | RANGE | 1 | 0x00 | 0 | 0x1 |
| 1 | VSET | 1 | 0xa5 | 0 | 0x1 |
| 1 | MODE | 1 | 0x40 | 0 | 0x1 |
| 1 | STEP | 1 | 0x82 | 0 | 0x1 |

有效读数 12，observer read-CMD 写 12，end error=0。未写供电寄存器或 PLL；两段完整映射已释放。

USB 156 帧 / 37401 console 字节，独立重放一致，缺口/重复/残帧/lost 均为零。接收器 Pipe error 原样保留。UEFI 内部 Prepare/Commit 不在此日志流，未另获按键事后确认。

S11 数字设定换算：905000 uV。只有两遍全部有效且一致、类型与范围匹配才换算。这是设定值，尚未证明实际模拟电压、负载下稳定性或动态控制所有权；不自动将其作为写 PLL 的授权。

代码：cpu-spmi-read-r38.h 由 Russell 实现，cpu-supply-spmi-r38.inc、真实构建/打包与运行由主代理完成，Aristotle 完成源复核；主代理逐项核对实际原值/源码和冻结哈希。

精确 boot-args：
```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_freq_diag=1
```
