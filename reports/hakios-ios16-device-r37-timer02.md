# R37 timer02：跨核计时与返回结果

日期：2026-11-20，Asia/Shanghai。22:56:03.638364临时启动一次；23:00:04唯一devices/product检查核实同一序列号`54223ce9`、产品`MSM8996_A8`已回Fastboot，保持该模式，不再启动。两轮均未请求用户手动恢复；没有独立硬件按键观察或事后未按键确认，自动返回归因为终点支持证据，UEFI Prepare/Commit内部日志未由USB捕获。

## 实测结果

三核`0x1/0x100/0x101`的CNTFRQ均为19200000Hz。XNU timebase为625/12 ns每tick。CNTVOFF_EL2在当前EL1未读，值未知，不能记0；CNTFRQ不是CPU频率测量。

六个有向迁移对各8次，共48次。bad_cpu、internal_back、abs_back、virtual_back、physical_back均0，统计溢出0。全体间隔为0.432708–0.950416ms；五档直方图合计0/0/48/0/0，全部处于100µs至1ms。

| 物理迁移方向 | 样本 | min(ms) | avg(ms) | max(ms) | 倒退计数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `0x1` → `0x100` | 8 | 0.482968 | 0.510780 | 0.602395 | 0 |
| `0x1` → `0x101` | 8 | 0.500729 | 0.517812 | 0.574531 | 0 |
| `0x100` → `0x1` | 8 | 0.432708 | 0.447909 | 0.507968 | 0 |
| `0x100` → `0x101` | 8 | 0.881770 | 0.898078 | 0.950416 | 0 |
| `0x101` → `0x1` | 8 | 0.438750 | 0.447369 | 0.477864 | 0 |
| `0x101` → `0x100` | 8 | 0.845729 | 0.884739 | 0.897864 | 0 |

这里的正向间隔包括绑定、阻塞、调度和执行开销，不能解释成timer跳变幅度或纯迁移成本。有限采样未观察到倒退，不能证明跨核offset为0，也不能排除更小偏移、负载下问题或窗口外异常。本次隔离RAM/prelaunch，不等同于SpringBoard抽搐场景。

完整boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 timer_migrate_diag=1
```

## 正常结束和日志

正常180秒timer，`return_cpu logical=0 affinity=0x1`、armed和`drain=1 target=15612`可见。66帧/15661 console字节独立重放一致，无缺口、重复、残帧或device lost。接收器22:59:35.486946的`capture_failed / [Errno 32] Pipe error`原样保留；Fastboot终点由之后独立身份检查证明，不以USB断开代替。

RAM `DATA SYNC error=2 phase=lookup`没有访问UFS。本轮未重建，未做分区、刷写、擦除、ADB、Android/Recovery、QEMU、PLL或电压操作，没有重试。计数到本轮为67/36。

## 并行与主代理复核

Kant只读解读TIMER段，产出`evidence/cpu-r37-20261120/timer02-review/review.md/json`，无设备操作。主代理独立核对快照前缀SHA、全部6对原始字段/行号与8条原始端点快照，并重新计算各对最大abs差及625/12换算，数值一致。子报告的TIMER末行及结论编码文档问题已退回修正，不影响原始数据；生产文件没有合并冲突。

原始证据保留于`reports/local/r37-attempts/r37-timer-02/`的console.log/console.frames/usb-audit.json/return-verification.json/commands.json/result.json。

## 六项目标状态

1. 频率：部分完成，phys101两轮19.193/19.202MHz；phys1/100被各自PMU条件拒绝，未测。
2. 计时：本次48次迁移完成，未观察到倒退；CNTVOFF及独立外部时基未验证，桌面负载未覆盖。
3. SpringBoard主线程/定时器线程驻留：未实机采样，本次RAM hold不启动桌面。
4. slowcores_idle：已有默认关闭主机实现，真实桌面效果未上机。
5. 时钟/电压：已有只读来源报告`evidence/cpu-r37-20261120/clocks/r37-clock-root-cause.md`，实际mux/PLL/本机电压未知。
6. 固定频率：前置不齐，不实施或猜电压，不写动态调速器。

下一项是补齐真实UFS桌面测试结束时的写入/journal/设备flush封存与自动返回，然后用真实负载关联线程驻留并单变量验证slowcores_idle。现有10800秒UFS候选仍不满足自动返回要求，不直接启动。保留R20显示与已验证触摸基线，后续不重复这两轮RAM测试。
