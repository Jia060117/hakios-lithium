# R34：触摸严重延迟与CPU性能排查

2026-11-20，Asia/Shanghai。本报告分析仍在运行的R34 UFS同次日志；不启动新镜像，不查询设备，不改变现有频率/CPU拓扑或磁盘配置。

用户观察到几十秒才有一次界面反馈，短按表现为长按，已影响后续交互测试。记录为“触摸到达backboardd且用户报告部分界面响应，性能不可用”，不记为触摸完整验收通过。

## 本次运行的直接证据

1. 当前仅一个XNU核心在线，`bootcpu=1 cpus=1`，MPIDR `0x80000001`、affinity1、MIDR `0x512f2011`。不是尚未启动CPU调度；现有调度器已运行，容量限制为一个核心。
2. 本次启动前整数循环5次平均2473us，memcpy平均3499MB/s，RAM属性记录0xff、无translation fault，CNTFRQ为19.2MHz。与R15同快核的2477us/3510MB/s基本一致，没有重现R14慢核约162ms的整数测试。启动前基准不是当前MHz测量，也不能排除进入桌面后的降频或过热。
3. 后续非初始统计窗口360008ms内，CPU忙碌99%，backboardd48.9%、kernel_task31.2%、SpringBoard6.2%。这是运行中单核容量压力的实际证据；首个top窗口包含绝对计时原点，不采用其24%作为可比负载。
4. `MiniIOMobileFramebuffer` 典型14帧/约5.1秒，日志整数显示2帧/秒；driver内约12–14ms/帧。该driver计时不包含所有用户态合成、队列或调度成本，不能把2fps全部归因于最后的framebuffer拷贝。
5. 本次触摸生产者汇总可见：首次移动平均73ms、最多112ms；最后移动至抬起平均122ms、最多230ms。这是采样报告链上观察值，不是手指动作到屏幕的延迟；但它与后面的几十至几百秒陈旧事件明显不同。

## 短按为什么能表现成长按

同一坐标41930/47898、finger0的首个consumer DOWN和UP：

| 项目 | DOWN | UP |
| --- | --- | --- |
| 事件原始时间戳ticks | 12220639987 | 12222380614 |
| EventFilter处理观察ticks | 12879205812 | 14793411699 |
| 当时日志报告的陈旧程度 | 34.30030秒 | 133.90787秒 |

以日志中的19.2MHz counter换算，原始DOWN至UP相隔 **90.658ms**，EventFilter两次处理观察则相隔 **99.698秒**。这证实输入事件链存在严重积压或处理停顿；不能把本次短按误判仅归为手指按得久。

17:29:13已保存快照共有43条该类陈旧digitizer记录，陈旧程度34.30030至539.91437秒。它们使用实际事件时间戳；不得用改写为“当前时间”来掩盖排队，或随意丢掉UP导致触点挂住。

现有配置没有 `touch_lat=1`。仅凭当前记录，尚不能把时间严格拆为内核队列等待、输入线程未调度、同步HID dispatch阻塞和后续事件系统处理各占多少；这也是下一修订要补的最小测量。

## 下一步的范围

性能应作为当前主线推进。触摸报告已可读取，无需继续重复芯片ID/初始化测试。输入延迟与CPU/软件绘制分别排查，保留R20显示基线及R34冻结包。

R14已有三核运行证据，但phys0x100/0x101实测约比0x1慢66倍，不能直接把三个在线核当成三个正常性能核心；准确MHz及慢核原因未确定。现有上游cpufreq针对redwood EPSS，不能直接用于MSM8996。UFS固件worker占核与四核可用性由本次CPU来源调查单独核对，未完成硬件验证。

本次主代理保存的逐行数字和时间计算：`evidence/touch-live-20261120/latency-main/runtime-facts.json`。输入、CPU、绘制三路来源调查独立进行；本报告会区分来源确认与硬件未验证事项。

## 已有栈进一步缩小了输入停点

输入子代理Mencius只符号化本次两轮backboardd的 `"redwood input"` 线程：序列1在 `__ulock_wake → IOHIDVirtualServiceClientDispatchEvent+0xc8`，序列2在 `mach_msg2 → io_hideventsystem_dispatch_event_for_virtual_service → IOHIDVirtualServiceClientDispatchEvent+0xbc`。主代理读回保存流8418/15562，核对命名线程和原始地址与所交付符号记录相符；两次都落在同步派发路径。

注意第一轮是ulock **wake**，不是wait。两次取样不证明同一次调用连续阻塞数分钟，也不确定哪一个接收端/锁造成等待。实际输入循环一次read一个record，同步send返回后才读下一条，因此派发停顿会阻止随后UP进入系统；时间戳在入队时生成且一直保留，不是被消费时覆盖。

