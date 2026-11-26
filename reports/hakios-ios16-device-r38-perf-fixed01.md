# R38 perf-fixed01 首次定频试验

临时启动接受于 2026-11-21T00:33:44.656981+08:00，镜像 `268605b90e1b571300c53ea556d085c9fbec8118ea0ccea97aaff2991d51cea1`，只执行一次。

隔离 RAM、无物理 UFS provider；复用 power05 内核。新 loader/Mu 在 XNU CPU_ON 前核对 CPU OFF、完整 fuse、S11 数字905mV、BHS/FSM/AVS和时钟初态，随后才可能执行primary614.4MHz/main分频307.2MHz初始化。未知或失败将保留画面，不回滚或重试。默认开关关闭。

实际终态：No USB enumeration and one-shot Fastboot empty; screen needed。接收器 frames=0、enumerated=False；唯一返回核验：2026-11-21T00:37:56.105142+08:00。

本轮没有内核 USB 日志，不能判断是否到达 helper、是否发生 PLL 写、哪个前置检查停止、是否发生异常，也不能归因供电、MMIO地址或PSCI。未证明正常 timer、自动返回或实际频率改善。下一步先保存当前屏幕照片，保留全部文字；不要先重启丢失停点。取得停点后针对性修正，再使用新独立包测试。

此前 power03/power05 的映射释放修复、真实fuse/S11读取及Fastboot返回证据保持有效。此前12项S11两遍稳定：1c/09/00/a5/40/82，换算数字设定905mV；CPU最低档stock策略585mV，二者不是模拟电压实测，也不声称正常最高频率/四核已完成。

Russell实现SPMI/core接口适配与UEFI helper专项；Aristotle完成fuse解码和adapter源复核；main独立原值重算、source/hash/调用点/映射复核、真实内核及loader/Mu构建、包读回、设备生命周期与报告。超时quiesce子任务已终止，由main接手实际源码定位，未等待它阻塞流程。

无分区刷写、擦除、ADB、Android/Recovery、QEMU；无电压写。已接受启动累计73/iOS16为42。原索引及旧冻结保留，源变更通过精确补丁可回退。


## 2026-11-21 用户照片补证：停点已明确

原图已保存至 `reports/local/r37-attempts/r37-frequency-10/screen-terminal-original.jpg`，SHA256 `724811e145ae4a7ee7c4b3370346a8acf71f74362246ebdbf03fcfc6c27bffe4`。

照片明确 `core_status=4 phase=6 pll_attempts=10 core_attempts=10 observer_read_cmds=72 gates=13`。冻结API2枚举对应 READBACK_ERROR / LOCK_WAIT。FSM使能的第10写请求、立即读回均为0x00118000且valid1/wrc0/rrc0，因此停止发生在随后的轮询MODE掩码比较，不是锁超时，也不是写/读回调报错。实际触发失配的MODE未被旧结果结构保存，不能判断变化位或LOCK状态。

第11写MUX未尝试，XNU未启动；`LAUNCH_ABORTED=1`及要求手动电源+音量下是主动保留分支。原USB零帧及Fastboot未返回与此吻合，不能再写“手机停点未知”。`fault_pa=0x6480044`是退出时MUX/CLKCTL重读留下的末次地址，不是该地址外部abort证据。频率字段0是未完成编程模型的默认值，不是CPU实测0Hz。

下一候选仅在已有锁轮询读值上保存first/last/valid/次数/耗时/掩码差异，并在结束摘要输出。不增加MMIO，不放宽mask、不删断言、不写电压、不改10/11写序列或超时，也不把APC_PDN假设当已证实。源核对由Faraday完成，主代理复查冻结代码分支及pinned driver的497、510–520行；原驱动只检查LOCK，但没有充分来源给出本次未知位的动态语义。候选实际构建完成后等用户人工Fastboot，不查询当前手机。
