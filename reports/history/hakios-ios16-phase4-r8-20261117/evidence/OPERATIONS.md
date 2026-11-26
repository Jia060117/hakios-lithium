R8 最终采集（2026-11-17）：正常内核 timer 与成功 USB 排空正文已捕获。最终 366347 字节/6849 帧重解码一致，缺口/重复/残帧/设备 lost 均0；有界取栈 8 轮。用户确认运行中苹果标志，物理终态未目视核验，桌面未通过。保持 UFS、2880MiB/单核；不重试R8/R7/R6/R5，不重刷userdata。用户已明确授权日志收集核验保存后直接关闭电脑，无需再询问。见 reports/hakios-ios16-device-r8.md/json。

2026-11-17 用户明确授权：“请你帮我看着，确定日志收集完毕后直接把电脑关机，无需询问”。已为同次 R8 启用持久后台监看，等待原接收器和观察器退出、正常 timer/USB 排空正文核验及日志稳定后，保存最终证据、文本归档和 Git 备份，再执行正常 Windows 关机；不使用强制关闭应用选项。状态见 reports/hakios-ios16-r8-shutdown.json，私有监看入口 reports/local/wait-ios16-r8-shutdown.py 已在运行，禁止重复启动。保持当前真机与USB采集，不重试启动或重刷userdata。

2026-11-17T04:32:13.786515+08:00，iOS16 R8联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。首次真实图形帧后保留30分钟观察窗口，总时限60分钟；无图形帧仍按30分钟结束。结束摘要保留，自动采集USB日志，无需守屏；发生明确故障则直接据此修复。

R7 最终观测（2026-11-17）：内核正常定时结束与成功USB排空正文已捕获；用户确认UEFI结束摘要，桌面未通过。最终252881字节/6699帧，原始帧重解码逐字节一致，缺口/重复/残帧/设备lost均0。多线程8轮；SpringBoard同锁/同owner关联5次，跨度1440.424秒，owner等待_LSDReadService数据库同步回复。第七轮lsd曾等待容器代码签名缓存失效同步回复；实际IPC对端与循环依赖根因未定。画行3次，墙钟[3497790, 5438626]微秒、线程CPU[2713141, 3469201]微秒。top参数截断、长printf尾部在USB前丢失、容器管理器名称漏采已确认，因此未宣称所有日志完整。保持UFS、2880MiB/单核；不重试R7/R6/R5，不重刷userdata。全6GB、SMP、A530、物理触摸及桌面未验证。见reports/hakios-ios16-device-r7.md/json。

2026-11-17T03:21:36.077402+08:00，iOS16 R7联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。首次真实图形帧后保留30分钟观察窗口，总时限60分钟；无图形帧仍按30分钟结束。结束摘要保留，自动采集USB日志，无需守屏；发生明确故障则直接据此修复。

R7 启动前准备（历史）：已完成完整 HakiOS 内核、413项用户空间任务、真实 D22AP loader/Mu 构建、检查、冻结及实时 Fastboot 身份核验，等待同次临时启动。复用固定上游 ustacks/top 同轮采样 SpringBoard、backboardd、lsd、containermanagerd、runningboardd；首次内核180秒，此后每6分钟，最多8轮、每进程96线程/14帧、每轮8进程。等待 syscall 原始寄存器和只读 unfair-lock owner_tid 用于关联持锁者，明确 sampled word 可能变化；显示前8次提交分段记录 CPU/墙钟/非CPU耗时，算法与像素保持不变。实际函数 ASan/UBSan、50项画行比较、依赖回归和实际包检查通过；诊断能力不等于锁根因或桌面已解决。保持UFS、2880MiB/单核；全6GB、SMP、A530和物理触摸未验证；不重刷userdata、不重试R6/R5。备份archive/ios16-r6-20261117和对应历史报告已核验。见reports/hakios-ios16-r7-prepared.md/json。

R6 最终观测（2026-11-17）：已正常定时返回 UEFI，用户确认结束摘要，桌面未通过。最终325433字节/7373帧重解码一致，缺口/重复/不完整均0；从内核横幅到正常timer返回与成功排空结果的已输出控制台流通过核验。USB随后Pipe error，UEFI摘要属于独立控制台未被USB捕获，不能宣称所有固件/daemon日志完整。SpringBoard主线程21次相同栈跨越26分36.816秒，状态WAITING、CPU累计不增，尚未进入主队列/运行循环；原20F66缓存映射为ControlCenterServices → BaseBoard BSCollectionFilter → CoreServices → __ulock_wait，具体锁持有者/根因未定。首次基础图层在内核计时1143907ms，之后完整观察30分钟；三次画行约3.29～4.79秒（含调度），末尾1754MB free。下一修订复用上游有界多线程取栈关联依赖，并同步查软件显示耗时，不再单纯加时。保持UFS系统、2880MiB/单核；全6GB、SMP、物理触摸、A530和桌面仍未验证。本次仅分析归档，未改运行代码、构建、查询或操作手机；不重试已消费R6/R5，不重刷userdata。备份archive/ios16-r5-20261117和R5历史报告保留。见reports/hakios-ios16-device-r6.md/json。

R6 临时启动时记录（历史）：2026-11-17T00:53:45.579696+08:00，iOS16 R6联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。首次真实图形帧后保留30分钟观察窗口，总时限60分钟；无图形帧仍按30分钟结束。结束摘要保留，自动采集USB日志，无需守屏；发生明确故障则直接据此修复。 此为启动时状态，最终采集及用户终态见本文件顶部和R6最终报告。

R6 启动前历史记录：已完成构建、冻结和实时 Fastboot 身份核验，随后已临时启动一次；最终结果见本文件顶部。完整 HakiOS 固定基线、413 项用户空间任务、真实 D22AP loader 和 Mu 均已构建；46 项指令扫描、实际函数 ASan/UBSan、依赖同步回归及16项实际包检查通过。新增构造阶段就开始的 SpringBoard 主线程采样、主队列/运行循环里程碑、显示准备与画行分段计时和正常返回前有界 USB 排空；补齐上游 sb_delay=8 与打包墙钟参数。这些是诊断与集成修正，未宣称桌面或 USB 尾部问题已解决。保持 UFS 系统、2880MiB/单核；全6GB、SMP、物理触摸和 A530 仍未验证。不重刷 userdata，不重试已消费 R5。备份 archive/ios16-r5-20261117 与 reports/history/hakios-ios16-phase4-r5-20261117 已保留。见 reports/hakios-ios16-r6-prepared.md/json。

R5 最终观测（2026-11-17）：已定时返回 UEFI 保留摘要，桌面未通过。首次基础图层在内核计时1095537ms，随后保留完整30分钟；主机启动至USB终止约48分54秒。最终309344字节/6890帧重解码一致，缺口/重复/不完整均0，USB Pipe error仍缺返回尾部，完整采集未通过。SpringBoard已连接backboardd，activity assertion后来返回，虚拟HID后来有非空对象；最后仍无明确屏幕所有者、BootUI为白苹果。六次图层提交各约3.29–3.82秒（含调度），末尾约1750MB free，不能继续只归因于等待或RAM不足。下一修订先复用上游诊断获取SpringBoard初始化/主线程停点与主队列里程碑，核对启动依赖、时钟和显示耗时；CPU拓扑与UFS驻留核须对齐后再开SMP。此次只归档和只读排查，未构建、改运行代码或操作手机；不重试R5、不重刷userdata。见reports/hakios-ios16-device-r5.md/json。

R5 早期历史快照（2026-11-16 23:26:59，UTC+8）：USB 同次快照 60,445 字节 / 2,131 帧，原始帧重解码与控制台前缀完全一致，缺口/重复/不完整均为0。从内核横幅开始，新观察预算已在真机打印，原始 launchd PID1 已初始化；该快照尚未见 early boot 完成、首次基础图层或桌面，当时采集继续；最终已结束。此为当时状态，现已结束，最终结果以上文及reports/hakios-ios16-device-r5.md/json为准。

