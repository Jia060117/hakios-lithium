# R38 UFS low01：桌面与触摸交互测试运行中

单次临时启动在 2026-11-21T13:38:51.776318+08:00 被Fastboot接受，镜像SHA256 `bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6`。五项只读预检通过。用户已明确要求先看桌面效果，已说明正常Data文件系统写入、307.2MHz配置模型与本包不自动返回Fastboot。未刷写分区。

## 当前同次日志

截至 2026-11-21T13:43:26.740765+08:00 的保存前缀：

- XNU3of3运行；UFS Darwin APFS根挂载、Data journal replay完成。
- 显示1080×2040，stride3240，符合R20已验收基线。
- 触摸真实ID=0xe7，error=0，63字节轮询ready；SMP初始化和poll均绑定logical0/phys1。
- 触摸报告至少10200份，成功入队计数下界253。当前打印事件类型[1, 2, 3]；若0表示尚无本次用户输入证据，不能把驱动ready当成实际点击验收。
- backboardd真实touch screen service已建立。
- SpringBoard main_queue_enter在constructor后43920ms；最近sample={'sample': 5, 'elapsed_ms': 224830, 'stage': 'main_queue_callback_done', 'loop_mask': 15, 'commits': 4291}。已有持续framebuffer输出，日志窗口约6–10frames/s，非屏幕观感/刷新率或用户响应时间测量。
- 13:40:32独立重放运行前缀2181帧/488924字节，missing/duplicate/incomplete/device_lost均0，逐字节一致；不是全程终态证明。USB当前仍capturing，PID38396继续采集。

本轮没有有效PMU MHz或原生温度记录。307.2MHz来自已通过的配置模型；未提至384MHz，未写电压。UEFI寄存器初始化内部日志未进入该USB前缀，不凭目标参数声称逐写硬件读回已再次捕获。GenericARMPE在线和实际触摸绑定可见；`LITHIUM RUN A`观测线程在logical2时的match0不等于引导核选择失败。

## 用户交互与运行边界

当前保持同一运行，请试：点击应用、连续滑动、长按、后台卡片、回到桌面；比较桌面和应用内延迟。物理主屏和实际响应尚待本次用户反馈；未把日志提交当作肉眼验收。没有启动后设备查询、重新启动或重试。

UFS自动返回仍不支持；内嵌原10800秒timer保持。USB接收上限12600秒，不阻止用户即时测试。未等待timer，因此未声称最终sync/drain/返回成功。计数78/47；旧冻结与索引保持。

原始记录：`reports/local/20261121-r38-ufs-perf-low01-approved/`。关键行号见runtime-prefix-review.json；传输快照独立保存。报告为运行中前缀，收到用户交互后再补完整输入/响应结论。

## 同次交互更新

13:43后日志已出现真实DOWN/MOVE/UP，至少340条成功进入实际队列；backboardd输入服务已实际读取type1/2样本（早期printed samples有采样上限，不能由未打印UP推断UP丢失）。SpringBoard observer计数至少4291，不等同已肉眼确认4291帧。对照此前peer独立快照posted0是更早边界；本次更新不回写peer原证据。实际界面响应/流畅度待用户反馈。手机与USB保持同次运行。

## 用户体验验收与剩余瓶颈判断

用户本次明确反馈：“流畅的数倍，目前的卡顿感估计就只是来源于软件绘制的瓶颈了”。记录为真实UFS桌面交互明显改善的用户确认，数倍为主观描述而非量化benchmark。此前“待用户反馈”的条目由本节更新。

当前gl0/metal0、fb_threads1。最近12个显示统计窗口每帧驱动wall耗时min/weightedmean/max为 25.5/28.3/30.7ms，窗口帧率范围10.12–10.38/s。它包含驱动内wall时间及可能的调度等待，不是纯CPU像素耗时；低更新需求也可降低窗口帧数，不能声称显示被硬锁10fps。软件绘制是主要嫌疑之一，尚未排除低CPU档位、线程调度和用户态合成，不能宣称只剩GPU。

保留本次用户确认的UFS包与运行；后续先接原生测温再做384MHz单档，显示优化用已有CPU/wall区分和单变量worker方案。此反馈归档未增加构建/手机查询/重启/刷写；USB继续原同次采集。精确12窗数据见evidence/cpu-r38-20261121/ufs-runtime/user-feedback/feedback.json。

## 14:01 后续采集状态

原USB接收器于2026-11-21T14:01:09.099602+08:00以 `capture_failed / [Errno 32] Pipe error` 退出。保存的15349帧/3192352字节console已独立重放一致，gap/dup/残帧/device-lost均0；未见最终timer/drain。此条更新上文继续采集状态；物理画面未知，不由USB断流推断崩溃、关机或自动返回。新准备的测温和双绘制线程包尚未上机。
