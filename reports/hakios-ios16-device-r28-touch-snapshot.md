# R28 触摸 QUP/FIFO 真机快照结案

记录：2026-11-19T23:14:23.104820+08:00。本轮仅一次临时启动，2026-11-19 23:05:36（Asia/Shanghai）获 Fastboot 接受。**17 个寄存器的两轮快照均有效且一致，正常 180 秒 timer 与 USB 排空已核验；尚无 I2C 事务或真实触摸 ID。**

## 范围与保留

- 镜像：`artifacts/hakios/lithium-ios16/phase4-r28-touch-snapshot/lithium-hakios-ios16-r28-touch-snapshot.img`，16,281,600 字节。
- SHA256：`9a01c3db68164949a8f093a3625dbea9c852047b35417b1fe3597be6bf4342d1`；内核 SHA256：`34402977ea364f6823ad992c220d44bddf73406ce1800fef6ac165f710b1c983`。
- 仅执行 5 项已说明的只读 Fastboot 身份/状态核验和 1 次 `boot`。无分区查询/读写、flash、erase、ADB、Android/Recovery、QEMU、自动重试或启动后设备查询。
- 本包为隔离 RAM 诊断：`ufs=0`，固定 Mu 无物理 UFS provider，固定只读 loader，双 4MiB RAM 卷。长期 UFS 系统路线未变。
- R20-display 仍是唯一显示基线。R15/R20/R25/R26/R27/R28 冻结镜像哈希均独立复核，原索引字节保持。
- 本轮物理结束屏尚无目视/照片确认；正常返回结论仅来自已捕获内核 timer/drain，不能冒称已目视 UEFI。

## 实际结果

- `logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 bootcpu=1 cpus=1 match=1`。
- 原 Stage1：26 读、3 写、8 映射、312us；写入仍是既有时钟/引脚准备，不能说整轮启动零 MMIO 写。
- 新快照：34/34 次读取，37us；`pass0=pass1=stable=0x1ffff changed=0`。
- 新适配层含前置/GIC检查共 48 读、0 写、1 个额外映射；入口及末尾 GIC 可见状态检查通过，但不等同独占所有权证明。
- 无 FIFO 数据端口读取、QUP 写、SW_RESET、触摸复位、供电切换、IRQ 接入、HID 或 I2C 事务。
- PID1 进入 `ram_only=1 timer_seconds=180` 保持，未进入系统桌面流程。

两轮均逐项 `valid=1`；下面的零是有效读取，不是 R27 未读初始值。两轮为先后读取，不是原子锁存，也不证明整轮稳定。

| 寄存器 | 两轮相同的实测值 |
| --- | --- |
| VERSION | `0x20060000` |
| STATE | `0x0000011c` |
| CONFIG | `0x00000000` |
| IO_MODES | `0x000000a5` |
| OPERATIONAL | `0x000000c0` |
| ERROR_FLAGS | `0x00000000` |
| ERROR_FLAGS_EN | `0x0000003c` |
| OPERATIONAL_MASK | `0x00000000` |
| OUT_FIFO_COUNT | `0x00000000` |
| IN_FIFO_COUNT | `0x00000000` |
| MX_OUTPUT | `0x00000000` |
| MX_INPUT | `0x00000000` |
| MX_READ | `0x00000000` |
| MX_WRITE | `0x00000000` |
| MASTER_CONFIG | `0x00000000` |
| CLK_CTL | `0x00000000` |
| I2C_STATUS | `0x0c000000` |

## 原因判断与证据边界

1. **当前采样未具备原厂 I2C v2 事务配置。** `CONFIG=0` 没有 I2C mini-core `0x200` 和 `N=7`；`MASTER_CONFIG=0` 未开 v2 tags。不能据 CONFIG=0 断言已选 SPI。
2. `CLK_CTL=0` 尚非原厂 100kHz 策略值 `0x003e007c`，但原厂在 RESET→RUN 的相应阶段设置该寄存器，因此不能单凭继承 RESET 下的零认定时钟故障；实际输入/SCL 频率未测。
3. `IO_MODES=0xa5` 按原厂和固定 Linux v2 算法均得 IN/OUT 各 64 字节的硬件容量。它不是已占用字节数；FIFO count 的单位、锁存/读副作用未验证，不能把 count=0 等同严格空 FIFO 证明。
4. 固定 Linux SPI-QUP 将 bits6/7 命名为 OUT/IN_FIFO_FULL；本轮 `OP=0xc0`，但在继承 I2C/RESET 配置中的适用语义尚未验证。不能据此认定积压、NACK、控制器损坏或可忽略，更不能直接放宽原 ID 门禁。
5. STATUS 的 SDA/SCL 指示高，BUS_ACTIVE/BUS_MASTER 和已知错误位清，只是 idle-looking 采样；真实供电、电气、所有权、ACK/NACK 仍未验证。