2026-11-16T23:24:16.228707+08:00，iOS16 R5联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。首次真实图形帧后保留30分钟观察窗口，总时限60分钟；无图形帧仍按30分钟结束。结束摘要保留，自动采集USB日志，无需守屏；发生明确故障则直接据此修复。

R4 最终观测（2026-11-16）：本轮已定时返回 UEFI 保留摘要，照片明确显示 TIMER DEADLINE REACHED、保留日志未见 panic 正文。USB 最终保存 206,173 字节 / 6,229 帧，从 Darwin 内核横幅覆盖到原始 launchd 与图形启动；重新解码逐字节一致，缺口/重复/不完整帧均为 0。ProgressUI 加载系统 Apple Logo，backboardd 创建真实 IOSurface，显示驱动提交 3 次实际帧；用户录像截图确认白苹果已显示在物理屏幕。SpringBoard 与 backboardd 的 System shell 连接已建立，但仍有屏幕所有权/activity assertion 与 digitizer 等待，桌面未验证。USB 最终 Pipe error，未捕获返回尾部，完整日志采集仍未通过。UFS/Data 实际挂载与读写保持通过，完整容器 RPC、物理触摸、A530、全部 6GB 与跨重启持久读回仍待验证。下一步查配件/HID 初始化等待、SpringBoard 屏幕接管和软件绘制耗时；新修正包完成冻结后再上机，不重试 R4、不重复刷 userdata。见 reports/hakios-ios16-device-r4.md/json；本次仅归档，无设备查询、启动、刷写或新构建。

2026-11-16T22:13:46.602238+08:00，iOS16 R4联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。最长30分钟后保留上游返回摘要，无需守屏；发生明确故障则直接据此修复。

R4 启动前冻结记录（历史）：完整 HakiOS、真实 D22AP loader 和 Mu 构建、46 项指令检查、实际控制台/返回摘要 ASan/UBSan 及 16 项实际包检查通过。镜像 SHA256 8a3230840e8b5636be18fa92e13cef781811e45f057632072a95b6ea2a1bad6d。本轮将实际 BSD printf 输出接入 USB，移除 kmoutput 重复采集，关闭全局 systrace 并保留 usertraps；返回摘要区分明确 timer、panic 和未知原因。R3 报告已独立归档。随后已临时启动一次并定时返回，最终结果见 reports/hakios-ios16-device-r4.md/json，不重复刷 userdata。真实桌面、容器响应、触摸、A530、完整 6GB 与跨重启持久读回仍待验证。

R3 最终观测（2026-11-16）：R3 已结束并由用户及照片确认返回 UEFI 保留摘要。USB 最终 120,934 字节/2,473 帧重新解码与日志逐字节一致，无丢失、重复或不完整帧；原始 launchd、SpringBoard、backboardd、containermanagerd 已启动，上游容器补丁已执行。屏幕摘要 RAW BYTES 252074、PANIC CAUSE NOT RECOVERED，末尾仍是 systrace 文件查询。USB 约30分钟后 Pipe error，未捕获早期内核及返回尾部，不能据此确认定时正常结束或 panic。当前修复重点是补齐控制台采集、减少全局 syscall 刷屏和明确返回原因；不重试已消费 R3，不重复刷 userdata。桌面、容器服务响应、物理触摸、GPU、全6GB与跨重启持久读回仍待验证，阶段四未完成。

R3 20:50 实际观测：R3 已完成整包构建检查并临时启动一次。恢复上游 cctools 静态链接后，stage0/init 已执行并交接原始 iOS16.5 launchd PID1，R2 的 BAD_MACHO 停点已解除。连续 USB 确认 UFS System、OS/App cryptex 和 Data 挂载、Data 写入读回及 45 项 init 检查通过；初次冻结快照 49,118 字节/1,583 帧，无丢失或重复。用户仍见启动文字，当前保持同次采集并根据实际服务/显示故障推进；不重试已消费镜像，不重复刷 userdata。桌面、物理触摸、GPU、全6GB和跨重启持久读回仍待验证，阶段四尚未完成。 原始 launchd 随后报 Early boot complete，开始加载系统/用户域任务；服务响应和本轮终态待观察。见 reports/hakios-ios16-device-r3.json/md。

2026-11-16T20:44:55.989550+08:00，iOS16 R3联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。最长30分钟后保留上游返回摘要，无需守屏；发生明确故障则直接据此修复。

R2 清晰摘要确认 init 的 SIGILL；namespace 9 / code 1 按当前 XNU reason.h 解码为 OS_REASON_EXEC / EXEC_EXIT_REASON_BAD_MACHO，是 exec 装载失败，不能直接断言 CPU 缺指令。当前静态 init/stage0 实际由 ld64.lld 输出 MH_DYLDLINK、LC_MAIN 和 /usr/lib/dyld 依赖，与上游静态入口契约不符；改回上游 cctools 静态链接的单独构建已得到 LC_UNIXTHREAD，无 dyld 依赖，仍待整包与真机。摘要正文和回溯已可读，原图 SHA256 84bd631683d2e52919a70ce14f9036f18c5dc38cfdabdfb7ef6c69ddee14ebaa。USB 精确身份核验及 bulk IN 端点配置通过，0 帧后 Pipe error；传输未通过。用户已返回 Fastboot。下一轮只更换引导程序构建结果，UFS 原始 System/cryptex/Data 不重刷。

2026-11-16T20:30:18.258962+08:00，iOS16 R2联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。最长30分钟后保留上游返回摘要，无需守屏；发生明确故障则直接据此修复。

R1 已返回 UEFI 结束摘要，未见桌面。原图 SHA256 6848b5be0c58382e9e695a29a495f1caf9ba01860a75bb520e904aa453383d17。可读 Stackshot Succeeded、5707 bytes / uncompressed 15680，周围二进制样式字符覆盖正文，具体 panic 原因无法从照片确认。USB 接收端读取 LANGID 失败退出，未取得控制台字节；不能记作枚举或传输通过。用户已确认返回 Fastboot。R1 不重试，修正日志显示与接收后准备新修订；未重复刷写 userdata。

2026-11-16T20:10:54.523517+08:00，iOS16 R1联合包临时启动一次，发送和Booting均获Fastboot接受。使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。最长30分钟后保留上游返回摘要，无需守屏；发生明确故障则直接据此修复。

# 阶段四授权（2026-11-16）

用户明确回复“进入第四阶段真机启动与联合调试”。授权基于已安装16.5用户空间和完整HakiOS冻结成果，核验设备后临时启动联合包，采集日志并修复实际故障。阶段三userdata不重复刷写；HakiOS运行中允许Data范围的正常文件系统写入。辅助stock ADB不是HakiOS USB验收。下方“阶段四待确认”属于确认前历史状态。本阶段仍区分命令接受、运行证据和桌面验收，完成后报告。

# 阶段三已完成（2026-11-16）

完整16.5用户空间、真实D22AP身份loader、内核、Mu与UFS安装镜像已构建检查并冻结；报告见reports/hakios-ios16-migration-phase3.md及对应JSON。413项任务、原始System关键程序、Data骨架权限和文件读回/fsck通过。实时GPT的CRC、类型、位置、容量及无重叠核验通过后，仅执行一次Fastboot userdata flash，30段全部成功，耗时284.595秒。安装后GPT字节保持不变，System、OS/App cryptex、Data四处8KiB头部均与冻结来源一致，共32KiB采样；不是全盘读回。原iOS14 userdata已被20GiB内部iOS16布局替换，外层GPT未调整，无其他分区写入。手机已由辅助ADB返回可枚举的Fastboot。实际HakiOS Data挂载/写入、服务、USB日志、全6GB和桌面尚未验证。

