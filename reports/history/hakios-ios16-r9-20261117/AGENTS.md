R9 最终故障（2026-11-17）：用户原图确认已 panic 返回 UEFI，RAW BYTES34293；Console I/O from interrupt-disabled context @serial_console.c:168，cpu1/kernel_task。冻结符号只恢复到控制台及 debugger/exception 帧，完整违规调用链未恢复；R9 新增 initCPU 关中断 IOLog 为已确认契约缺陷，与保护条件吻合。USB未枚举、0帧，延长接收器已按确切命令定向停止并保留原状态。R10先修复日志上下文，保留三核目标（另1核驻留UEFI服务UFS）、UFS和2880MiB；SMP/服务/桌面未通过。不重试R9，不重刷userdata。见reports/hakios-ios16-device-r9.md/json。

R8 最终诊断（2026-11-17）：已分析全部8轮。SpringBoard同锁/采样owner关联跨度1441.096秒，但第4–6轮为NonBlocking数据库请求、第7–8轮转入Blocking，不能认定同一RPC持续挂起。最后两轮lsd的数据库请求与安装恢复等待同一dispatch group（非unfair-lock），跨度360.293秒；容器/偏好设置依赖栈仍变化，循环死锁和最终根因未证实。发现一次HFS内部搬移ERANGE34，实际UFS Data缺DKIOCISSOLIDSTATE属性，属具体修正候选，尚未关联到出错文件/数据库。单核区间top 98–99%，末尾free1745MB；画行0.733–0.740秒。下一修订追数据库ready完成条件、补齐固态属性并降低同步日志开销，保留UFS/2880MiB/单核；不重试旧轮、不重刷userdata。本次仅分析与记录。见reports/hakios-ios16-r8-final-diagnosis.md/json。

R8 最终采集（2026-11-17）：正常内核 timer 与成功 USB 排空正文已捕获。最终 366347 字节/6849 帧重解码一致，缺口/重复/残帧/设备 lost 均0；有界取栈 8 轮。用户确认运行中苹果标志，物理终态未目视核验，桌面未通过。保持 UFS、2880MiB/单核；不重试R8/R7/R6/R5，不重刷userdata。用户已明确授权日志收集核验保存后直接关闭电脑，无需再询问。见 reports/hakios-ios16-device-r8.md/json。

R8于2026-11-17T04:32:13+08:00临时启动一次，用户确认启动文字；USB已枚举/持续传输，完整payload参数读回一致，有限采样配置和top已实际输出，原始launchd已启动。首轮目标尚未启动，尚无服务线程/IPC接收PID样本；根因和桌面未解决。接收器与只读观察器在后台按同次运行保存快照和终态，不重启、不重试、不重刷userdata。保持UFS、2880MiB/单核；BGR24批量写入的硬件时序待后续计时，日志同步输出会影响性能。首次top统计含绝对计数器原点偏移，不能当作本次CPU利用率。报告reports/hakios-ios16-device-r8.md/json；准备提交5c6b7ab，备份archive/ios16-r7-20261117。

R8已完成完整构建、检查、冻结和实时Fastboot身份核验，等待同次临时启动。修正R7参数/printf/短名漏采；增加当前IPC接收PID和服务日志，BGR24批量写入经1650项旧新像素比较。诊断不等于根因或桌面已解决。保持UFS、2880MiB/单核，不重刷userdata。见reports/hakios-ios16-r8-prepared.md/json。

R7 最终观测（2026-11-17）：内核正常定时结束与成功USB排空正文已捕获；用户确认UEFI结束摘要，桌面未通过。最终252881字节/6699帧，原始帧重解码逐字节一致，缺口/重复/残帧/设备lost均0。多线程8轮；SpringBoard同锁/同owner关联5次，跨度1440.424秒，owner等待_LSDReadService数据库同步回复。第七轮lsd曾等待容器代码签名缓存失效同步回复；实际IPC对端与循环依赖根因未定。画行3次，墙钟[3497790, 5438626]微秒、线程CPU[2713141, 3469201]微秒。top参数截断、长printf尾部在USB前丢失、容器管理器名称漏采已确认，因此未宣称所有日志完整。保持UFS、2880MiB/单核；不重试R7/R6/R5，不重刷userdata。全6GB、SMP、A530、物理触摸及桌面未验证。见reports/hakios-ios16-device-r7.md/json。

