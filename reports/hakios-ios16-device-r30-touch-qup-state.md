# R30 真机结案：RUN 写入后 OP 清零，严格 STATUS 策略停止

记录：2026-11-20T00:51:32.725840+08:00。用户确认已进入Fastboot后，新独立预检通过；**2026-11-20T00:40:16.391122+08:00，冻结R30镜像临时启动一次获接受**。未重构建或更改上机镜像。正常180秒timer及USB drain1已捕获；物理结束屏尚未目视确认。

## 结论

R30已越过原来的基础配置与传输准备阶段。最后一次写为STATE=RUN(0x004←1)，已返回。终态有效原值为STATE0x11d（VALID及低状态1）、OP0、ERROR0、I2C_STATUS0x0c110000。

停止原因是`i2c_status_not_exact_idle`：R30在RUN过渡轮询中仍要求STATUS整值等于继承空闲值0x0c000000，检查未通过，因而在**checked RUN快照、分频及PAUSE之前**主动停止。稍后的两轮终态STATUS均多出0x00110000（bit16、bit20）；触发停止的那一次poll原STATUS未独立保存，不能保证其与终态完全相同。它不是MMIO访问失败或软件超时，没有芯片ID事务/NACK结论。终态已知错误掩码0x038000fc无置位，BUS_ACTIVE/BUS_MASTER两位亦未置位；这些瞬时读数不证明总线全程无活动。

`run_observed=0`表示完整检查未通过，不表示STATE从未成为RUN：终态确实读到0x11d。run/clock/pause三组未尝试的102字段是unread，不是硬件返回零。延长等待不会让已返回的一次性诊断继续往下执行。

## 实测与各阶段原值

- actual logical0/MPIDR0x80000001/affinity1，bootcpu1/cpus1，match1。
- 原Stage1：26读3写8映射314us，clock/pin检查通过。
- 新状态核心：270读16写、3次poll、398us；适配层330读16写、1额外映射，adapter_reason=none。首个失败访问valid=0，属于逻辑停止。
- 8个命名阶段共272条字段记录：pre/post_reset/base/setup/terminal五组各双轮17项，170项有效且各组两轮一致；run/clock/pause三组共102项unread。先后快照不是原子/独占证明。
- 精确16写前缀到首次RUN，没有divider/PAUSE/第二次RUN、软件FIFO端口/tag/ID调用、W1C、恢复复位、GIC写、外部GPIO复位/供电切换、IRQ/HID接入。

| 寄存器 | pre | SW_RESET后 | base | setup | terminal |
| --- | --- | --- | --- | --- | --- |
| VERSION | `0x20060000` | `0x20060000` | `0x20060000` | `0x20060000` | `0x20060000` |
| STATE | `0x0000011c` | `0x0000011c` | `0x0000011c` | `0x0000011c` | `0x0000011d` |
| CONFIG | `0x00000000` | `0x00000000` | `0x00000207` | `0x00000207` | `0x00000207` |
| IO_MODES | `0x000000a5` | `0x000000a5` | `0x000000a5` | `0x0000c0a5` | `0x0000c0a5` |
| OPERATIONAL | `0x000000c0` | `0x000000c0` | `0x000000c0` | `0x000000c0` | `0x00000000` |
| ERROR_FLAGS | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| ERROR_FLAGS_EN | `0x0000003c` | `0x0000003c` | `0x0000003c` | `0x00000000` | `0x00000000` |
| OPERATIONAL_MASK | `0x00000000` | `0x00000000` | `0x00000300` | `0x00000300` | `0x00000300` |
| OUT_FIFO_COUNT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| IN_FIFO_COUNT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_OUTPUT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_INPUT | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| MX_READ | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000003` | `0x00000003` |
| MX_WRITE | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000009` | `0x00000009` |
| MASTER_CONFIG | `0x00000000` | `0x00000000` | `0x00000001` | `0x00000001` | `0x00000001` |
| CLK_CTL | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |
| I2C_STATUS | `0x0c000000` | `0x0c000000` | `0x0c000000` | `0x0c000000` | `0x0c110000` |

