# R31 真机结案：QUP RESET→RUN→分频→PAUSE 观察通过

记录：2026-11-20T01:40:34.166957+08:00。用户确认Fastboot可测试，主代理说明最终实验范围后，5项只读预检通过；**2026-11-20T01:34:55.437934+08:00，冻结R31临时启动一次获接受**。无重构建、无自动重试。正常180秒timer/drain1已捕获；物理结束屏未目视确认。

## 本轮实际进展

新核心返回`observed/prefifo_pause_observed_no_new_tags`：reset/base/setup/run/clock/pause六个阶段标志均1。八组17寄存器双轮共272项全部有效，各组两轮相同，valid/stable均1ffff、changed0。四个转换poll全部20字段有效，没有failed poll。

首次RUN的真实转换poll已直接保留STATUS0x0c110000、OP0、STATE0x11d，随后成功写入并读回CLK_CTL0x003e007c，再请求并读回PAUSE STATE0x11f。R30仅在稍后terminal看到的差异，在R31中已获得转换poll/完整RUN/CLOCK/PAUSE的独立证据。完整记录证明本轮检查下的限定控制器流程走通，不证明触摸芯片应答、全硬件兼容或总线全程静默。

- actual logical0/MPIDR80000001/affinity1、bootcpu1/cpus1、match1。
- 原Stage1：26读3写8映射311us，clock/pin检查通过。
- 新核心411读18写/4poll/774us；适配层477读18写/1额外映射，adapter_reason=none。
- 18次QUP写与冻结顺序完全一致，全部issued1/returned_on_time1：一次SW_RESET、普通RESET、base/setup、首次RUN、divider、PAUSE；无第二RUN或cleanup写。
- 新软件仍无FIFO data-port/tag/ID、W1C/错误确认、总线清除、GIC写、外部GPIOreset/rails、IRQ/HID。

## 八阶段原始寄存器

十六进制原值，无0x前缀。先后快照不是原子/独占证明。

