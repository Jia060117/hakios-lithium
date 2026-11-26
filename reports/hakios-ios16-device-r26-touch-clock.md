# R26 真机结果：时钟、引脚及 QUP 快照完成

2026-11-19，Asia/Shanghai。用户明确表示手机已连接、可以测试后，核对固定镜像、来源、设备身份、解锁状态、电量及下载容量，进行了一次 Fastboot 临时启动。未执行分区查询、刷写、擦除、格式化、ADB、Android/Recovery 启动、QEMU 或重试。

**本轮 Stage1 快照和已输出内核控制台流至正常 timer/drain 的核验通过；物理触摸尚未测试。** 用户随后提供R26同次原图，确认屏幕为UEFI `TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`，与USB正常timer/drain一致。原图SHA256 `e4da623f19849a375f0676409388ed7bdbecee9e4fd2786a5fc92cdd3af563c6`，原始文件及转录已归档；照片标示RAW BYTES 31,142，属于UEFI保留日志通道，不能与USB 11,050字节直接相减解释为丢帧。

## 产物与实际启动

- 镜像：`artifacts/hakios/lithium-ios16/phase4-r26-touch-clock/lithium-hakios-ios16-r26-touch-clock.img`。
- SHA256：`ff79c3ea55fb3022d997d119859b4b772fcbe233fc4364a8dc58a20c89612351`，16,269,312 字节。
- 独占 intent：20:37:45.192235+08:00；实际 Fastboot 命令20:37:45.403071发出，20:37:46.677928返回，Sending/Booting均OKAY。
- CPU实测：logical0、MPIDR0x80000001、affinity0x1、max_logical0，`bootcpu=1 cpus=1 match=1`。
- 显示内核记录：1080×2040，物理stride3240、BGR24；本轮不是桌面显示验收。