R7 启动前准备（历史）：已完成完整 HakiOS 内核、413项用户空间任务、真实 D22AP loader/Mu 构建、检查、冻结及实时 Fastboot 身份核验，等待同次临时启动。复用固定上游 ustacks/top 同轮采样 SpringBoard、backboardd、lsd、containermanagerd、runningboardd；首次内核180秒，此后每6分钟，最多8轮、每进程96线程/14帧、每轮8进程。等待 syscall 原始寄存器和只读 unfair-lock owner_tid 用于关联持锁者，明确 sampled word 可能变化；显示前8次提交分段记录 CPU/墙钟/非CPU耗时，算法与像素保持不变。实际函数 ASan/UBSan、50项画行比较、依赖回归和实际包检查通过；诊断能力不等于锁根因或桌面已解决。保持UFS、2880MiB/单核；全6GB、SMP、A530和物理触摸未验证；不重刷userdata、不重试R6/R5。备份archive/ios16-r6-20261117和对应历史报告已核验。见reports/hakios-ios16-r7-prepared.md/json。

R6 最终观测（2026-11-17）：已正常定时返回 UEFI，用户确认结束摘要，桌面未通过。最终325433字节/7373帧重解码一致，缺口/重复/不完整均0；从内核横幅到正常timer返回与成功排空结果的已输出控制台流通过核验。USB随后Pipe error，UEFI摘要属于独立控制台未被USB捕获，不能宣称所有固件/daemon日志完整。SpringBoard主线程21次相同栈跨越26分36.816秒，状态WAITING、CPU累计不增，尚未进入主队列/运行循环；原20F66缓存映射为ControlCenterServices → BaseBoard BSCollectionFilter → CoreServices → __ulock_wait，具体锁持有者/根因未定。首次基础图层在内核计时1143907ms，之后完整观察30分钟；三次画行约3.29～4.79秒（含调度），末尾1754MB free。下一修订复用上游有界多线程取栈关联依赖，并同步查软件显示耗时，不再单纯加时。保持UFS系统、2880MiB/单核；全6GB、SMP、物理触摸、A530和桌面仍未验证。本次仅分析归档，未改运行代码、构建、查询或操作手机；不重试已消费R6/R5，不重刷userdata。备份archive/ios16-r5-20261117和R5历史报告保留。见reports/hakios-ios16-device-r6.md/json。

# IOS-MIX1 项目指令

更新：2026-11-16。以下路线由用户明确确认，是本仓库最高优先级的项目级路线约束，适用于当前及后续会话。

**最高约束：基于完整 HakiOS 移植增加 MIX 一代设备支持，以 HakiOS 为唯一活动内核基线。原先的 7195 内核仅用于只读对照，不再使用。**

用户原话：

> 就按照这个方案“基于完整 HakiOS 移植增加 MIX 一代设备支持”，以Hakios为基准进行内核适配，原先的内核仅用于对照不要再使用了，一点点造轮子毫无意义，写入项目提示词内，作为最高约束。

