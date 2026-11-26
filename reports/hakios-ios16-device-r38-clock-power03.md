# R38 power03 真机结果

单次临时启动接受于 2026-11-20T23:44:20.561282+08:00。隔离 RAM、ufs=0、三核、180 秒；未修改 PLL、电压或分区。

上轮映射释放 panic 未复现。68 个供电/fuse 寄存器读数有效，两遍完全一致；六段完整 16 KiB 分配均已释放，之后继续完成 CPU benchmark。

| XNU 核 / affinity | 整数平均 µs | memcpy 平均 MB/s |
|---|---:|---:|
| logical0 / 0x1 | 2454 | 3400 |
| logical1 / 0x100 | 162146 | 107 |
| logical2 / 0x101 | 161897 | 106 |

perf primary L=0、MUX=0、AUTO=0、primary/alternate 均未锁，仍与前次约 19.2 MHz 测频相符；本轮 PMU setup=0，不新增有效 MHz 结论。供电 PGS/VREF 等仅数字配置；SAW PMIC_STS/VCTL/AVS 为零，不能据此声称已知 S11 工作电压。

正常 timer/drain=1；USB 144 帧、34668 字节独立重放一致，gap/dup/残帧/lost 均为零。接收器终态 capture_failed/Pipe error 原样保留。于 2026-11-20T23:48:48.179930+08:00 唯一一次 devices/product 核实 54223ce9 / MSM8996_A8 已在 Fastboot。未捕获固件内部 Prepare/Commit，未另获事后按键确认。

下一步补读 S11 类型、范围、VSET、模式与 step，结合真实 fuse 计算最低固定频率的 stock 要求。尚未修复频率，不以三核 RAM 测试代替四核桌面验收。

原始证据：reports/local/r37-attempts/r37-frequency-07/；解码交给 Aristotle，主代理负责运行、原始记录核对与 USB 独立重放。
