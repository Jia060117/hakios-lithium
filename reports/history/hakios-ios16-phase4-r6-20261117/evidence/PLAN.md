R6 最终观测（2026-11-17）：已正常定时返回 UEFI，用户确认结束摘要，桌面未通过。最终325433字节/7373帧重解码一致，缺口/重复/不完整均0；从内核横幅到正常timer返回与成功排空结果的已输出控制台流通过核验。USB随后Pipe error，UEFI摘要属于独立控制台未被USB捕获，不能宣称所有固件/daemon日志完整。SpringBoard主线程21次相同栈跨越26分36.816秒，状态WAITING、CPU累计不增，尚未进入主队列/运行循环；原20F66缓存映射为ControlCenterServices → BaseBoard BSCollectionFilter → CoreServices → __ulock_wait，具体锁持有者/根因未定。首次基础图层在内核计时1143907ms，之后完整观察30分钟；三次画行约3.29～4.79秒（含调度），末尾1754MB free。下一修订复用上游有界多线程取栈关联依赖，并同步查软件显示耗时，不再单纯加时。保持UFS系统、2880MiB/单核；全6GB、SMP、物理触摸、A530和桌面仍未验证。本次仅分析归档，未改运行代码、构建、查询或操作手机；不重试已消费R6/R5，不重刷userdata。备份archive/ios16-r5-20261117和R5历史报告保留。见reports/hakios-ios16-device-r6.md/json。

R4 最终观测（2026-11-16）：本轮已定时返回 UEFI 保留摘要，照片明确显示 TIMER DEADLINE REACHED、保留日志未见 panic 正文。USB 最终保存 206,173 字节 / 6,229 帧，从 Darwin 内核横幅覆盖到原始 launchd 与图形启动；重新解码逐字节一致，缺口/重复/不完整帧均为 0。ProgressUI 加载系统 Apple Logo，backboardd 创建真实 IOSurface，显示驱动提交 3 次实际帧；用户录像截图确认白苹果已显示在物理屏幕。SpringBoard 与 backboardd 的 System shell 连接已建立，但仍有屏幕所有权/activity assertion 与 digitizer 等待，桌面未验证。USB 最终 Pipe error，未捕获返回尾部，完整日志采集仍未通过。UFS/Data 实际挂载与读写保持通过，完整容器 RPC、物理触摸、A530、全部 6GB 与跨重启持久读回仍待验证。下一步查配件/HID 初始化等待、SpringBoard 屏幕接管和软件绘制耗时；新修正包完成冻结后再上机，不重试 R4、不重复刷 userdata。见 reports/hakios-ios16-device-r4.md/json；本次仅归档，无设备查询、启动、刷写或新构建。

R4 启动前冻结记录（历史）：完整 HakiOS、真实 D22AP loader 和 Mu 构建、46 项指令检查、实际控制台/返回摘要 ASan/UBSan 及 16 项实际包检查通过。镜像 SHA256 8a3230840e8b5636be18fa92e13cef781811e45f057632072a95b6ea2a1bad6d。本轮将实际 BSD printf 输出接入 USB，移除 kmoutput 重复采集，关闭全局 systrace 并保留 usertraps；返回摘要区分明确 timer、panic 和未知原因。R3 报告已独立归档。随后已临时启动一次并定时返回，最终结果见 reports/hakios-ios16-device-r4.md/json，不重复刷 userdata。真实桌面、容器响应、触摸、A530、完整 6GB 与跨重启持久读回仍待验证。

R3 已结束并由用户及照片确认返回 UEFI 保留摘要。USB 最终 120,934 字节/2,473 帧重新解码与日志逐字节一致，无丢失、重复或不完整帧；原始 launchd、SpringBoard、backboardd、containermanagerd 已启动，上游容器补丁已执行。屏幕摘要 RAW BYTES 252074、PANIC CAUSE NOT RECOVERED，末尾仍是 systrace 文件查询。USB 约30分钟后 Pipe error，未捕获早期内核及返回尾部，不能据此确认定时正常结束或 panic。当前修复重点是补齐控制台采集、减少全局 syscall 刷屏和明确返回原因；不重试已消费 R3，不重复刷 userdata。桌面、容器服务响应、物理触摸、GPU、全6GB与跨重启持久读回仍待验证，阶段四未完成。