辅助包故障已单独记录：第一次未枚举ADB，补齐漏带的本机公钥；第二次已枚举，但读取器误对UID2000只读命令施加root限制，未写入。修正为setup专用root保护、固定只读命令允许shell且强制检查两层RO后，第三次安装流程的前后采样均成功。共4次stock辅助boot命令被接受，其中3次取得ADB运行证据；它们不是活动HakiOS内核验收。HakiOS累计仍31次历史启动，iOS16启动0次。阶段三报告后停止，阶段四仍待用户确认；不自动打包启动iOS。

# 阶段三授权（2026-11-16）

用户回复“没问题，进入下一阶段”，批准阶段二报告并授权阶段三：供给16.5 IPSW核验、原始D22AP身份、上游用户空间和UFS可写Data集成、冻结包与实际设备核验后的userdata替换。阶段四iOS启动调试尚未授权，本阶段不启动iOS。下面的阶段二限制只描述其历史操作范围，已被本段阶段三授权覆盖。

# 阶段二构建报告（2026-11-16）

完整HakiOS iOS16内核、Mu固件和占位身份loader已构建并检查；内存首个窗口2880MiB，6GB全部可用尚未实现；USB日志源码及主机工具已编译/检查，枚举与传输未验证。见reports/hakios-ios16-migration-phase2.md。无手机查询、启动或分区写入，累计仍31次历史HakiOS启动。当前手机物理状态未重查，userdata仍按最后证据记录为历史iOS14 HFSX。按用户逐阶段要求停止；阶段三需确认，阶段四未授权开始。不要启动占位loader或旧R31包。

# 2026-11-16迁移阶段约束（覆盖下方历史目标）

用户已批准iOS16.5/20F66迁移阶段二，包含内核、全部可用RAM目标和USB日志集成。当前仅电脑端归档、修改、构建与检查，不查询/启动手机、不写分区。每阶段报告后停止。既有userdata操作授权保留，阶段三实施时重新核验设备/几何/冻结包。当前手机userdata仍是历史iOS14 HFSX，不能假设可返回Android；R31单次入口已消费，禁止重试。
备份分支archive/ios14-r31-20261116与E盘独立校验备份已完成。详细清单reports/local/ios16-migration-phase2/backup.json。

以下为保留的原操作授权与历史证据：

# 设备操作授权与当前约束

更新：2026-11-16。仅适用于本项目连接并核验身份的小米 MIX 一代 lithium。

R31同次10张运行照片确认launchd[1]完成early boot并退出bootstrap/ondemand模式，SpringBoard[12]执行实际请求，backboardd[11]启动并到达HID和window server初始化。最新明确故障是backboardd向containermanagerd请求共享系统容器时反复无效连接，container error51/XPC_INVALID_REPLY后得到空路径，触发NSURL initFileURLWithPath:nil string parameter异常并以SIGABRT退出。原始18H17共享缓存结合本轮slide0xb5d0000核对，两帧准确落在BaseBoard的BSSystemContainerForCurrentProcessPathProvider libraryPath/cachesPath。冻结包已包含containermanagerd配置和原始程序；容器服务为何未应答仍待诊断，不能直接归因为漏包、persona或目录缺失。上游16.5修补使用的MCMUserIdentityCache/personasAreSupported符号在原始14.8 MobileContainerManager中未找到，禁止直接套用16版地址。两项MediaConversionService原始Info.plist及程序存在，XPCService本来为false，其Invalid XPCService dictionary不是已确认的主故障。另记录type4 persona查找失败及HID bootstrap权限拒绝；checkerboard仅匹配diagnostics、cryptexd要求research-enabled，硬件条件排除不是UFS格式故障。控制台按行回顶部覆盖，旧文字仍留在下方，不能据此认定整机或launchd循环重启。9个可读timer样本从110151ms/14562次PPI27推进到710813ms/101005次，均stalled0；未见kernel panic或桌面。本组照片未见上一轮SpringBoardUI.m:56断言或type6错误，但尚无IDENTITY FINAL，不能宣布权限/身份修复已通过。此次仅归档和原始镜像/冻结源码只读分析，未查询或启动手机、未写分区；累计31次，R31入口已消费。下一步核对完整containermanagerd实际启动/退出、Mach服务和凭据、原始14版容器接口及Data目录，结合本轮结束摘要处理显示/HID后续停点；保持UFS/HFSX与完整HakiOS基线。 见reports/hakios-device-r31.md。

2026-11-16T04:54:23.739584+08:00，R31 XML权限与persona联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。完整HakiOS重新编译，接入原始签名XML读取与完整persona初始化；系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计31次HakiOS临时启动；本轮未写分区。R31入口已消费，不自动重试。 见reports/hakios-device-r31.md。

R31已修正完整HakiOS中原始XML权限读取的接口集成，并在lithium_ui启动前复用完整persona子系统创建type6身份。原始18H17 SpringBoard签名包含com.apple.springboard-ui.client，XML特殊槽哈希已核验；原始SecTask使用csops op7/ERANGE读取。不修改Apple程序或共享缓存，不绕过断言，不将权限查询改为全部允许。完整HakiOS重新编译，三个实际对象文件确认新增调用已接入。17项实际适配函数和保留摘要的ASan/UBSan检查通过；实际APFS读回确认335项plist0644、目录和程序0755、uid/gid0，R30根卷文件内容保持。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `2af949c5c1a8d958720b28e2e3f6b9a54c9e60cd99c83b43e0116652d33d7efd`。本次准备未查询或启动手机、未写分区，累计仍30次；R31权限、身份和UI运行结果待真机。结束摘要新增LITHIUM IDENTITY FINAL，保留SpringBoard实际XML探测/复制及persona查询结果。

R31入口核验设备后只临时启动冻结镜像一次；保留R29/R30已消费guard和全部冻结证据，不自动重试。无格式化或分区写入。

R30运行中三张照片确认原始launchd[1]完成early boot并进入system bootstrap，随后原始SpringBoard在+[SpringBoardUI load]、SpringBoardUI.m:56触发NSInternalInconsistencyException；PID11、20、21反复abort，桌面初始化失败，继续等待不能消除相同断言。照片中的0x1c9469ebc减同次cache slide0x23f68000，准确落在原始18H17的+[SpringBoardUI load]+0x1a0；原始代码首先引用com.apple.springboard-ui.client权限标记，但尚未观测具体运行时查询结果，不能将缺失entitlement写为已确认根因。同时多项服务报persona type6查询失败。缺launchd.plist/NRN cache后仍继续early boot，不可当作此次明确崩溃的直接原因。三个timer样本50.069、110.137、170.161秒均stalled0，照片未见kernel panic、结束摘要、服务诊断结果或图形界面；运行结论仍限于这些同次证据。本次仅归档与只读原镜像分析，无手机查询、启动或分区写入，累计仍30次。R30入口已消费。下一步优先核对SpringBoard早期进程权限查询与上游集成，再处理persona兼容；保留现有UFS布局和完整HakiOS基线。 见reports/hakios-device-r30.md。

2026-11-16T04:09:33.061024+08:00，R30权限修正联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。复用R29同字节完整HakiOS内核与loader，系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计30次HakiOS临时启动；本轮未写分区。R30入口已消费，不自动重试。 见reports/hakios-device-r30.md。

R30联合包已修正R29任务权限缺陷并独立解包冻结。实际APFS读回确认335项plist为0644、目录0755、程序和适配库0755，uid/gid均为0。真实R29坏镜像拒绝、上游writer/reader读回及异常所有者/目录/执行权限共7项检查通过。复用R29已完整编译且已真机执行的同字节HakiOS内核与loader。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `e9a2f7d5b2d8fe97e0964f324a12275bec61dbb9ee45b7502679968505c21104`。本次准备未启动手机、未写分区，累计仍29次；R30服务及UI结果待真机。

R30入口核验设备后只临时启动冻结镜像一次；保留R29已消费guard和全部冻结证据，不自动重试。无格式化或分区写入。

