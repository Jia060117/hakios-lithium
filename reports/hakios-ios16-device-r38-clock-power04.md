# R38 clock-power04：S11 真机采集

单次 RAM 临时启动：2026-11-20T23:56:51.992830+08:00。镜像 `91d4b99d57e93896ba960ae4b9c1054e5dfc3e74584c66e8c2a021c50ef52ebb`。正常 180 秒 timer/drain，2026-11-21T00:00:54.341469+08:00 一次 devices/product 核实同设备回到 Fastboot。

| pass | 字段 | valid | raw | error | status |
|---|---|---|---|---|---|
| 0 | TYPE | 1 | 0x1c | 0 | 0x1 |
| 0 | SUBTYPE | 1 | 0x09 | 0 | 0x1 |
| 0 | RANGE | 1 | 0x00 | 0 | 0x1 |
| 0 | VSET | 0 | UNREAD | 11 | 0x1 |

有效读数 3，observer read-CMD 写 4，end error=11。未写供电寄存器或 PLL；两段完整映射已释放。

USB 151 帧 / 36097 console 字节，独立重放一致，缺口/重复/残帧/lost 均为零。接收器 Pipe error 原样保留。UEFI 内部 Prepare/Commit 不在此日志流，未另获按键事后确认。

TYPE=0x1c / SUBTYPE=9 / RANGE=0 已读成功。VSET 的第二次 STATUS 读已是 DONE=1，但墙钟间隔147us，旧core先检查100us时限，返回error11/TIMEOUT且未取RDATA。因此 VSET 仍是 UNREAD，不能填0或推电压。后续将只修完成状态与期限检查顺序，保留同参数、轮询上限和失败停止。

代码：cpu-spmi-read-r38.h 由 Russell 实现，cpu-supply-spmi-r38.inc、真实构建/打包与运行由主代理完成，Aristotle 完成源复核；主代理逐项核对实际原值/源码和冻结哈希。

精确 boot-args：
```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_freq_diag=1
```
