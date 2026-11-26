# R29 真机结案：初始化读回通过，严格 OP 门禁在事务前停止

记录：2026-11-19T23:56:29.139627+08:00。用户回复“已重新连接”后，新独立会话只读预检通过，**2026-11-19T23:50:42.918086+08:00 临时启动同一冻结镜像一次获接受**。先前空枚举记录完整保留，不重构建、不覆盖历史。

**本轮证明：一次SW_RESET写请求已返回、VALID/RESET状态和原厂I2Cv2基础配置已读回；四组OP仍为0xc0，因此在发出任何I2C事务前按既有严格门禁停止。没有ACK/NACK或真实ID结果。**

## 实测

- 实际logical0/MPIDR0x80000001/affinity0x1、bootcpu1/cpus1、match1。
- 原Stage1：26读3写8映射326us。新初始化：145读6写、2次poll、226us；含板级/GIC检查的适配层175读6写、1额外映射、adapter_reason=none。
- 六次QUP写均记录issued=1/returned_on_time=1，顺序为SW_RESET(00c)=1、STATE(004)=0、CONFIG(000)=207、ERREN(020)=3c、OPMASK(028)=300、MASTER(408)=1。
- 四组各17寄存器双轮，136次均valid，所有pass/stable mask均0x1ffff、changed0。两轮是先后读取，非原子/独占证明。
- `first_failed_access valid=0`表示没有失败的MMIO访问地址；停止原因是逻辑准入检查。`id attempted=0 raw=unread`不是芯片返回0。
- PID1在隔离RAM诊断保持，正常180秒timer和USB drain1均捕获；本包不走系统桌面流程。物理结束屏尚无目视/照片确认。

| 寄存器 | 初始化前 | SW_RESET后 | 基础配置后/ID前 | 终态 |
| --- | --- | --- | --- | --- |
| VERSION | `0x20060000` | `0x20060000` | `0x20060000` | `0x20060000` |
| STATE | `0x0000011c` | `0x0000011c` | `0x0000011c` | `0x0000011c` |
| CONFIG | `0x00000000` | `0x00000000` | `0x00000207` | `0x00000207` |
| IO_MODES | `0x000000a5` | `0x000000a5` | `0x000000a5` | `0x000000a5` |
| OPERATIONAL | `0x000000c0` | `0x000000c0` | `0x000000c0` | `0x000000c0` |
| ERROR_FLAGS | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| ERROR_FLAGS_EN | `0x0000003c` | `0x0000003c` | `0x0000003c` | `0x0000003c` |
| OPERATIONAL_MASK | `0x00000000` | `0x00000000` | `0x00000300` | `0x00000300` |
| OUT_FIFO_COUNT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| IN_FIFO_COUNT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_OUTPUT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_INPUT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_READ | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_WRITE | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MASTER_CONFIG | `0x00000000` | `0x00000000` | `0x00000001` | `0x00000001` |
| CLK_CTL | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| I2C_STATUS | `0x0c000000` | `0x0c000000` | `0x0c000000` | `0x0c000000` |

## 结论与限制

1. CONFIG0→207、OPMASK0→300、MASTER0→1，读回有效；不能再把此停点仅解释为这些字段仍未配置。CLK_CTL仍0符合尚未进入RUN/旧ID分频设置的路径，不能认定时钟故障。
2. OP在请求复位前、后、配置后和终态均0xc0，已知error0、STATUS0c000000、counts0。请求返回及RESET状态不证明全部内部状态确实清零；count/full含义及物理占用仍未验证。
3. 当前直接停止条件是自定义严格`OP==0`门禁，位于未改动ID调用之前。没有触摸芯片应答证据，不能称芯片不响应、总线不通或驱动已可用。
4. ERREN3c真实启用错误源；入口及每写前GIC enable/pending/active可见状态检查通过，但没有终态GIC专门检查，也不证明全局屏蔽或安全世界排除。无GIC写、外部触摸复位/供电切换、IRQ/HID接入。

## 完整性及终态

独立解析原始帧，并用冻结接收器重放：**125帧、34602原始字节、30602控制台字节，offset0→30602逐字节一致**。缺口/重复/残帧/device lost均0；源端争用丢字节0。正常timer及`drain=1 target=30553`已捕获。

接收器保留原始`capture_failed/[Errno32] Pipe error`，结束于2026-11-19T23:54:14.751208+08:00；该错误在成功排空记录之后。其2条serial_descriptor API异常亦保留；后续identity_verified/configuration_verified为true。接收代码只在序列号匹配后接入，未因VID/PID相同而放宽。

本次通过的是已输出内核USB流至timer/drain，不声称所有固件/daemon/printf全量。RAM Data sync error2发生在无/private/var的lookup，未到VFS_SYNC/设备flush，不能作为UFS故障。

控制台SHA256：`6ae2772f8dde8b2f746c4611be68dbcbcd61bf5240d71d0752b1603b975a8370`。原始帧SHA256：`dec9736491d0864e1410ee93b3d51b6409a5acd2b8edeaefbbfec1fc5651c1f9`。本地完整命令、私有身份及原始日志在`reports/local/20261119-r29-touch-qup-init-reconnected/`，不进入共享补丁。

## 并行复核与下一关

Aristotle在同次运行期间只读核对实际四组原值及固定MiCode/Linux状态和FIFO流程，写入`evidence/touch-r29-20261119/runtime/findings.md`；主代理独立检查其引用、源码身份及实际写序列，另行完成最终帧审计。细节与未验证项见该调查报告及本报告JSON。

下一修订应针对**RESET阶段的自定义全零OP准入条件是否符合上游状态机**，按原厂/Linux的RESET→RUN→分频→PAUSE/FIFO使用时机设计有界前后对照；保留错误、活动、数据量与各阶段full检查，不直接忽略0xc0。此为后续设计方向，不是本轮已测试结论或自动启动授权。触摸ID、报告和HID仍待验证。

## 保留与操作范围

镜像：`artifacts/hakios/lithium-ios16/phase4-r29-touch-qup-init/lithium-hakios-ios16-r29-touch-qup-init.img`，SHA256`2dec085c75a29ea770a56231ff359ae76da0d13aa36e11836d657ea1a03b9d65`；内核`d57837a730a58a3e748c2a8ed45b5035faae01cd3c17068b60a32340ef932222`。沿用42套主机测试/82打包/100源闭包的同一冻结包。

本会话5项只读Fastboot预检+1次boot；先前失败会话只有1次空枚举。无启动后设备查询、分区查询/读写/刷写、ADB、Android/Recovery、QEMU、自动重试或第二次启动。HakiOS累计53次、iOS16为22次，R29启动已消费。R20唯一显示基线、所有冻结镜像和原索引保持；代码未再修改。

精确payload参数：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_init=1
```

本阶段报告后停止，保留当前现场，无需守屏。