R29用户运行中照片确认原始launchd[1]反复拒绝system域任务，报error122：Path had bad ownership/permissions，其中包括com.hakios.lithium.svc_test。独立读取同次冻结包的真实APFS inode确认：335项任务配置和LaunchDaemons目录均为0777，uid/gid均为0；问题是组/其他用户可写的权限，而非非root所有者。Windows暂存目录的0777经copytree保留进ext4并进入镜像。此前逐项读回仅证明输入与镜像一致，漏查launchd所需的权限语义，属于本轮打包缺陷。延长等待不能消除此拒绝。原图、局部转录、inode清单和745项冻结文件已核验。照片不是最终摘要；未观察到界面、诊断成功、panic或正常返回。本次归档无设备操作，累计仍29次；下一步修正联合根卷目录0755、plist0644及可执行文件0755，按上游root所有者规则打包并直接核验镜像元数据，不修改UFS布局，不绕过launchd权限检查。 见reports/hakios-r29-permissions-analysis.md。

2026-11-16 03:54:43（北京时间），按用户“那就上机吧，手机已连接”的指令，重新核验冻结镜像及授权MIX的Fastboot身份、解锁、userdata容量、电池启动条件与下载容量后，执行R29联合UI临时启动一次。发送与Booting均返回OKAY；尚未收到同次屏幕结果，不能据此认定内核、服务或桌面运行通过。同包尝试服务诊断、backboardd、SpringBoard和显示，物理触摸采样未接通、A530加速未验证。累计29次HakiOS临时启动，本轮无分区写入、旧7195或QEMU执行。R29入口已消耗，不自动重试。 见reports/hakios-device-r29.md。

联合 iOS14 界面测试包已完成完整 HakiOS 编译、检查、独立解包并冻结。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `d98b24d5e502c27c15049791178d1b4350d9c416b15c879198956802e73b8794`。本次准备未启动手机、未写分区，累计仍28次。

默认 `python scripts/boot-hakios-ui.py` 仅核验设备；`--boot` 在既有授权内只临时启动冻结镜像一次，记录意图并禁止自动重试。无格式化或分区写入。最终冻结报告为 reports/hakios-ios14-ui-ready.json；先前准备报告是历史快照。

2026-11-16 03:51:59（北京时间）执行联合包默认只读核验：授权MIX处于Fastboot，身份、解锁、userdata几何、电池启动条件和下载容量均通过，未发出boot或分区写命令。当前可执行一次冻结包联合启动；累计仍28次。见reports/hakios-ui-device-preflight.json。

**当前授权与状态：R28同次结束照片确认LAUNCHRESULT PASS：原始ps在6487ms正常exit0、signal0、timeout0，无清理kill；首次完整快照同时找到PID1 launchd、notifyd和cfprefsd。观察器在自身入口后8045ms完成检查，不能解释为整个iOS在8秒启动。硬件计时181310ms、PPI27增量24934、3次采样停滞0次，按预定时限正常返回，无panic。检查通过后诊断任务继续等待，日志继续滚动不表示这项检查尚未完成。原图、转录及199项冻结证据已核验，此进程存在性子项结案，无需延长或重复。累计仍28次HakiOS临时启动；本次仅归档，无设备查询、启动或分区写入。 后续按最新联合路线推进实际服务请求、显示、物理触摸和SpringBoard，同包保留结果。P2完整服务、图形、触摸和SpringBoard仍未通过；系统继续UFS/HFSX按需读取。R28单次入口已消耗，不再运行。见reports/hakios-device-r28.md。**

旧7195历史状态（2026-11-14）：第十六轮照片已确认根卷以可写、journaled方式挂载并越过mount-phase-1；mount-phase-2因必需的FactoryData源目录缺失退出66，launchd主动触发panic。另已定位HFS新版同步ioctl未被驱动处理的ENOTTY警告。分析见 `reports/ios14-mount-phase2-analysis.md`。该历史轮物理UFS只读，RAM写入不跨重启保存；userdata仍为iOS14.8 HFSX。此前误入MIUI后的完整16GiB读回已与原镜像一致。

**2026-11-14 用户已确认新路线并要求写为最高项目级约束：基于完整 HakiOS 移植增加 MIX 一代设备支持，HakiOS 为唯一活动内核基线；原先7195内核只作只读对照，不再修改、构建、打包、启动或兜底。权威指令见 AGENTS.md，现行计划见 PLAN.md。**

新路线已完成工程重构、完整HakiOS依赖/内核、Mu板层/loader/hello及首包静态核验，记录见 reports/hakios-platform-test-ready.md。2026-11-14 20:06重新核验原授权MIX身份、解锁及userdata容量后，执行一次HakiOS首包临时启动，Fastboot返回OKAY；用户报告立即自动重启并进入MIUI启动画面。未取得UEFI/内核/hello执行证据，不能将命令接受计为运行通过。后续原授权手机重新出现在Fastboot，ADB为空；回到Fastboot的具体方式未知。输入、命令及观察见 reports/hakios-device-r1.md 和 reports/hakios-device-r1.json。本轮没有刷写、清除、格式化或重分区，未运行QEMU或旧7195内核。下一轮沿HakiOS入口定位早期加载停点。

20:19第二轮HakiOS临时启动前，用户确认已稳定进入Fastboot，脚本重新完成身份/解锁/容量核对。Fastboot接受新诊断包；用户报告未辨识文字闪过，随后确认重新出现MIUI启动画面。后续USB查询再次看到原授权设备处于Fastboot，回到Fastboot的具体方式未知。记录见 reports/hakios-device-r2.md；本轮未取得UEFI/内核/hello证据，没有分区写入。

20:30第三轮把现有Mu日志显示在屏幕，并启用上游断言停机选项。用户照片证实UEFI、MMU/HOB、BDS、GOP和EFI文件校验，随后显示HAKIOS LOADER FAILED；日志范围与loader首选PE地址精确匹配，尚无最终API返回码。记录见 reports/hakios-device-r3.md。用户随后明确确认以电源＋音量下返回Fastboot并保持连接。

20:43第四轮将loader首选地址设为已有低地址窗口并增加标准加载/内存日志，核验身份后临时boot获Fastboot接受。用户照片明确显示LoadImage返回 `0x8000000000000009` / EFI_OUT_OF_RESOURCES，最大连续空闲块小于约9.4 MiB的loader内存映像。未执行StartImage，内核和hello仍未验证。记录见 reports/hakios-device-r4.md。用户提供照片后确认“已进入”Fastboot；下一轮将核验身份后测试独立高位DDR UEFI堆。累计四次HakiOS临时boot，均没有分区写入，未运行旧7195内核或QEMU。

20:55第五轮在独立高位DDR中增加64 MiB UEFI堆，静态核验实测RAM及10个保留范围/动态分配窗口后，核验身份并临时boot，Fastboot接受。用户报告“这次直接重启进miui了”，没有可辨识日志，不能认定进入loader或内核。记录见 reports/hakios-device-r5.md。累计五次HakiOS临时boot，均没有分区写入。下一轮增加可拍照的LoadImage/StartImage延时，并沿现有loader异常回调保留错误日志。

21:06第六轮增加LoadImage前5秒和成功后20秒停留，并让lithium的上游loader异常回调保留日志。核验身份后临时boot获Fastboot接受；两张用户照片确认LoadImage成功、loader实际进入且装载于 `0x127695000`，内核/设备树/boot args已准备。最终在实际内核分支前的栈缓存清理触发UEFI异常：ELR偏移 `0x5574` 对应 `dc cvac, x10`，ESR `0x96000147`，FAR `0x415d0000` 为未映射的栈上边界。未取得内核入口或hello证据。记录见 reports/hakios-device-r6.md。用户随后明确确认已返回Fastboot，供两处栈清理边界修正后复测。累计六次HakiOS临时boot，均没有分区写入。

