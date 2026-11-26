# R34：真实触摸读取及输入队列真机结果

日期：2026-11-20，Asia/Shanghai。用户明确授权“可以测试 R34”；本次执行一次隔离 RAM 临时启动，已结束。**真实触摸的 ID、报告读取和按下/移动/抬起入队已通过；SpringBoard 界面响应尚未验收。**

## 实测结果

| 项目 | 本轮观察 |
| --- | --- |
| 临时启动接受时间 | 17:05:15.400713+08:00 |
| 实际运行核 | logical 0，MPIDR `0x80000001`，affinity `0x1`，`bootcpu=1 cpus=1`，match=1 |
| 板级初始化 | GPIO89 低10ms、高后等待300ms；供电继承，无触摸固件或 regulator 写入 |
| ID 寄存器读取 | 0x38 / 0xA3，`error=0 byte=0xe7` |
| 持续报告读取 | 63字节；最后可见 `number=7200`，因此成功读取至少7200份 |
| 实际手指输入 | 用户点击/滑动产生 DOWN、MOVE、UP；累计成功入队197条 |
| 最后触点状态 | `points=0 posted=197 irq=1` |
| 触摸错误 | 保存流未出现 `LITHIUM TOUCH read_error`、`report_error` 或错误停止 |
| 正常结束 | `loader_return_after expired`，首次 `drain=1 target=15435` |
| 启动后 Fastboot 核验 | 17:10:56.500+08:00，约341.1秒；同序列号54223ce9，product MSM8996_A8 |

关键原始行：

```text
LITHIUM TOUCH live reset low_ms=10 high_settle_ms=300 cfg=0x00000200 io=0x00000003 supplies=inherited firmware_write=0
LITHIUM TOUCH live id error=0 byte=0xe7 status=0x0c110000 op=0x00000c40 core=0x00000000 polls=31
LITHIUM TOUCH live ready id=0xe7 report_bytes=63 sink=hakios_touch_post coordinates=normalized65535 polling=1
LITHIUM TOUCH event type=1 finger=0 x=34891 y=49954 total=139
LITHIUM TOUCH event type=2 finger=0 x=34649 y=49216 total=140
LITHIUM TOUCH event type=3 finger=0 x=33920 y=54516 total=197
LITHIUM TOUCH report number=7200 mode=0x00 points=0 posted=197 irq=1
LITHIUM USB: timer boundary drain=1 target=15435
```

主代理核对实际源码：`ports/hakios-lithium/peripherals/touch-live.inc:78` 的 `lt_live_emit` 在 sink 成功之后才递增 posted；实际 sink 在 `touch-live-events.h:245` 调用 `hakios_touch_post_batch`。因此197是已被真实输入队列接受的事件累计值，不能按采样打印的 MOVE 行数重新计数。

早期报告的原始 `points=15` 保留，不擅自解释为空闲状态或协议错误；随后实触期间见points=1，松手后见points=0。本轮可见事件均为finger=0，多指实体测试未验证。

R32 的 NACK/FAIL/INVALID_READ_ADDR 停点在本轮已越过。但R34同时修改板级复位和传输流程，不能据此宣布GPIO89就是此前错误的唯一原因。

## 启动配置

镜像：`artifacts/hakios/lithium-ios16/phase4-r34-touch-live-ram-final/lithium-hakios-ios16-r34-touch-live-ram.img`

SHA-256：`d1bbc6bf94defeacacf3ffbbae32bd3e227b70ef74bb1ceda2fb5539f8ad0d3b`

内核SHA-256：`799bdcec2e72fc6f3a0d34c810431254687ba5ef772566e851a97956fa60650c`

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch=1 touch_log=1 touch_poll=1 touch_diag=1 return_fastboot=1 return_fastboot_delay=10
```

隔离Mu没有物理UFS provider，PID1停在RAM诊断hold。`LITHIUM DATA SYNC end error=2 phase=lookup` 是隔离RAM布局中查找路径失败，不是UFS故障。

## 日志及终态

USB保存105帧、18844原始帧字节、15484控制台字节。主代理使用独立帧解析器和冻结接收器重新解码，两者及保存console逐字节一致；偏移0至15484，缺口、重复、残帧、设备声明丢失均为0，源端writer contention丢弃为0。正常timer和首次成功drain均在保存流中。

接收器在17:08:47.256267+08:00以 `capture_failed / [Errno 32] Pipe error` 结束，该状态原样保留。它不推翻已保存的连续数据，也不是Fastboot返回的证明；Fastboot终点由随后唯一一次只读枚举和product核验建立。没有重试。

本轮已核实“同一设备在临时启动后回到Fastboot”。用户被要求仅触摸屏幕并保持连接，但本次没有另行收集未按硬件键的事后确认；不借用R33的用户确认填补本轮因果记录。USB亦未保存UEFI prepare/commit的内部字段。R33的RAM自动返回已验证结论保留，UFS自动返回仍不支持。

## 分工及复核

Descartes读取本轮已保存日志，交付 `evidence/touch-live-20261120/qup/r34-runtime-note.txt`，确认ID、63字节报告、真实触点和队列投递；该短记录截至5400份报告。主代理独立核对实际源码与最终console，更新最终下限为7200份，完成USB独立重放和Fastboot终点核验。未因结案重复构建或运行历史总回归。

启动前5项只读预检、一次临时boot、启动后一次只读devices及一次product，共8条Fastboot命令。没有分区读写/刷写/擦除、ADB、Android/Recovery、QEMU、固件下载或自动重试。临时启动累计HakiOS59次、iOS16为28次。

原始记录：`reports/local/20261120-r34-touch-live-approved/`，包括commands、console、frames、usb-status、usb-audit、result及return-verification。R20/R32/R33和两份R34最终包保持冻结；原Git索引保持。结案补丁见本报告同名 `.patch`。

## 下一步

直接使用已构建的 `phase4-r34-touch-live-ufs-final` 桌面候选，验收图标点击、横向滑动、四角坐标、按住/松开及多指。无需再拆成仅初始化的测试轮次。

该候选SHA-256为 `47740664f56fcf021096493444cc96934223f49ce0dfc099920bfb73cfc426ed`，沿用R20显示和UFS参数，正常系统运行可能写入现有Data/userdata；不需要刷写分区，未启用UFS自动Fastboot。本轮授权和设备操作止于RAM短测及其终点核验，尚未启动该桌面候选。

尚未验证：backboardd/digitizer消费与可见界面响应、实体多指、屏幕边界坐标准确性、长时稳定性、真正GPIO中断方式。当前 `irq=1/0` 是GPIO读取值，不是已接入GIC的证据。