下一修订沿现有默认关闭的 `touch_lat` 增加有界记录：原时间戳、消费序号、read进入/返回、dispatch进入/返回及Boolean结果；调用前保留in-flight阶段，避免调用不返回时只留下空白。记录在定长内存中，低频限量输出，wake/button路径也覆盖。再依据结果修派发或线程/队列，不改长按阈值，不把历史时间戳改成now，不直接扩大FIFO或并发重入全局触点状态。

来源：`evidence/touch-live-20261120/latency-input/findings.md`、`input-stack-symbols.json`、`input-callsite.txt`。本阶段未改生产源码或构建新包。

## 四核与调频的实际边界

CPU子代理Bohr定位实际补丁loader：`xnuloader/loader.c:3623-3653` 排除resident core，并在UFS请求时拒绝与其冲突的kernel core；`2328-2424` 的resident worker轮询mailbox并调用UEFI ReadBlocks/WriteBlocks/FlushBlocks。主代理实际读回核心枚举分支。因此，**当前UFS实现最多三个XNU核**；四核是后续存储/固件交接架构任务，不能用 `cpus=4` 直接实现。这不是芯片本身只有三个核。

现有 `drivers/power/hakios.cpufreq.c` 开头明确限定POCO X5 Pro/redwood及其EPSS地址，主代理已读回。固定写最高频率表行也不等于已实现MSM8996自适应调速。原厂pin `1492c8e4373b7428bb0accc4f4881e97b36508b9` 的路径是 `qcom-cpufreq → clock-cpu-8996 → cluster mux/APCC PLL`，并有OPP/efuse分档、电压corner、CPR/调压依赖及按配置选择的SCM访问。不能照抄redwood寄存器或直接提高PLL。

CPU线下一步保留快核作对照，补SCTLR_EL1的I/C/M和每核同工作负载；现有MAIR/TCR/RAM属性没有单独证明每核cache enable状态。随后按本机DT/Pro分支读取有限的mux/PLL状态；物理MMIO可访问性及实时频率仍需上机验证。确认慢核恢复正常性能后，再用已实现的三核支持进行调度与响应对照。四核长期目标需先消除resident UFS占核依赖。

来源与逐行依据：`evidence/touch-live-20261120/latency-cpu/findings.md`。该子代理只收集来源、快照和报告，没有调频、regulator/SCM写入、build或手机操作。

## 绘制与日志线：先降低可避免的同步负担

绘制子代理Ohm检查实际完整路径，未找到每事件固定数秒sleep或人为2fps硬限。典型14帧/5.1秒约2.7fps，日志取整为2；同次另有51帧/5秒的窗口。driver计时位于合成之后，约12–14ms不表示整个UI帧只需这些时间。

实际 `kern_exec.c:2663-2699` 的 `applog_name=backboardd,SpringBoard` 会选中两进程强制os_log回显；源码在后续exec环境注入DT_MODE。`inject.c:61-73` 的say同步write到stderr，console TTY/ring格式化、锁和复制仍可能在调用路径中；USB只是下游异步接收，不能因此认为前端日志没有CPU/锁成本。主代理读回applog选择及say写入代码，确认这是一条具体的可优化路径；未实测它占用了多少延迟。

最小性能对照候选可以只取消 `applog_name=backboardd,SpringBoard` 的强制回显，保留applog=1、实际job stderr及有限触摸诊断；先确认job或继承环境没有另外设置DT_MODE。不同时更改CPU数、频率、分辨率、HID API或数据策略，才能判断这一个变量的收益。它是待验证提案，不宣称关闭日志即能解决本次派发停顿。

来源：`evidence/touch-live-20261120/latency-render/findings.md`。

## 本轮交付与后续顺序

并行分工：Mencius负责输入消费/选定栈，Bohr负责CPU/时钟/占核来源，Ohm负责绘制/console路径；主代理负责本次运行数字、同触点时间差、用户性能反馈和各分工关键来源复核。三路只写各自证据目录，无共享生产编辑冲突；最终报告未将提案改写为已经实现。

后续优先级为：**先解除输入派发积压及可避免日志负载，同时查慢核/cache/时钟；再验收三核收益；四核以UFS交接重构为前置**。CPU饱和和输入串行停顿可能共同作用，现有证据尚不能把全部延迟归给某一个时钟、锁或闭源函数。

本次没有再次启动或查询手机、没有更改频率/核心/分区。原同次R34 UFS与USB接收继续保留。性能验收仍未通过。