21:29:33第七轮修正两处栈清理边界，核验身份后临时boot获接受。四张照片确认内核/hello执行，memory日志增加后panic返回loader；具体panic原因未拍到，到期退出仅是首要假设。用户后来明确确认用电源＋音量下回Fastboot，供第八轮测试。记录见 reports/hakios-device-r7.md。累计七次HakiOS临时boot，没有分区写入。

第八轮（2026-11-14 23:43:29北京时间）在用户确认Fastboot后核验身份、解锁和userdata容量，临时boot获接受。第八轮三张照片确认同包HakiOS内核和上游PID1 hello执行，九组实际PPI27计数递增至31733；最终600次select成功、errors=0、elapsed_ms=602287、nvcsw=600、nivcsw=6，PID1保持存活并正常定时返回loader，返回摘要清晰。终屏未显示最后timer计数，独立外部计时尚未核验，故完整10分钟timer/线程验收及连续3次复现仍待补齐；iOS14、GPU和触摸未验证。 原照片和SHA256已保存，见 reports/hakios-device-r8.json。用户随后报告“手机没电了，我正在充电，你先准备好镜像吧”；未确认再次回Fastboot。当前只在电脑上准备并校验同一冻结镜像复测副本，未发出新的boot或分区写入命令。累计八次HakiOS临时boot。

第九轮（2026-11-15 00:04:23北京时间）核验授权MIX身份、解锁和userdata容量后，用同一R8镜像临时启动获接受，电池启动条件yes。用户确认Hello和日志增加，后来明确说明停在结束文字摘要，能确认没有panic；准确timer/select数值未捕获，不据此通过完整验收，见 reports/hakios-device-r9.json。用户要求取消守屏抢拍流程，已准备自动保留最终测量的新包，待真机复测。第九轮结束后的USB查询Fastboot和ADB均为空；用户后来确认回到Fastboot，供第十轮测试。累计九次HakiOS临时boot，没有分区写入、旧7195或QEMU执行。

第十轮（2026-11-15 00:52:18北京时间）在新包完整构建、静态核验和10项回归后，重新核验授权MIX身份、解锁和userdata容量，电池启动条件yes，临时boot获接受。第十轮结束照片确认新包自动采集和停屏保留：硬件计时660003ms，实际PPI27由0增至39515；11次采样stalled=0、min_step=2993，最后timer样本为650128ms/38925；600次select成功、errors=0、elapsed_ms=602282、nvcsw=600、nivcsw=6，PID1保持存活并正常定时返回。本轮计为第1次完整合格启动，连续三次门槛尚未通过。原图及SHA256已归档，见 reports/hakios-device-r10.json。照片显示停在清晰结束摘要，未确认再次回Fastboot；后续使用同一冻结包复测。累计十次HakiOS临时boot，无分区写入、旧7195或QEMU执行；本次证据整理未启动设备。

第11轮（2026-11-15 15:30:37北京时间，同一冻结镜像第2次验收）在用户要求继续后重新核验授权MIX的Fastboot身份、解锁和userdata容量，电池启动条件yes，临时boot获接受。第11轮同次结束照片确认硬件计时660003ms，PPI27由0增至39509；11次采样停滞0次、最小增量2993，最后timer样本650128ms/38919。select600次成功、errors0、elapsed_ms=602281、nvcsw=600、nivcsw=5，PID1保持存活并正常定时返回；同一镜像连续合格计数为2/3。原图和SHA256已归档，见 reports/hakios-device-r11.json。照片显示停在清晰结束摘要，尚未确认再次回Fastboot。累计11次HakiOS临时boot，无分区写入、旧7195或QEMU执行；本次证据整理没有新boot或构建。

第12轮（2026-11-15 15:46:25北京时间，同一冻结镜像第3次验收）在用户要求继续后重新核验授权MIX的Fastboot身份、解锁和userdata容量，电池启动条件yes，临时boot获接受。第12轮同次结束照片确认硬件计时660003ms，PPI27由0增至39518；11次采样停滞0次、最小增量2992，最后timer样本650128ms/38927。select600次成功、errors0、elapsed_ms=602276、nvcsw=600、nivcsw=6，PID1保持存活并正常定时返回；同一镜像连续合格计数为3/3。原图和SHA256已归档，见 reports/hakios-device-r12.json。照片显示停在清晰结束摘要，尚未确认再次回Fastboot。累计12次HakiOS临时boot，无分区写入、旧7195或QEMU执行；本次证据整理没有新boot或构建。

2026-11-15 P2准备：完整HakiOS只读UFS/HFS测试包已完成构建、传输模拟、ARMv8指令检查和冻结容器独立解包核对，镜像SHA256为28fdb35a51f2f9a512e50d109156cafb7b842e9e237d978b00e29402d3b8b78c，见 reports/hakios-p2-storage-test-ready.md。计划从APFS诊断根盘启动，读取userdata HFSX中的18H17文件；物理BlockIO拒绝写入，内核只读挂载。约120秒后保留结束摘要，加载阶段另有25秒停留。该镜像、MPIDR 0x100交接和存储链路均尚未真机验证。此次仅准备电脑端产物，没有设备查询、临时boot或分区写入，累计HakiOS临时boot仍为12次；最后物理状态证据仍是R12结束摘要。下一轮依现有授权重新核验Fastboot身份、解锁及userdata容量后测试。

第13轮（2026-11-15 17:26:47北京时间，首个P2只读存储包）在用户确认连接并要求开始后，核验Fastboot身份、解锁、userdata容量和电池条件yes，临时boot获接受。用户照片确认UEFI/MMU/HOB/BDS/GOP，停在FIND VOLUME CODE 8000000000000003（EFI_UNSUPPORTED）；尚未进入loader/内核或存储诊断。已保存原图与输入，见 reports/hakios-device-r13.md。代码与冻结FAT核对发现分区连接后新增FAT子句柄未被BDS旧列表枚举，准备修正后复测。累计13次HakiOS临时boot，无分区写入；最后照片为UEFI失败画面，未确认回Fastboot。

R13后修正准备：BDS改为连接磁盘后重新枚举文件系统，只从RAM盘读取并校验加载器。4项BDS主机模拟、7项UFS模拟、完整Mu构建和6个二进制指令检查通过，新冻结镜像SHA256为4c954331d209d50c1b51d595fa8e8e69a54e9db24886c1584ff0cac6b9a0db36，见 reports/hakios-p2-storage-test-ready-bds-volume.md。尚未发出下一次boot；最近USB查询Fastboot/ADB均为空，手机最后用户照片为R13失败摘要。既有授权有效，需用户用实体键返回Fastboot后继续。

第14轮（2026-11-15 17:50:02北京时间）用户确认已回Fastboot后，核验身份、解锁、userdata容量及电池条件yes，临时启动BDS卷枚举修正包获接受，镜像4c954331d209d50c1b51d595fa8e8e69a54e9db24886c1584ff0cac6b9a0db36。同次照片确认BDS修正有效，UEFI UFS stage=6、只读userdata HFSX卷头检查通过，内核运行10608ms并收到742次timer IRQ；随后诊断PID1发生SIGBUS（namespace 2/subcode 10），panic后摘要保留。内核HFS挂载和18H17文件读取未验证；原图及哈希见 reports/hakios-device-r14.md。最后物理状态为该摘要，未确认回Fastboot；累计14次HakiOS临时启动，无分区写入。

第15轮（2026-11-15 20:05:58北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本pid1-fault获接受，镜像4b2d13d2f2455dd91ea4d2a4a7444a980e388d15b301cad9ce5e4b279cc48c9f。同次照片及冻结反汇编确认诊断C入口第一条压栈触发SP对齐SIGBUS（PC=0x100003508、SP=0x16fdffe88、ESR=0x9a000000）；内核运行10580ms并收到734次PPI27中断，UEFI只读HFSX卷头检查通过，内核HFS挂载未通过。摘要及原图已保存，见 reports/hakios-device-r15.md。最后物理状态为该摘要，尚未确认回Fastboot；累计15次HakiOS临时启动，无分区写入。