主代理独立核对固定 MiCode 头第73–98/112–160行、驱动第117–145/279–295/408–420/455–470/1877–1902行；固定 Linux I2C blob 第62–75/1554–1580/1821–1856行及 SPI blob 第79–89行，并复算来源哈希。完整来源身份见 `evidence/touch-r28-20261119/protocol/findings.md`；同轮解释见 `evidence/touch-r28-20261119/runtime/findings.md`。

## 日志终态及完整性

独立帧解码与冻结接收器重放逐字节相同：67 帧、原始 17,989 字节、控制台 15,845 字节，offset 0→15845。缺口、重复、残帧、设备 lost 均 0，源端 `writer_contention_dropped_bytes=0`。

```text
--- loader_return_after expired ---
LITHIUM DATA SYNC end error=2 phase=lookup
LITHIUM LOG writer_contention_dropped_bytes=0
LITHIUM USB: normal timer return boundary; draining before quiesce
LITHIUM USB: timer boundary drain=1 target=15796
```

接收器原状态保留为 `capture_failed / [Errno 32] Pipe error`，结束于 2026-11-19T23:09:08.592396+08:00；该错误在捕获成功排空之后，未否定 timer/drain 证据。target15796 是排空记录之前的偏移，不与最终主机总数直接相减认定丢失。

Data sync errno2 为 RAM 无 `/private/var` 的 lookup，未到 VFS_SYNC 或设备 flush，不是 UFS 故障证据。此次通过的是**已输出内核 USB 流至 timer/drain**，不声称所有固件、daemon 或原始 printf 全量完整，也不以未见 panic 字样证明绝无异常。

控制台 SHA256：`fa9cc1f517c930c7ae217c830c10ed041615998383da68c4c849374f9000a2d4`；原始帧 SHA256：`39d05c5a98233fd448a770392ded5da2081f49ffeb9ab92cb8e89bf50ccedab9`。本地原始证据及审计在 `reports/local/20261119-r28-touch-snapshot-approved/`，共享补丁不包含固件/二进制和私有身份。

## 并行执行与主代理复核

Herschel（core）、Harvey（固定来源）、Tesla（打包器）、Hume（实际适配层测试）完成主机模块；明确的修正、独立测试和证据时间窗口见 prepared 报告。最终完整 HakiOS/Mu/D22AP/413job、82 来源边界、46 指令扫描、39 套 ASan/UBSan、95 合并源哈希及整包/APFS 读回通过；历史 R9 几何 hash 总门禁未解决，不冒称所有历史回归通过。

Pauli 在同次 USB 采集期间独立完成 runtime/findings.md；主代理复核全部 34 条有效读取、引用的原厂/Linux 源码窗口及哈希，另行完成最终帧审计，无需退回修正。该代理仅写调查报告，不操作设备/生产源码/索引，现已关闭。

## 下一修订与风险

下一步准备一个默认关闭的原厂 QUP v2 初始化集成：在同一个包保留 **初始化前→SW_RESET 后→配置后→ID 前→终态** 的逐寄存器有效值与读写记录；每一步有界并检查结果。不是为了每个阶段再单独安排一次纯快照测试。

只有来源支持的 reset/config/tag/mode/count/clock/error/idle 和原有严格 OP 条件均满足，才允许一次 `0x38` 地址、`0xa3` 寄存器的最小 ID 事务。若 `0xc0` 仍在且含义未补齐，保留新证据并停止，不能通过扩大掩码强行前进。

最高风险仍是 QUP 所有权/复位效应、未知 full/count 语义以及不能被软件超时约束的永久 MMIO/总线异常；随后才是控制器 ID、实际报告、IRQ 与 HID。R28 一次启动已消费，不重试、不自动开始 R29；后续上机须先说明对应集成范围。

## 本轮精确启动参数

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_snapshot=1
```

本阶段以报告及可逆补丁结案；保留手机现状，不要求用户守屏。