精确内嵌 payload 参数已读回；同次没有最终完整 boot-args 控制台行，CPU参数有运行记录佐证：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1
```

## R25 停点已越过

```text
LITHIUM TOUCH stage=1 inherited blsp_bcr=0x00000000 bcr=0x00000000 cmd=0x80000000 cfg=0x00000000
LITHIUM TOUCH stage=1 clock_plan cfg=0x00000000 source=XO hid=0 handoff=vendor_equivalent_0_or_1 rcg_write=0 rate_hz=unmeasured owner=unverified
LITHIUM TOUCH stage=1 clock phys=0x00325004 value=0x2000c220 polls=1
LITHIUM TOUCH stage=1 field phys=0x00330008 before=0x80000000 after=0x00000001 mask=0x00000001
LITHIUM TOUCH stage=1 clock phys=0x00330020 value=0x00000000 polls=1
LITHIUM TOUCH stage=1 qup state=0x0000011c version=0x20060000 i2c_status=0x0c000000 runtime_chip_id=unverified reset=0 transfer=0
LITHIUM TOUCH stage=1 end status=observed reason=snapshot_complete_not_touch_verified reads=26 writes=3 mappings=8 elapsed_us=314 clocks_checked=1 pins_checked=1 reset=0 transfer=0 irq_attached=0 hid_events=0
```

对应本地console第102–119行，CPU确认第126行。

| 项目 | 实际观察及范围 |
| --- | --- |
| 根时钟继承识别 | 与R25相同CFG0、初始ROOT_OFF=1；显式新路径识别通过，没有写CMD/CFG。 |
| BLSP2 AHB | 0x00352004原值0x88f8a580已含bit15，因此省略写入；状态0x2000c220符合原厂ON判据，1次轮询通过。 |
| QUP6 apps分支 | 使能字段后CBCR=1；CMD变为0，ROOT_OFF清零，1次轮询通过；后续BCR/CMD/CFG复核通过。 |
| GPIO87/88 | 各从0x1读回0xd，掩码0x23c字段符合0x0c；pull/drive等非目标字段保持。 |
| QUP | STATE/VERSION/I2C STATUS三次读取返回；VERSION是QUP控制器寄存器，不是FT5x46芯片ID。本轮未写QUP或FIFO。 |
| GPIO105/125 | CFG均0x1，单次IN均0；不能裁决哪条物理IRQ线路正确，也不能解释为中断已通过。 |
| 计数 | 26读、3写、8映射、314us；3写是apps分支及两个pin，AHB位原本已满足。 |

这些证据证明本次软件路径和返回的访问完成，不证明电源/NoC的完整配置、真实时钟Hz、长期稳定性、安全世界排他所有权、实体触摸响应或电气安全。软件超时仍不能兜底永久MMIO/屏障、external abort/SError/XPU或映射停滞。

## 诊断保持和正常结束

实际两份4MiB RAM卷分别显示Boot/md0、redwood/md1。PID1打印：

```text
LITHIUM TOUCH session state=ready pid=1 ram_only=1 timer_seconds=180
LITHIUM TOUCH session state=wait owner=kernel_loader_return_after
```

没有再次进入R25的通用RAM init自检/主动关机输出。末尾依次取得：

```text
--- loader_return_after expired ---
LITHIUM LSDB finalize begin scope=live_snapshot writers_quiesced=0 desktop=UNVERIFIED
LITHIUM DATA SYNC end error=2 phase=lookup
LITHIUM LOG writer_contention_dropped_bytes=0
LITHIUM USB: normal timer return boundary; draining before quiesce
LITHIUM USB: timer boundary drain=1 target=11001
```

`DATA SYNC error=2 phase=lookup`来自既有finalize对`/private/var`的lookup；此隔离诊断没挂Data，失败即返回，未到VFS_SYNC或设备flush，不据此认定UFS/HFS故障。源码：`ports/hakios-lithium/diagnostics/lsdb-finalize.inc` 的 `lithium_lsdb_finalize()`。

USB接收器20:41:18.497443+08:00以Pipe error结束；**其前已收到正常timer和成功drain正文**。记录接收错误，但不将其等同于panic或日志尾部丢失，也不自动重连。同次用户原图现已确认UEFI正常计时返回。

## 日志核验

- 原始传输12,586字节；文本11,050字节；48帧，起点offset0，终点11050。
- 缺口/重复/不完整帧/设备报告丢失均0；独立解析与冻结接收器一致，重解码与保存文本逐字节相同。
- timer offset10723，normal boundary offset10934，drain行offset11001；`drain=1 target=11001`在已捕获正文范围内。
- 源端writer_contention_dropped_bytes=0。验证范围是已输出的内核控制台流至timer/drain，不扩称所有UEFI、daemon及原始printf输出均完整。
- 原始帧SHA256：`66df9d27020b3f07e81b243fe5cdaecb09526b39246f4e1fc7608e31877c0ed2`。
- 文本SHA256：`53d097b29435180101dc90441a17753d75a05b10e64fb5bad383944537a4b448`。

## 保存、并行复核及下一步

私有本地目录：`reports/local/20261119-r26-touch-clock-approved/`，保留独立intent、命令原文、私有镜像副本、冻结接收器、原始frames/console、USB状态和审计。设备标识只留被忽略的本地文件。R15/R20/R25/R26冻结包未修改。

Gauss子代理只读复核Stage1准入与终态判据，并独立检查同次保存的控制台/帧；主代理另用既有 `hakios_usb_audit.audit` 重解码，复查实际字段与finalize源码。子代理不操作手机、不改源码。实际交付范围和主代理复核结果见同名JSON。

下一修订可推进**有界的最小I2C只读芯片ID事务**：先据实际QUP状态核对静默条件、FIFOs/报告协议和总线配置，再冻结候选。当前没有执行I2C START、地址发送、芯片ID读取、复位/供电、IRQ注册或HID投递。继续保持R20为显示基线。

本轮临时启动授权已消费。任何后续查询或启动仍须先说明并获对应确认；不重试R26，不刷分区，不进入Android/Recovery。回退/续接以同名checkpoint和已有冻结镜像为准。屏幕终态已由同次原图核验，无需再次启动或补测本轮。