R15后入口修正准备：已接入完整HakiOS上游libmini启动，静态核验及冻结输入见 reports/hakios-p2-storage-test-ready-pid1-startup.md。电脑重新核验本机已在Fastboot、解锁及userdata容量一致；新镜像尚未临时启动，累计仍为15次。下一步按既有授权核验电池并临时测试，结束摘要自动保留。

第16轮（2026-11-15 20:24:31北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本pid1-startup获接受，镜像89b3dd7e62d9c1f1e06ac8ce899543122f6a7308bb9c9ae0d28f2567145a86c4。同次照片确认诊断入口修正有效，内核读取userdata前8192字节match=1；HFS挂载因字符节点返回ENOTBLK（-15），三个18H17文件未验证。120172ms、7325次PPI27中断后正常返回且摘要保留，无panic。原图及源码证据见 reports/hakios-device-r16.md；最后物理状态为结束摘要，未确认回Fastboot。累计16次HakiOS临时启动，无分区写入。

R16后挂载调用修正准备：新冻结镜像95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6已通过正常HakiOS构建、4项实际诊断/摘要主机检查和独立解包，见 reports/hakios-p2-storage-test-ready-block-mount.md。修正块节点fspec与START/RESULT空格，完整内核保持R16字节。此次无新boot或分区写入，累计仍为16次；最后屏幕证据为R16结束摘要，Fastboot待核验。

第17轮（2026-11-15 21:04:19北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本block-mount获接受，镜像95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6。同次结束照片确认内核卷头读取8192字节match=1，START/RESULT摘要修正有效；块节点HFS挂载返回EINVAL（规范化-22），RESULT FAIL STAGE 2，内部分支尚未定位。120177ms、7433次PPI27中断后正常返回且摘要保留，无panic，三个18H17文件未验证。原图、转录和依据见 reports/hakios-device-r17.md；最后物理状态为该摘要，未确认回Fastboot。累计17次HakiOS临时启动，无分区写入。R17归档时用户暂停自动推进；该归档没有设备查询、新boot或构建，未集成438草案。屏幕证据等待已解除。后续用户明确修复请求已恢复工作，当前授权及状态见本文顶部。

2026-11-15 HakiOS P2前置卷维护：第一次原厂Linux RAM维护启动因特殊文件系统挂载点误判在任何写入前拒绝，真实卷头/JIB/零日志头独立读回未变，USB回Fastboot。修正后第二次维护启动完成真实几何、卷头、完整JIB及8MiB空日志校验，仅写offset532480与528384的两个4KiB块，逐次fsync/读回；清除NeedInit并恢复sda/sda15只读。电脑再次逐字节核验16384字节元数据，USB回Fastboot。实际持久写入8192字节，未重分区/格式化或运行旧7195；维护启动两次不计作HakiOS内核启动。完整卷新哈希未测量，原根卷SHA256为修复前输入；修复后用原卷加两块差异契约，见reports/hakios-hfs-journal-repair.json。HFS挂载仍待HakiOS实测。

第18轮（2026-11-15 22:13:50北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本block-mount获接受，镜像95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6。同次照片确认HFSX只读挂载成功RC0/readonly1，随后在ufs_request读拷贝的memmove目标写入发生kernel data abort；13247ms、972次PPI27后返回摘要保留，三个18H17文件与卸载未验证。原图及源码见reports/hakios-device-r18.md；最新状态为该摘要，尚未确认回Fastboot。累计18次HakiOS临时启动，本次HakiOS启动无分区写入。前置原厂Linux RAM维护已初始化现有HFSX空日志，实际持久写入8192字节并独立读回核验；本次HakiOS临时启动自身为只读。内核/镜像与R17逐字节一致，本轮只改变真实文件系统日志输入。

R18后准备：普通UFS读取映射已按上游buf_map/unmap修正并通过11项主机检查，完整HakiOS正常流程重新编译、打包、独立解包和冻结，新镜像bf23f302f1e1e6a13582fd672d3f23b0b133daf169e55871890058f11493afee。用户已确认回Fastboot，后续USB查询一致；下一次按身份/容量/电池guard执行一次临时复测。准备未boot或写入分区，累计18次HakiOS启动。

第19轮（2026-11-15 22:39:09北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本ufs-buffer-map获接受，镜像bf23f302f1e1e6a13582fd672d3f23b0b133daf169e55871890058f11493afee。同次照片确认HFS只读挂载，VERSION读取已返回正数，诊断错误沿用被read清零的x1在算哈希时SIGSEGV（PC100002aa4/FAR0）；13233ms/1044次PPI27后panic摘要保留。完整文件核验与卸载未通过，见reports/hakios-device-r19.md；累计19次HakiOS临时启动，本次HakiOS启动无分区写入。最后状态为该摘要，未确认回Fastboot。前置原厂Linux RAM维护已初始化现有HFSX空日志，实际持久写入8192字节并独立读回核验；本次HakiOS临时启动自身为只读。本轮在完整HakiOS内核中沿用buf_map/unmap修正普通UFS读取映射；真实卷沿用R18已核验的日志修复输入。

R19后准备：诊断改用已链接完整上游libmini_syscall，七项实际边界/摘要主机检查、ARM64调用检查和冻结容器核验通过，镜像c00191bbef17f45b079f402313dfac2040b8f59ca5b1ca2bc855a1add2013346。用户已确认回Fastboot；下一次依身份/容量/电池guard只进行一次临时复测。准备无分区写入，累计19次HakiOS启动。

第20轮（2026-11-15 23:02:51北京时间）电脑核验Fastboot身份、解锁、userdata容量及电池条件yes后，临时启动P2冻结版本probe-syscall获接受，镜像c00191bbef17f45b079f402313dfac2040b8f59ca5b1ca2bc855a1add2013346。同次照片确认HFS只读挂载、三个18H17文件全部长度/FNV match1、卸载rc0和RESULT PASS，120173ms/7485次PPI27后正常定时返回无panic。只读存储子项通过，iOS14进程仍未执行；见reports/hakios-device-r20.md。累计20次HakiOS临时启动，本次启动无分区写入，最新状态为保留PASS摘要，未确认回Fastboot。前置原厂Linux RAM维护已初始化现有HFSX空日志，实际持久写入8192字节并独立读回核验；本次HakiOS临时启动自身为只读。本轮诊断改用已链接完整上游libmini_syscall，修正R19的x1返回契约；真实卷沿用已核验日志输入，保留UFS映射修正。

第21轮同次照片确认完整HakiOS内核和上游init执行，真实HFSX只读根切换rc0，原始共享缓存头匹配，原始18H17 dyld的438映射count3/rc0。df与mount均被SIGKILL终止，DYNRESULT FAIL stage6；信号来源尚未确定，timeout0不能排除父进程wait错误后的清理kill。120198ms/10116次PPI27后正常定时返回，无panic，摘要保留。P1和R20存储验收继续有效，P2整体未通过；累计仍21次HakiOS临时启动，本次仅归档照片，无启动或分区写入。见reports/hakios-device-r21.md。

R21后修正镜像已构建、独立解包并冻结，见reports/hakios-p2-ios14-exit-ready.md。修正等待错误/EINTR耗尽的分类，ECHILD不再发送kill；保留父进程等待errno、清理原因、kill结果及耗时，记录上游现有内核退出reason和签名拒绝页，不改变签名或内存策略。10项实际子进程检查、7项实际摘要检查及最终内核日志调用核验通过，153份冻结证据。新镜像72cd35b591a16073508695f4e7b81d685877c102461ce3cf7799038d86e6389f，尚未启动；电脑未检测到Fastboot，需要用户实体键返回后按既有授权进行一次120秒测试，结束摘要保留，无需守屏。P1和R20存储不再复测，累计仍21次HakiOS临时启动。

