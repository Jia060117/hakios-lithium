# R32 真机结案：进入事务阶段，控制器报告 NACK 与伴随错误

记录：2026-11-20T02:52:54.905476+08:00。用户明确回复“可以上机”后，主代理说明本轮QUP初始化、单次0x38/A3读取及USB采集范围。5项只读Fastboot预检通过；**2026-11-20T02:44:12.258921+08:00，冻结R32临时启动一次获接受**。本轮正常180秒timer/drain1已捕获；物理结束屏未目视确认。

## 结论与边界

本轮已越过“仅初始化观察”的停点：未改R31初始化再次返回observed，所有三次FIFO软件写入和第二RUN均按冻结顺序执行并及时返回。随后完成轮询第4次保留了STATUS **0x0d1300c8**，核心依次检查后返回`nack/status_nack`。未观测INPUT_DONE、没有RX FIFO读取、没有ID字节或accepted ID；post_rx/stop明确未执行，不能把瞬时高线/活动清零当作成功STOP。

状态错误掩码为`0x010000c8`：bit3 NACK；bits6–7是源码命名的两位FAIL字段，值3；bit24 INVALID_READ_ADDR。ERROR_FLAGS寄存器为0，不代表I2C_STATUS无错误。NACK分支优先于宽错误掩码，报告文字不能隐藏其余错误位。错误命名来自固定MiCode头与debug映射；这些瞬时状态不能判定错误地址、供电/复位、线路、电气应答或具体芯片型号，也不证明完整物理波形。

## 实测摘要

- 实际logical0/MPIDR80000001/affinity1、bootcpu1/cpus1、match1。
- 原Stage1：26读/3写/8映射/316us，clock/pin检查通过。
- 未改R31初始化：411读/18写/4poll，checked PAUSE通过；272初始化字段和20poll字段全部valid。
- 整体新核心：484读/22写/4事务poll，elapsed 1106us；事务段198us。这是内核记录的实验段，不是开机墙钟。
- 适配层：562读/22写/1额外映射；adapter_reason=none。first_failed_access=unread表示没有报告返回调用/MMIO失败地址，并不否定逻辑状态错误。
- 精确22写：原18写 + OUT_FIFO三字01827081/877181a3/00000001 + 第二RUN；全部returned_on_time1。没有服务ACK/错误W1C/结束RESET/恢复重试/GIC写/外部GPIOreset/rails/IRQ/HID。

| 采样阶段 | ERROR | STATUS | OP | STATE | IN_COUNT原值 | OUT_COUNT原值 |
| --- | --- | --- | --- | --- | --- | --- |
| pre_load0 | `0x00000000` | `0x0c110000` | `0x00000000` | `0x0000011f` | `0x00000000` | `0x00000000` |
| pre_load1 | `0x00000000` | `0x0c110000` | `0x00000010` | `0x0000011f` | `0x00000000` | `0x00000001` |
| pre_load2 | `0x00000000` | `0x0c110000` | `0x00000010` | `0x0000011f` | `0x00000000` | `0x00000002` |
| pre_run | `0x00000000` | `0x0c110000` | `0x00000010` | `0x0000011f` | `0x00000000` | `0x00000003` |
| completion | `0x00000000` | `0x0d1300c8` | `0x00000010` | `0x0000011d` | `0x00000000` | `0x00000003` |
| post_rx | unread | unread | unread | unread | unread | unread |
| stop | unread | unread | unread | unread | unread | unread |
| first_failure | `0x00000000` | `0x0d1300c8` | `0x00000010` | `0x0000011d` | `0x00000000` | `0x00000003` |

OUT_COUNT依次0/1/2/3与软件FIFO写入先后对应，单位和硬件更新语义仍未证实，不能转换为字节/ACK/已发送数量。completion与first_failure是同一保留采样的两份记录，不是两次独立MMIO观察。只保留最后一次completion poll，前三次完整状态不能还原。采样时间是绝对计数器原点微秒；不当作开机elapsed。

## 日志与结束状态

主代理独立解析和冻结接收器重放一致：**210帧/59257原始字节/52537控制台字节**，从offset0到52537，gap/duplicate/incomplete/device-lost均0；源端writer争用丢0。正常180秒timer、`drain=1 target=52488`全部保留。

接收器保留`capture_failed`/`[Errno 32] Pipe error`，结束于2026-11-20T02:47:44.093120+08:00；Pipe error发生在已成功排空的日志尾之后，未改写为接收器成功退出。只证明已输出内核USB流，不宣称全部固件/daemon/原printf输出。Data sync error2是RAM隔离包的lookup失败，未到VFS_SYNC/deviceflush，不是UFS故障证据。PID1仍为RAM诊断hold，本轮不运行SpringBoard。

console SHA256：`9b85802c338b778429cf953225c9ce7ffeaed38d82ddc7f2cde3378f368c7333`。frames SHA256：`f1d10cd1d5dbef77c0a76289aafc2e0f5cec5d4c0ea53ba100b9f88bf9262dd3`。

## 并行复核、保留与下一阶段

子代理Franklin独立解析全部初始化/guard/sample/write记录、原始错误字段与传输终态，输出`evidence/touch-r32-20261120/runtime-review/final.md/json`；主代理复核420条原日志记录的行号和字节偏移、9个源码文件/24处源码行区间及哈希，另独立完成USB帧审计、错误解码和6条Fastboot命令审计。没有一致性错误。主复核保存在本次local目录runtime-main-review.json。

下一主机修订先核对完整错误组合：固定FIFO标签、字节计数/模式、状态机顺序及原厂错误处理语义；再依据lithium原厂资料明确实际供电、外部复位、电气和所有权前提。不要直接放宽掩码、扫地址、复位/拉电源或仅延长等待；这些不是本轮已授权的补救操作。根因尚未锁定。新修订完成主机验证并说明新增范围后再申请下一次单独上机。

本轮5条只读Fastboot预检+1次临时boot；无启动后的Fastboot/ADB查询、分区查询/刷写、Android/Recovery、QEMU或自动重试。累计HakiOS56/iOS16为25；R32已消费。R20唯一显示基线、R31及全部冻结字节、原索引/暂存内容保持。没有修改生产源码或重新构建。镜像SHA256 `b260776a3fff5513134b2c594e364eb58fb95a495c9a7625e6866354345fe767`。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_transfer=1
```