R1 已返回 UEFI 结束摘要，未见桌面。原图 SHA256 6848b5be0c58382e9e695a29a495f1caf9ba01860a75bb520e904aa453383d17。可读 Stackshot Succeeded、5707 bytes / uncompressed 15680，周围二进制样式字符覆盖正文，具体 panic 原因无法从照片确认。USB 接收端读取 LANGID 失败退出，未取得控制台字节；不能记作枚举或传输通过。用户已确认返回 Fastboot。R1 不重试，修正日志显示与接收后准备新修订；未重复刷写 userdata。

# 完整 HakiOS / MIX iOS16.5 迁移

R5 最终观测（2026-11-17）：已定时返回 UEFI 保留摘要，桌面未通过。首次基础图层在内核计时1095537ms，随后保留完整30分钟；主机启动至USB终止约48分54秒。最终309344字节/6890帧重解码一致，缺口/重复/不完整均0，USB Pipe error仍缺返回尾部，完整采集未通过。SpringBoard已连接backboardd，activity assertion后来返回，虚拟HID后来有非空对象；最后仍无明确屏幕所有者、BootUI为白苹果。六次图层提交各约3.29–3.82秒（含调度），末尾约1750MB free，不能继续只归因于等待或RAM不足。下一修订先复用上游诊断获取SpringBoard初始化/主线程停点与主队列里程碑，核对启动依赖、时钟和显示耗时；CPU拓扑与UFS驻留核须对齐后再开SMP。此次只归档和只读排查，未构建、改运行代码或操作手机；不重试R5、不重刷userdata。见reports/hakios-ios16-device-r5.md/json。

2026-11-16用户批准；以完整HakiOS固定提交63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2为唯一内核。目标iOS16.5/20F66/D22AP/arm64，用户IPSW是唯一用户空间来源。系统在UFS按需读取。旧7195只读，禁用QEMU。

1. 审计已完成：reports/hakios-ios16-migration-phase1.md，保留设备差异，归档14版ABI与集成；用户已批准阶段二。
2. 内核电脑端构建已完成并获用户确认：独立校验备份与Git备份分支已完成；恢复完整上游16路径，保留MSM8996pro/lithium支持。开放全部可用RAM为目标，同步推进USB日志、BGR24、日志完整性和UFS布局边界；按正常上游流程完整构建，解释编译修复。成果及限制见reports/hakios-ios16-migration-phase2.md。
3. 用户空间阶段已完成：完整16.5用户空间、真实身份loader、更新内核/Mu和20GiB内部UFS布局均已构建核验，Data权限/读回/fsck通过。实时GPT核验后，仅向userdata执行一次Fastboot flash，30段全部成功；安装后GPT字节保持一致，System、OS/App cryptex、Data四处共32KiB头部与冻结来源一致，已返回Fastboot。采样不是全盘读回；阶段四 R3/R4 已证明 Data 实际可写挂载与写入读回，完整容器服务响应仍待验证。阶段四已获用户确认。见reports/hakios-ios16-migration-phase3.md及对应JSON。
4. 真机调试已获用户确认，正在执行：冻结同一个联合包，采集从launchd前开始的日志，观察真实SpringBoard更新。每次修复明确故障，保留变更记录、故障/结束摘要，不要求守屏；物理触摸和A530加速分别记录。完成后报告并停止。

WSL保持16GiB/12CPU/2GiB swap，任务14GiB/1200%CPU、并发12，大任务顺序执行。P1、UFS读取和旧进程存在性检查不重复验收。
内存不得简单把6GB连续上报：上部RAM、低地址保留区、UEFI、XNUP、DMA与内核页分配必须一致。当前第一扩大窗口2880MiB不是最终目标；未知动态保留区和多内存段接入仍须闭环。
历史计划与原配置在reports/history/hakios-ios14-r31-20261116及备份分支中，历史运行记录不能计作新16集成的验证。