R22同次照片确认真实HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。df与mount均SIGKILL，父进程wait_errno0/cleanup NONE/kill0，内核退出均namespace23/code20000003000000c8。实际HakiOS源码解码为Mach port GUARD/INVALID_OPTIONS，target为消息号200；唯一匹配非零target分支是旧mach_msg消息头的兼容策略拒绝。df23067ms、mount3388ms，均非超时；120200ms/10103次PPI27后正常返回，无panic。下一步为原始iOS14.8旧Mach消息入口增加有界兼容，不改文件系统、不取消签名校验。P1/R20存储保持通过，P2未通过；累计22次启动，归档未启动或写入。见reports/hakios-device-r22.md。

R22后的旧Mach消息兼容修正包已构建并冻结，镜像14f4a17c81b2a746d51c05fb266797135fe788d13cbc55bebbfe60bce3b1811a。沿完整HakiOS8796原有IPC路径，仅在LITHIUM_ARMV8_0、lithium_ios14_test=1、iOS平台及精确SDK14.8时允许旧Mach消息入口；未改消息复制、大小/描述符、端口权限或后续guard检查，未取消签名验证。18项实际原函数边界检查、完整内核编译、最终兼容日志调用、独立容器解包及163份冻结证据通过。R23已使用该包临时启动并获Fastboot接受，同次照片已确认MACH14分支与原始mount成功，df触及父进程时限；测试摘要已保留。P1/R20存储不再复测，累计23次HakiOS临时启动，准备与本次启动均无分区写入。准备证据见reports/hakios-p2-ios14-mach-msg-ready.md，启动记录见reports/hakios-device-r23.md。当前为R23保留摘要；先完成下一修正包，再请求实体键返回Fastboot。

R23同次照片确认完整HakiOS执行、HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。MACH14 platform2/sdkE0800/id200/legacy1确认旧Mach入口兼容分支生效；原始mount在5878ms正常exit0、signal0、timeout0。df在30148ms触及30秒测试时限，父进程TIME_LIMIT/kill1/rc0，内核namespace2/code9与该SIGKILL一致；导致df尚未完成的具体等待或首次加载成本仍未确定。120208ms/10968次PPI27后正常定时返回，无panic。原图、转录和163份同次冻结证据已核验。P1/R20存储保持通过，P2整体未通过；累计23次启动，归档无设备操作或分区写入。见reports/hakios-device-r23.md。

R24修正包已构建并冻结，镜像4f8946ab4b69cfd4eeeb39d8bf954243bc93d8b2b0b18c3a1af92205931d1bc0。保留完整HakiOS8796、原始18H17程序和已有精确14.8 IPC兼容；df等待预算由30秒增至60秒，mount保持30秒，两者及清理的预算合计94秒，整轮仍120秒。复用HakiOS已有systrace=2/systrace_name=df，结束摘要保留两项预算和最后三条已完成df BSD调用。十项实际子进程检查覆盖30/60秒两种预算，八项实际摘要检查及168份冻结证据通过。准备时尚未验证；R24现已确认原始df/mount均正常退出，动态工具子项通过。P1/R20存储不再复测，P2整体未通过；累计仍23次HakiOS临时启动，准备无分区写入。R24现已按既有授权临时启动并获Fastboot接受，累计24次HakiOS临时启动，本次无分区写入；同次照片已确认DYNRESULT PASS并正常返回；动态工具子项结案，下一步准备launchd包。准备证据见reports/hakios-p2-ios14-df-ready.md，启动记录见reports/hakios-device-r24.md。

R24同次照片确认原始18H17的df在31908ms、mount在5209ms均exit0、signal0、timeout0；父进程cleanup NONE/kill0，DYNRESULT PASS。HFSX只读根切换rc0、原始缓存头匹配、438映射rc0及精确14.8旧Mach入口均可见。120236ms/11055次PPI27后正常定时返回，无panic；168份同次冻结证据与原图已核验。动态工具子项结案，不再复测；P1/R20存储保持通过，P2整体、launchd、基础服务、图形/触摸及SpringBoard仍未通过。累计24次HakiOS临时启动，本次仅归档，无设备操作或分区写入。见reports/hakios-device-r24.md。

原始18H17 launchd PID1测试包已构建并冻结，镜像3e58cf6cef7c28ea952d273a12bc3e961a6600a06d78c5535dcf1cd8737c6c72。内核与R24字节一致；复用完整上游init的bindfs、ramfs数据目录初始化及rc/exec交接。使用iOS14已安装卷的元数据生成95个Data目录，选取原始notifyd、cfprefsd及一个诊断任务。诊断必须由真实launchd启动，并用原始ps核对PID1与两个原始服务进程；BEFORE_EXEC不代表运行通过。165份冻结证据、独立容器解包、13项快照边界与9项摘要检查通过。P1/R20存储/R24动态工具已结案，不再复测；P2整体及launchd/基础服务仍待真机。累计仍24次HakiOS临时启动，准备没有分区写入或新boot；整轮120秒，加载另25秒，摘要保留，无需守屏。见reports/hakios-p2-ios14-launchd-ready.md。

R25同次照片确认HFSX只读根切换rc0、原始launchd403488字节match1、上游ramfs目录初始化2项检查/0失败。PID1交接后原始dyld438映射rc0、精确14.8旧Mach入口生效；70395ms/9355次PPI27后PID1因SIGABRT终止，panic显示no exit reason available、signal6、exitstatus78。原始launchd已进入执行路径，稳定PID1及所选服务未通过；具体主动退出原因尚未定位，照片未显示诊断任务或进程快照。165项同次冻结证据、原图及转录已核验。P1/R20存储/R24动态工具保持通过，P2整体未通过；累计25次启动，本次归档无设备操作或分区写入。见reports/hakios-device-r25.md。

R26原始launchd标准输出交接修正包已构建并冻结，镜像c652c9fd059d6808e23b03283ce02ebef80531367de9cb158637f3e17d36b543。原始18H17 launchd的isatty(1)拒绝分支会退出十进制78；R25的stdout连接控制台，属于具体候选原因，尚未真机确认。本包用正常boot描述符stdin/stdout=/dev/null并实查ENOTTY，stderr交接前保留控制台；显式核对上游自动提供的rd=md0，不修改原始Apple程序或内核策略。复用HakiOS已有applog/user_abort_trace/systrace，结束摘要保留原始错误、abort栈和最后3条launchd调用。172份冻结证据、独立容器解包、3项真实描述符和12项摘要检查通过；完整内核与R24/R25字节一致。P1/R20/R24结案记录保持，P2整体及launchd稳定运行未通过。准备没有分区写入或新boot，累计仍25次；本轮仍120秒、加载另25秒，摘要保留，无需守屏。见reports/hakios-p2-ios14-launchd-stdio-ready.md。

R26同次照片确认HFSX只读根切换rc0、原始launchd403488字节match1、上游ramfs初始化2项检查/0失败、rd=md0。随后stage6/errno19拒绝交接，未显示LAUNCHSTDIO成功标记或原始服务；120766ms/9551次PPI27后正常定时返回，无panic。源码审计确认新增检查误要求/dev/null终端查询返回ENOTTY25，而活动XNU的mmioctl对此返回ENODEV19，与停点吻合；照片未单独记录open/dup/ioctl子步骤，不能将R25原始launchd退出原因记为已解决。172项同次冻结证据、原图和转录已核验。P1/R20/R24继续有效，P2整体未通过；累计26次启动，本次仅归档和只读打包路线审查，无新boot或分区写入。 下一步修正交接检查并核对上游集成，继续使用UFS根卷按需读取；不再直接复测R26。详见reports/hakios-system-image-route-audit.md。