| 寄存器 | pre | post_reset | base | setup | run | clock | pause | terminal |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| VERSION | `20060000` | `20060000` | `20060000` | `20060000` | `20060000` | `20060000` | `20060000` | `20060000` |
| STATE | `0000011c` | `0000011c` | `0000011c` | `0000011c` | `0000011d` | `0000011d` | `0000011f` | `0000011f` |
| CONFIG | `00000000` | `00000000` | `00000207` | `00000207` | `00000207` | `00000207` | `00000207` | `00000207` |
| IO_MODES | `000000a5` | `000000a5` | `000000a5` | `0000c0a5` | `0000c0a5` | `0000c0a5` | `0000c0a5` | `0000c0a5` |
| OPERATIONAL | `000000c0` | `000000c0` | `000000c0` | `000000c0` | `00000000` | `00000000` | `00000000` | `00000000` |
| ERROR_FLAGS | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| ERROR_FLAGS_EN | `0000003c` | `0000003c` | `0000003c` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| OPERATIONAL_MASK | `00000000` | `00000000` | `00000300` | `00000300` | `00000300` | `00000300` | `00000300` | `00000300` |
| OUT_FIFO_COUNT | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| IN_FIFO_COUNT | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| MX_OUTPUT | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| MX_INPUT | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` |
| MX_READ | `00000000` | `00000000` | `00000000` | `00000003` | `00000003` | `00000003` | `00000003` | `00000003` |
| MX_WRITE | `00000000` | `00000000` | `00000000` | `00000009` | `00000009` | `00000009` | `00000009` | `00000009` |
| MASTER_CONFIG | `00000000` | `00000000` | `00000001` | `00000001` | `00000001` | `00000001` | `00000001` | `00000001` |
| CLK_CTL | `00000000` | `00000000` | `00000000` | `00000000` | `00000000` | `003e007c` | `003e007c` | `003e007c` |
| I2C_STATUS | `0c000000` | `0c000000` | `0c000000` | `0c000000` | `0c110000` | `0c110000` | `0c110000` | `0c110000` |

## 本轮原始转换poll

每组valid=0x1f、complete1，VERSION20060000、ERROR0。时间是既有计数器原点的us值，比较区间差值，不当作本次开机累计秒。为保留证据没有新增MMIO/time回调。

| slot | from→target | STATUS | OP | STATE | start→done us |
| --- | --- | --- | --- | --- | --- |
| 0 | 0→0 | 0x0c000000 | 0x000000c0 | 0x0000011c | 2563734518→2563734524 |
| 1 | 0→0 | 0x0c000000 | 0x000000c0 | 0x0000011c | 2563734596→2563734601 |
| 2 | 0→1 | 0x0c110000 | 0x00000000 | 0x0000011d | 2563734803→2563734808 |
| 3 | 1→3 | 0x0c110000 | 0x00000000 | 0x0000011f | 2563735118→2563735124 |

读数核对：272snapshot+119freshguard+20poll=411核心读；另9board+3entryGIC+54prewriteGIC=477物理读。无失败poll，`poll_failure valid=0 slot=unread`不等于缺日志。`terminal_gic=unread`明确只检查入口/每写前的available-view，不声称终态或全局排除。

## STATUS来源与边界

固定MiCode decoder `drivers/platform/msm/i2c-msm-prof-dbg.c:196–214`命名DAT10:3、CLK13:3、O_FSM16:3、I_FSM19:3；0x0c110000即DAT/CLK/O/I=0/0/1/2。R31只有RESET完整A0c000000、RUN/CLOCK/PAUSE完整A/B两种准入，仍拒绝其他错误/活动/未知位、线电平差异和混合FSM组合。

数值O1/I2的架构意义、普遍合法性并未由源码表证明；本轮新增的是该精确组合下的真实RUN/divider/PAUSE观察，不能扩称通用安全。终态错误标志0、STATUS已知错误及busactive/master位0、SDA/SCL采样高；这些瞬时值不证明没有继承活动或独占所有权。名义100kHzdivider读回不等于实测SCL波形。

## 日志终态与完整性

独立解析与冻结接收器重放：**209帧/58915原始字节/52227控制台字节，offset0→52227逐字节一致**；gap/duplicate/incomplete/device-lost均0，writer争用丢0。正常timer与`drain=1 target=52178`在流内。接收器保留原状态`capture_failed`/`[Errno 32] Pipe error`，结束于2026-11-20T01:38:27.270423+08:00；若为Pipe error，发生在排空记录之后，未更改为接收器成功退出。身份/configuration verified，描述符错误0。

只验证已输出内核USB流，不声称全部固件/daemon/原printf全量。Data sync error2为RAM路径lookup，未到VFS_SYNC/deviceflush，不是UFS故障。本包故意保持RAM PID1诊断，不运行SpringBoard。

console SHA256：`3d2c57f0ae4a82d85e3b705812a22858e55f4ee21cdf6a49b4244a811d7056bf`；frames SHA256：`95dd034077448b742808916d1a840b3440e89ca998a05048bc7edb4304d47547`。

## 并行复核与后续

Lorentz独立从原console解析全部272snapshot字段、4poll/20fields及18write顺序，并对照核心predicates/sourcefield；主代理重新核对其文件/行号/哈希及所有实测值，另独立完成帧/timer审计。报告见`evidence/touch-r31-20261120/runtime/findings.md/json`；主复核`main/runtime-main-review.json`。

下一主机修订可在已观测的RESET/setup/RUN/divider/PAUSE路径上设计一次最小芯片ID事务：明确原厂FIFOtags、完成/STOP证据、失败/部分返回与有界行为。保持R30/R31及旧严格ID不变，不把本轮通过扩大为FIFO/ID已经通过或新启动授权。芯片ID后才据真实型号推进报告读取和HID；本阶段结案停止。

## 操作与保留

本轮5项只读Fastboot预检+1次boot；无启动后查询、分区查询/刷写、ADB、Android/Recovery、QEMU或自动重试。累计HakiOS55/iOS16为24，R31已消费。R20唯一显示基线、所有旧冻结字节及原索引/暂存内容保持。主机49套回归/130打包/111源闭包的同一冻结包上机，镜像SHA256`a6bd21ef428e620f4cf9fe9f5d954c23c00c12d3d19efe523a70b67b2e2d0f53`。

物理结束屏/实际芯片ID/IRQ/HID/真实触摸均未验证。本轮手机无需守屏或抢拍；所有关键日志已保留。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_phase=1
```