1. 活动内核必须来自 HakiOS 的完整移植、正常依赖和构建流程；在其上增加 MSM8996/8996pro 与 lithium 的设备支持。不能只引用 HakiOS 的名称、工具或零散驱动，实际仍自行补齐一套 Apple 裸 XNU 移植。
2. 原 Apple XNU 7195.141.2 / ports/lithium 路线及其内核、补丁、镜像和构建树均冻结为历史对照。允许只读查阅、反汇编、比较和核对既有证据；禁止继续修改、构建、打包、启动或以其作为测试、演示、过渡、兜底内核。不得继续沿第十六轮 FactoryData/panic 停点推进旧路线。
3. 不运行旧 7195 活动入口来推进项目，包括 scripts/build-ios14-xnu.py、scripts/prepare-ios14-lithium.py、scripts/build-ios14-native-boot.py、scripts/boot-ios14-native.py，以及 run-wsl-limited.ps1 的旧 kernel/native-package 任务。旧命令在历史文档中出现不构成执行授权。
4. 本项目新增工作集中于 HakiOS 下的 SoC/板级适配、加载契约、外设接入和有边界的系统版本兼容。允许按上游流程重新编译完整 HakiOS。通用内核与运行服务优先使用上游完整实现；遇到缺口先核对依赖和集成，不逐项自造替代服务。
5. 已验证的 MIX 硬件参数、原厂固件、DTB、内存/分区资料及观测结论可继续使用；适配代码按 HakiOS 接口和原始上游来源整理。不得将旧 7195 运行时实现整套搬回新活动内核。
6. 用户2026-11-16明确批准改用iOS16.5 / 20F66 / iPhone10,3 / D22AP / arm64（不是arm64e），仅使用用户提供的IPSW。唯一活动基线仍为upstream/hakios-xnu-kernel的63fc2b4633bac3af8e57dadab9f5a8d4cb940438、XNU8796.121.2。先达到真实SpringBoard显示更新；触摸、GPU和稳定性分别验证。iOS14适配归档，不再作为活动集成。
7. UniXNU、ios-sailfish和其他项目仅作为组件/资料来源，不得切换活动内核、退回7195或另造通用服务。按审计、内核、用户空间、真机调试四阶段推进，每阶段报告后停止，等待用户确认下一阶段。用户已确认阶段二成果并授权阶段三用户空间、UFS/Data集成及核验后的userdata写入；阶段三已完成一次userdata写入及采样核验，用户已确认进入阶段四真机启动与联合调试。
8. 不执行 QEMU。运行结论以 MIX 真机证据为准；构建和静态检查只证明输入与实现检查结果。2026-11-16用户确认资源调整：WSL 内存16GB、12个逻辑处理器、swap2GB，任务上限14GB、CPU配额1200%、构建并发12，大任务顺序执行；统一配置见config/hakios-resources.json。系统优先存放UFS并按需读取，精简压缩不作为launchd修复前提。
9. 每次工作先读本文件、PLAN.md、OPERATIONS.md 和 project.json。README、旧计划、历史报告、子目录说明、脚本默认值或既有自动推进记录与本约束冲突时，服从本约束。只有用户后续明确修改路线才可改变它。
10. 2026-11-16用户要求加快集成：服务请求、显示、物理触摸和SpringBoard联合推进，尽量同一集成包上机观察，不再为每个小项单独安排真机轮次。P2/P3/P4仍分别记录真实证据，但不把逐项独立复测作为下一项开发前提。服务诊断在后台运行，不阻挡桌面启动；通过的P1、存储、动态工具和R28进程存在性不再重复验收。首次UI尝试可复用上游软件绘制候选并同步推进A530加速，不能把候选路径、进程存在或合成输入记成可操作桌面、GPU加速或物理触摸通过。上机前仍需完整HakiOS构建、检查并冻结实际集成包。

现行执行计划：PLAN.md。设备状态和已有操作授权：OPERATIONS.md。旧内核的成功记录属于历史路线，不能计作 HakiOS 新基线的已验证能力。

11. 用户2026-11-16要求以充分使用6GB机型全部可用RAM为目标，同时推进USB日志。不得把512MiB或首个扩大窗口当作长期上限；固件、外设和未知动态保留区不得冒充空闲RAM。USB先复用现有来源，区分源码集成、编译成功、枚举、传输和完整日志捕获的证据。
12. 迁移前备份已完成：archive/ios14-r31-20261116分支、仓库外E盘校验备份；不得把Apple二进制/固件加入Git或共享包。

R5 最终观测（2026-11-17）：已定时返回 UEFI 保留摘要，桌面未通过。首次基础图层在内核计时1095537ms，随后保留完整30分钟；主机启动至USB终止约48分54秒。最终309344字节/6890帧重解码一致，缺口/重复/不完整均0，USB Pipe error仍缺返回尾部，完整采集未通过。SpringBoard已连接backboardd，activity assertion后来返回，虚拟HID后来有非空对象；最后仍无明确屏幕所有者、BootUI为白苹果。六次图层提交各约3.29–3.82秒（含调度），末尾约1750MB free，不能继续只归因于等待或RAM不足。下一修订先复用上游诊断获取SpringBoard初始化/主线程停点与主队列里程碑，核对启动依赖、时钟和显示耗时；CPU拓扑与UFS驻留核须对齐后再开SMP。此次只归档和只读排查，未构建、改运行代码或操作手机；不重试R5、不重刷userdata。见reports/hakios-ios16-device-r5.md/json。