R27同次照片确认HFSX只读根切换rc0、原始18H17 launchd403488字节match1、ramfs初始化2检查/0失败。标准描述符各步骤完成，LAUNCHSTDIO成功、tty errno19已接受；原始launchd以PID1执行并启动诊断子任务PID3/PPID1。121313ms、19905次PPI27后正常定时返回，无panic；此前退出78在本轮未重现。照片没有LAUNCHPROC或LAUNCHRESULT，notifyd/cfprefsd及完整服务验收仍未通过。观察器先等待60秒、原始ps另有30秒预算，120秒总窗口可能先结束；该轮真实任务开始时间未记录，不能断言只是超时。原图、转录与190项冻结证据已核验，累计仍27次；此次仅归档，无手机操作或分区写入。 下一步核对观察器时限并取得基础服务进程结果；不重跑R27冻结包。

R28同次结束照片确认LAUNCHRESULT PASS：原始ps在6487ms正常exit0、signal0、timeout0，无清理kill；首次完整快照同时找到PID1 launchd、notifyd和cfprefsd。观察器在自身入口后8045ms完成检查，不能解释为整个iOS在8秒启动。硬件计时181310ms、PPI27增量24934、3次采样停滞0次，按预定时限正常返回，无panic。检查通过后诊断任务继续等待，日志继续滚动不表示这项检查尚未完成。原图、转录及199项冻结证据已核验，此进程存在性子项结案，无需延长或重复。累计仍28次HakiOS临时启动；本次仅归档，无设备查询、启动或分区写入。 后续按最新联合路线推进实际服务请求、显示、物理触摸和SpringBoard，同包保留结果。P2完整服务、图形、触摸和SpringBoard仍未通过；系统继续UFS/HFSX按需读取。R28单次入口已消耗，不再运行。见reports/hakios-device-r28.md。

此前“分析后停止”对应的旧路线保持停止，不能据新计划重新推进旧7195或FactoryData修复。既有必要root/清除等操作授权继续保留，用户本轮另明确表示手机已连接、可以测试，后续HakiOS平台临时测试无需重复索取授权。当前画面以用户最新报告和实际USB查询为据；userdata仍按已有安装记录视为iOS HFSX。

R14后准备：PID1故障记录包已完成完整HakiOS构建、双读取器验证、ARMv8检查和独立解包，冻结镜像4b2d13d2f2455dd91ea4d2a4a7444a980e388d15b301cad9ce5e4b279cc48c9f，见 reports/hakios-p2-storage-test-ready-pid1-fault.md。启用上游systrace并保留故障地址，尚未确认SIGBUS修复。本次准备没有设备启动或分区写入，累计仍为14次；下一次按既有授权核验Fastboot、电池和本机容量后测试。

以下设备实验记录及旧内核成功结果均为历史事实，不构成 HakiOS 新基线的通过证据。

用户已明确表示：**“不需要备份，需要的话全部清除即可，需要root你也可以自行root”。**

- 不以用户数据、原系统或真机分区备份为项目推进的前置条件；不再为备份停下等待。
- 为移植和调试所需的 root、临时 recovery、boot/recovery 实验，以及必要的清除数据，可以在已核验的本机上自行执行，无需重复索取该授权。
- 授权表示允许在需要时操作，不表示必须立即 root 或清除。操作仍应有对应实验目的、匹配镜像和可解释的技术步骤。
- 清除、格式化或调整用于实验的存储范围，须先读取实际分区和容量；不按其他设备的 LBA 或固件 XML 占位容量操作。
- 保留现有原厂固件和哈希作为恢复材料。无需为此创建手机备份；校准/基带等与当前实验无关的分区没有清除用途。
- 不将授权解读为需要重新锁定 BL、修改安全固件或执行不明来源整机刷写批处理。

本地详细操作和设备标识放入忽略目录；共享报告只记录结果及必要参数。目标继续为 iOS 14，不以 iOS16/17 替代。

截至本次更新，已完成三次 `fastboot boot` 临时诊断启动，均记录 ARM64 入口后正常进入 Android；最后普通重启确认诊断属性消失。未 root、未清除、未刷写或重新分区。详细结果见 `reports/entry-bringup.md`。

随后完成一次显示采样、两次原厂串联日志采集及三次 UEFI 临时启动。UEFI 前两次自动返回 Android但保留日志无效；第三次使用原厂内核恢复交接，Fastboot 接受后未恢复 USB，需用户实体按键重启。第三次没有通过运行阶段验收。各次记录见 `reports/uefi-bringup.md` 与 `reports/uefi-device.json`；截至此时仍未 root、清除、刷写或重新分区。

再次检查时 ADB/Fastboot 仍为空，尚未恢复。等待期间仅在电脑上构建扩大 ramdisk 与 initrd 交接的独立诊断，14 个 QEMU 用例和 Windows 本地输入校验通过，没有发出新的真机启动或写入命令。复现及边界见 `reports/handoff-bringup.md`。

20:34 再次核验时手机已恢复原厂 Android，用户照片另已证明第三轮 UEFI 进入 BDS/GOP/ticks20。随后执行一次独立扩大 ramdisk 诊断，同次日志和普通原厂重启对照通过。末尾缺文件检查已适配 Android 7 的 legacy exec-out 行为并独立复核。下一轮定位 UEFI 后的恢复路径；仍使用临时启动，未 root、清除、刷写或重新分区。

随后第四、第五、第六轮 UEFI 均只执行临时启动；目视检查点依次推进到 07、08、09，但尚未拿到 UEFI 后的同次 Android 日志。第六轮后用户明确确认手动长按电源键恢复，ADB 已重新连接；不将该返回计为自动交接。后续继续在已授权范围内调试，详细冻结输入与结果见各轮报告。

第七轮用户确认五块青色、无红；其恢复方式未明确。第八轮 FAT/EFI 条件 PASS、五块青色、无红，但没有进入 Android；用户明确按要求手动重启回系统，电脑重新核验 lithium、原厂 MIUI 和 boot_completed。不能将这次返回记为自动交接。所有八轮仍仅临时启动；无 root、清除、分区刷写或重分区。

第九轮用户确认八黄、五青、无红；第十轮确认八黄、一绿、无红。两轮均没有自动返回，用户明确长按电源键恢复，电脑核验原厂lithium、MIUI和boot_completed。十轮全部仅为临时启动，无root、清除或分区刷写。用户随后要求优先复用开源项目，主线调整见 `reports/reuse-route.md`；当前不再把继续细分原厂Linux恢复作为主要里程碑，既有操作授权保持有效。

2026-11-13首轮直接XNU临时启动已获用户照片确认：构建标签38a7cfa8a32290ac，对应镜像a2df440f...a009026；XNU自身编号01至05及07日志可见，MMU开启后进入arm_init，最终停在entering arm_vm_init。这证明已进入真机XNU，不证明内存初始化完成、调度/中断稳定或iOS用户空间启动。没有持久写入；用户需实体电源键恢复，不能计为自动返回。证据见reports/ios14-native-device.json及本地照片。

2026-11-13第八轮XNU临时启动已读通UFS，核验容量、GPT CRC和userdata几何，用户随后手动恢复Android。八轮均未持久写入。当前准备完整iOS14.8 HFS根卷及专用userdata写入脚本；既有必要清除授权有效。实际写入须以最终镜像内容验证、fsck、稀疏传输逐字节验证及再次读取MIX身份/解锁/分区容量为前提，并记录在reports/ios14-root-install.json。刷写后保持Fastboot并直接临时启动XNU；Android userdata将被替换，若恢复Android须用原厂recovery重建userdata，不能再将长按电源描述为必然返回原Android桌面。

2026-11-13 17:12—17:29：安装前重新核验lithium/boot_completed、Fastboot型号MSM8996_A8、解锁状态、userdata长度250710192128字节及max-download-size=512MiB；仅执行 `fastboot -s <private-device-serial> -S 256M flash userdata`。完整根卷16GiB、稀疏传输9383190364字节，36段全部成功，总耗时988.584秒，原Android userdata已替换。没有root、单独erase/format、重分区或其他分区刷写。随后保持Fastboot并临时boot新XNU；该boot步骤自身未刷分区，不能据此将本轮整体记为“无持久写入”。未运行QEMU，手机全盘读回、根挂载及用户空间尚未验证。