setup→terminal仅STATE11c→11d、OPc0→0、STATUS0c000000→0c110000发生变化；其余14项一致。FIFO聚合计数保持MX_READ3/MX_WRITE9，block及FIFO占用读数0。不能把count/full读数直接当作物理FIFO或总线静默证明。

## 源码定位与下一修订

`ports/hakios-lithium/peripherals/touch-qup-state-core.h:295–303`执行整值STATUS检查；`:371–387`在检查低STATE/VALID前调用它；`:441–449`因此跳过run快照与分频；`:456–463`仅追加未再次准入的只读terminal快照。原厂固定MiCode `i2c-msm-v2.c:216–275`的状态转换以VALID/低STATE等为条件，不要求STATUS全值在各阶段始终等于继承空闲值。

这已定位到**R30自定义STATUS检查停止、且终态与原RESET空闲值不同**，但未证实失败poll瞬时值、bit16/20具体硬件语义或控制器全功能正常。下一修订先保存失败poll原值及时间，并据固定来源补足按阶段的状态检查，保留所有错误、活动、线电平、STATE及未知位的明确处理，再尝试分频/PAUSE的前后快照；不直接把差异整片屏蔽，不顺带加FIFO/ID事务。本阶段不改代码、不自动再启动。

Nash完成独立源码/日志复核；主代理逐项核对五组原值、精确16写、源码检查次序、已知错误掩码及其报告引用。独立报告在`evidence/touch-r30-20261119/runtime/findings.md/json`，主复核在`evidence/touch-r30-20261119/main/runtime-main-review.json`。不能从本轮推出时钟/芯片损坏、无应答或触摸可用。

## 日志完整性与终态

独立重解码**187帧、52482原始字节、46498控制台字节**，offset0→46498逐字节一致；缺口/重复/残帧/device-lost均0，源端writer争用丢0。正常timer及`drain=1 target=46449`在流内。接收器于2026-11-20T00:43:48.226283+08:00记录Pipe error，在成功排空之后；身份/configuration均verified，描述符错误0，保留原始失败状态而不改写为接收器成功退出。

验证范围是已输出内核USB流至timer/drain，不是所有固件/daemon/原始printf全量。RAM包Data sync error2在无/private/var的lookup阶段，未到VFS_SYNC/设备flush，不是UFS故障证据。本包故意保持RAM PID1诊断，不运行系统桌面流程。

控制台SHA256：`4a9091cf3dc19438caee0ecf1a61a8d636a3b8c6819376975d3fd806f19e34d1`。原始帧SHA256：`e2c5c40907c4cd9c69aef0de8e3330329c9a68bb3b6342e8c3ffc27bb517f9ea`。

## 保留及操作范围

镜像：`artifacts/hakios/lithium-ios16/phase4-r30-touch-qup-state/lithium-hakios-ios16-r30-touch-qup-state.img`，SHA256`fb7c575ec377dd8854786479cb1448f35974071c2ef19010b186eda1df1f92ae`；内核`2991245a0578658c77154df75236873c8dc4b2d805cb033c2ffd80a19b4c93cd`。46套回归/108打包/106源闭包的同一冻结包。

本轮5项只读Fastboot预检+1次boot，累计HakiOS54/iOS16为23；无启动后设备查询、分区查询/刷写、ADB、Android/Recovery、QEMU或自动重试。一次主机结果读取因默认GBK失败，改UTF-8重读保存文件后才启动；未重复任何手机查询。R20唯一显示基线、旧冻结字节和原索引保持。

R30启动已消费，本阶段结案后停止；无需抢拍瞬时日志。物理结束屏、芯片ID、真实触摸及IRQ/HID仍未验证。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_state=1
```
