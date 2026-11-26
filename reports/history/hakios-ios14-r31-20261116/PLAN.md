# 基于完整 HakiOS 为 MIX 一代增加设备支持

更新：2026-11-16。状态：**用户已确认路线，最高项目级约束见 AGENTS.md。**

HakiOS 是唯一活动内核基线。原 Apple 7195 / ports/lithium 内核路线退出活动使用，只保留只读对照；不再修改、构建、打包、启动或作为兜底。旧活动计划的原文存于 reports/history/PLAN-before-hakios-20261114.md。

最终目标保持 lithium / Snapdragon 821 / Adreno 530 原生运行 iOS 14.8（18H17），用户空间来源固定 iPhone10,1 / D20AP。唯一活动基线为完整HakiOS固定提交63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2，项目重构、完整依赖和P0输入已完成。第12轮同次结束照片确认硬件计时660003ms，PPI27由0增至39518；11次采样停滞0次、最小增量2992，最后timer样本650128ms/38927。select600次成功、errors0、elapsed_ms=602276、nvcsw=600、nivcsw=6，PID1保持存活并正常定时返回；同一镜像连续合格计数为3/3。 当前证据见 reports/hakios-device-r12.md；iOS14、GPU和触摸未验证。

**活动工作边界**

- 在完整 HakiOS 上增加 MSM8996/8996pro 与 lithium 的配置、硬件描述、引导交接和驱动适配。新代码集中于这些差异及边界明确的 iOS 14 兼容。
- 通用内核和公共服务使用上游完整实现与依赖。先核对来源和集成，避免逐项自造替代服务。
- MIX 原厂固件、DTB、运行时内存、显示、分区和既有观测继续作为输入；旧 7195 源码、补丁和产物仅作只读比较。
- UniXNU、ios-sailfish 仅作组件/资料来源，不自动替换活动基线。改变 HakiOS 唯一基线或 iOS 14 系统目标，必须由用户明确决定。

**顺序与验收**

| 阶段 | 工作 | 通过条件及失败处理 |
| --- | --- | --- |
| P0：HakiOS 完整基线审计 | 固定 HakiOS 与依赖；区分真实实现、外部二进制、占位代码；核对 ARMv8.0、iOS 14 私有依赖、A530 图形路径；设计首个 MIX 包 | 有完整来源/依赖清单、iOS 14 差异表和有界适配方案；若需大规模重建通用内核或图形栈，提交阻塞，不退回旧内核 |
| P1：MIX 平台适配（已通过） | 沿 HakiOS 正常流程构建；增加 SoC/设备配置，接入内存、GIC/timer、显示和加载契约；用上游已有最小程序验证闭环 | 真机执行 HakiOS 内核和用户程序；真实 timer IRQ、线程定时睡眠/唤醒与切换持续运行10分钟，连续3次启动复现 |
| P2：iOS 14 兼容 | 核对 D20AP / 18H17 依赖；沿 HakiOS 集成路径适配根卷、存储、dyld、launchd、必要挂载和基础服务 | 真实完成必要挂载和关键启动任务；明确私有组件和ABI版本；无法保持有界兼容时回审，不自动换系统或重建7195 |
| P3：图形与触摸 | 适配 A530、IOSurface/显示合成、MIX 面板与 FT5x46，复用可用的上游图形/HID设计 | 真机持续更新并显示图形表面，真实触摸通过系统HID路径；明确实际加速状态；缺少整条GPU路径时先报告范围 |
| P4：完整 iOS 14 验收 | 接入 backboardd、SpringBoard、系统服务与完整Data布局 | SpringBoard运行30分钟，真实触摸可操作，设置和至少一个系统应用打开，连续启动5次 |
| P5：扩展外设 | 推进UFS持久化、网络、充电/电量、音频及休眠；相机/通话分别评估 | 每项有真机功能证据与已知限制 |

P0 的版本和 A530 审查必须先于大量服务排错；P2/P3分别记录系统兼容与图形可行性证据。2026-11-16用户要求R28后联合推进P2/P3/P4：这些阶段是能力和验收维度，不再按表格顺序逐个小项安排独立真机轮次。服务调用、显示、物理触摸、backboardd和SpringBoard在同一集成包观察；后台诊断不阻挡桌面启动。最小用户程序只验平台，不能替代iOS14验收。旧7195的成功记录不算作HakiOS的已验证能力。

**执行约束**

- 不执行 QEMU。构建和静态检查约束输入，运行验收只用 MIX 真机证据。
- 使用用户2026-11-16确认的WSL16GB/12逻辑处理器/2GB swap、任务14GB/1200%CPU、并发12，大任务顺序执行；统一配置config/hakios-resources.json。
- 每轮先明确问题和通过条件，再冻结 HakiOS 来源、补丁、镜像/内核/ADT/根卷哈希并对照同次证据。
- 旧脚本的 kernel/native-package/native boot 入口禁止用于继续旧7195路线。新活动入口必须显式绑定 HakiOS 和本机适配配置。
- 同一通用服务连续两轮扩大自研范围，或需要重建通用内核/整套私有图形框架时，先回审；不能以“临时验证”恢复旧内核。
- 已有必要root、清除和临时启动授权保留，设备操作依 OPERATIONS.md 核对身份、分区和实际状态。当前userdata已是iOS HFSX，不能假设重启即可返回Android。

**下一项工作**

R31同次10张运行照片确认launchd[1]完成early boot并退出bootstrap/ondemand模式，SpringBoard[12]执行实际请求，backboardd[11]启动并到达HID和window server初始化。最新明确故障是backboardd向containermanagerd请求共享系统容器时反复无效连接，container error51/XPC_INVALID_REPLY后得到空路径，触发NSURL initFileURLWithPath:nil string parameter异常并以SIGABRT退出。原始18H17共享缓存结合本轮slide0xb5d0000核对，两帧准确落在BaseBoard的BSSystemContainerForCurrentProcessPathProvider libraryPath/cachesPath。冻结包已包含containermanagerd配置和原始程序；容器服务为何未应答仍待诊断，不能直接归因为漏包、persona或目录缺失。上游16.5修补使用的MCMUserIdentityCache/personasAreSupported符号在原始14.8 MobileContainerManager中未找到，禁止直接套用16版地址。两项MediaConversionService原始Info.plist及程序存在，XPCService本来为false，其Invalid XPCService dictionary不是已确认的主故障。另记录type4 persona查找失败及HID bootstrap权限拒绝；checkerboard仅匹配diagnostics、cryptexd要求research-enabled，硬件条件排除不是UFS格式故障。控制台按行回顶部覆盖，旧文字仍留在下方，不能据此认定整机或launchd循环重启。9个可读timer样本从110151ms/14562次PPI27推进到710813ms/101005次，均stalled0；未见kernel panic或桌面。本组照片未见上一轮SpringBoardUI.m:56断言或type6错误，但尚无IDENTITY FINAL，不能宣布权限/身份修复已通过。此次仅归档和原始镜像/冻结源码只读分析，未查询或启动手机、未写分区；累计31次，R31入口已消费。下一步核对完整containermanagerd实际启动/退出、Mach服务和凭据、原始14版容器接口及Data目录，结合本轮结束摘要处理显示/HID后续停点；保持UFS/HFSX与完整HakiOS基线。 见reports/hakios-device-r31.md。

2026-11-16T04:54:23.739584+08:00，R31 XML权限与persona联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。完整HakiOS重新编译，接入原始签名XML读取与完整persona初始化；系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计31次HakiOS临时启动；本轮未写分区。R31入口已消费，不自动重试。 见reports/hakios-device-r31.md。

R31已修正完整HakiOS中原始XML权限读取的接口集成，并在lithium_ui启动前复用完整persona子系统创建type6身份。原始18H17 SpringBoard签名包含com.apple.springboard-ui.client，XML特殊槽哈希已核验；原始SecTask使用csops op7/ERANGE读取。不修改Apple程序或共享缓存，不绕过断言，不将权限查询改为全部允许。完整HakiOS重新编译，三个实际对象文件确认新增调用已接入。17项实际适配函数和保留摘要的ASan/UBSan检查通过；实际APFS读回确认335项plist0644、目录和程序0755、uid/gid0，R30根卷文件内容保持。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `2af949c5c1a8d958720b28e2e3f6b9a54c9e60cd99c83b43e0116652d33d7efd`。本次准备未查询或启动手机、未写分区，累计仍30次；R31权限、身份和UI运行结果待真机。结束摘要新增LITHIUM IDENTITY FINAL，保留SpringBoard实际XML探测/复制及persona查询结果。

R31单次入口已执行并保存运行证据，禁止再次运行。后续须针对明确容器故障检查并冻结新包；本轮原计划为一次联合启动，同次记录服务诊断、显示和SpringBoard进展。物理触摸与A530继续适配，尚未通过的能力按实际结果记录。P1、R20存储、R24动态工具及R28进程存在性不重复验收。设备操作前重新核验身份、解锁、容量和电池；不要求守屏、秒表或中途抢拍。

**已完成轮次与准备历史**

R30运行中三张照片确认原始launchd[1]完成early boot并进入system bootstrap，随后原始SpringBoard在+[SpringBoardUI load]、SpringBoardUI.m:56触发NSInternalInconsistencyException；PID11、20、21反复abort，桌面初始化失败，继续等待不能消除相同断言。照片中的0x1c9469ebc减同次cache slide0x23f68000，准确落在原始18H17的+[SpringBoardUI load]+0x1a0；原始代码首先引用com.apple.springboard-ui.client权限标记，但尚未观测具体运行时查询结果，不能将缺失entitlement写为已确认根因。同时多项服务报persona type6查询失败。缺launchd.plist/NRN cache后仍继续early boot，不可当作此次明确崩溃的直接原因。三个timer样本50.069、110.137、170.161秒均stalled0，照片未见kernel panic、结束摘要、服务诊断结果或图形界面；运行结论仍限于这些同次证据。本次仅归档与只读原镜像分析，无手机查询、启动或分区写入，累计仍30次。R30入口已消费。下一步优先核对SpringBoard早期进程权限查询与上游集成，再处理persona兼容；保留现有UFS布局和完整HakiOS基线。 见reports/hakios-device-r30.md。

2026-11-16T04:09:33.061024+08:00，R30权限修正联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。复用R29同字节完整HakiOS内核与loader，系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计30次HakiOS临时启动；本轮未写分区。R30入口已消费，不自动重试。 见reports/hakios-device-r30.md。

R30联合包已修正R29任务权限缺陷并独立解包冻结。实际APFS读回确认335项plist为0644、目录0755、程序和适配库0755，uid/gid均为0。真实R29坏镜像拒绝、上游writer/reader读回及异常所有者/目录/执行权限共7项检查通过。复用R29已完整编译且已真机执行的同字节HakiOS内核与loader。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `e9a2f7d5b2d8fe97e0964f324a12275bec61dbb9ee45b7502679968505c21104`。本次准备未启动手机、未写分区，累计仍29次；R30服务及UI结果待真机。

R30单次启动已执行归档，禁止再次执行该入口；下一轮须修正、检查并冻结新包。该轮原计划为一次联合启动，同次记录服务诊断、显示和SpringBoard进展。物理触摸与A530继续适配，尚未通过的能力按实际结果记录。P1、R20存储、R24动态工具及R28进程存在性不重复验收。设备操作前重新核验身份、解锁、容量和电池；不要求守屏、秒表或中途抢拍。

R29用户运行中照片确认原始launchd[1]反复拒绝system域任务，报error122：Path had bad ownership/permissions，其中包括com.hakios.lithium.svc_test。独立读取同次冻结包的真实APFS inode确认：335项任务配置和LaunchDaemons目录均为0777，uid/gid均为0；问题是组/其他用户可写的权限，而非非root所有者。Windows暂存目录的0777经copytree保留进ext4并进入镜像。此前逐项读回仅证明输入与镜像一致，漏查launchd所需的权限语义，属于本轮打包缺陷。延长等待不能消除此拒绝。原图、局部转录、inode清单和745项冻结文件已核验。照片不是最终摘要；未观察到界面、诊断成功、panic或正常返回。本次归档无设备操作，累计仍29次；下一步修正联合根卷目录0755、plist0644及可执行文件0755，按上游root所有者规则打包并直接核验镜像元数据，不修改UFS布局，不绕过launchd权限检查。 见reports/hakios-r29-permissions-analysis.md。

2026-11-16 03:54:43（北京时间），按用户“那就上机吧，手机已连接”的指令，重新核验冻结镜像及授权MIX的Fastboot身份、解锁、userdata容量、电池启动条件与下载容量后，执行R29联合UI临时启动一次。发送与Booting均返回OKAY；尚未收到同次屏幕结果，不能据此认定内核、服务或桌面运行通过。同包尝试服务诊断、backboardd、SpringBoard和显示，物理触摸采样未接通、A530加速未验证。累计29次HakiOS临时启动，本轮无分区写入、旧7195或QEMU执行。R29入口已消耗，不自动重试。 见reports/hakios-device-r29.md。

联合 iOS14 界面测试包已完成完整 HakiOS 编译、检查、独立解包并冻结。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `d98b24d5e502c27c15049791178d1b4350d9c416b15c879198956802e73b8794`。本次准备未启动手机、未写分区，累计仍28次。

R29已确认任务权限拒绝，先修正打包权限再准备后续联合包；该单次入口已消耗，不再运行。物理触摸与A530继续适配，尚未通过的能力按实际结果记录。P1、R20存储、R24动态工具及R28进程存在性不重复验收。设备操作前重新核验身份、解锁、容量和电池；不要求守屏、秒表或中途抢拍。

以下R27准备记录为历史，不是当前执行入口。

R27同次照片确认HFSX只读根切换rc0、原始18H17 launchd403488字节match1、ramfs初始化2检查/0失败。标准描述符各步骤完成，LAUNCHSTDIO成功、tty errno19已接受；原始launchd以PID1执行并启动诊断子任务PID3/PPID1。121313ms、19905次PPI27后正常定时返回，无panic；此前退出78在本轮未重现。照片没有LAUNCHPROC或LAUNCHRESULT，notifyd/cfprefsd及完整服务验收仍未通过。观察器先等待60秒、原始ps另有30秒预算，120秒总窗口可能先结束；该轮真实任务开始时间未记录，不能断言只是超时。原图、转录与190项冻结证据已核验，累计仍27次；此次仅归档，无手机操作或分区写入。 下一步核对观察器时限并取得基础服务进程结果；不重跑R27冻结包。

2026-11-16实际上机条件核验已通过：当前授权MIX已在Fastboot，身份、解锁、userdata容量、电池启动条件及下载容量均符合R27要求；190项冻结证据和当前源再次核验一致。project.json和README中的旧资源记录已同步到WSL16GB/12逻辑处理器、任务14GB/并发12。当时可使用reports/local/boot-hakios-r27.py执行一次临时测试；该轮现已执行归档，不再运行；120秒运行、加载另25秒，结束摘要保留。此次只查询设备，没有临时启动或分区写入，累计仍26次；原始launchd运行结果仍待真机。最新就绪证据见reports/hakios-r27-device-test-readiness.md。

R27交接修复包已完成完整HakiOS正常构建、针对性检查、独立解包并冻结190项证据，镜像d1f3af4e79baef473cb1308cd52e7604ca92a62231f38ea18f80b9057ee6f4fa。修正R26误要求ENOTTY25的检查，接受目标ENODEV19非终端语义，保留描述符各步骤与exec结果。原始18H17程序及已通过的完整内核保持原字节；系统继续UFS/HFSX按需读取。两份原始镜像与完整上游差异审查完成；文件/配置差异沿上游补齐，既有共享缓存映射与Mach接口兼容继续保留。8项描述符、4项rc/exec用例、13项摘要和13项快照边界通过。WSL16GB/12CPU及任务配额已实际核验。本轮没有手机查询、临时启动或分区写入，累计仍26次。P1/R20/R24已结案，原launchd及基础服务仍待R27真机。下一轮用reports/local/boot-hakios-r27.py核验身份、解锁、容量及电池后只临时启动一次；120秒运行、加载另25秒，摘要保留，无需守屏。见reports/hakios-p2-ios14-launchd-handoff-ready.md和reports/hakios-ios14-version-alignment.md。

2026-11-16用户已确认计划并授权推进到下一次上机包就绪：归档R26；落实WSL16GB/12CPU；核对完整上游依赖、init/rc/FLASHBOOT；使用18H17及20F66原始镜像与既有数据形成版本差异清单；修复描述符检查与分步骤日志；正常构建、独立解包并冻结R27。系统继续存放UFS按需读取，精简压缩独立处理。R27准备阶段不启动手机；同次新包就绪后再进行上机。

R26同次照片确认HFSX只读根切换rc0、原始launchd403488字节match1、上游ramfs初始化2项检查/0失败、rd=md0。随后stage6/errno19拒绝交接，未显示LAUNCHSTDIO成功标记或原始服务；120766ms/9551次PPI27后正常定时返回，无panic。源码审计确认新增检查误要求/dev/null终端查询返回ENOTTY25，而活动XNU的mmioctl对此返回ENODEV19，与停点吻合；照片未单独记录open/dup/ioctl子步骤，不能将R25原始launchd退出原因记为已解决。172项同次冻结证据、原图和转录已核验。P1/R20/R24继续有效，P2整体未通过；累计26次启动，本次仅归档和只读打包路线审查，无新boot或分区写入。 下一步修正交接检查并核对上游集成，继续使用UFS根卷按需读取；不再直接复测R26。详见reports/hakios-system-image-route-audit.md。

R26原始launchd标准输出交接修正包已构建并冻结，镜像c652c9fd059d6808e23b03283ce02ebef80531367de9cb158637f3e17d36b543。原始18H17 launchd的isatty(1)拒绝分支会退出十进制78；R25的stdout连接控制台，属于具体候选原因，尚未真机确认。本包用正常boot描述符stdin/stdout=/dev/null并实查ENOTTY，stderr交接前保留控制台；显式核对上游自动提供的rd=md0，不修改原始Apple程序或内核策略。复用HakiOS已有applog/user_abort_trace/systrace，结束摘要保留原始错误、abort栈和最后3条launchd调用。172份冻结证据、独立容器解包、3项真实描述符和12项摘要检查通过；完整内核与R24/R25字节一致。P1/R20/R24结案记录保持，P2整体及launchd稳定运行未通过。准备没有分区写入或新boot，累计仍25次；本轮仍120秒、加载另25秒，摘要保留，无需守屏。见reports/hakios-p2-ios14-launchd-stdio-ready.md。

R25同次照片确认HFSX只读根切换rc0、原始launchd403488字节match1、上游ramfs目录初始化2项检查/0失败。PID1交接后原始dyld438映射rc0、精确14.8旧Mach入口生效；70395ms/9355次PPI27后PID1因SIGABRT终止，panic显示no exit reason available、signal6、exitstatus78。原始launchd已进入执行路径，稳定PID1及所选服务未通过；具体主动退出原因尚未定位，照片未显示诊断任务或进程快照。165项同次冻结证据、原图及转录已核验。P1/R20存储/R24动态工具保持通过，P2整体未通过；累计25次启动，本次归档无设备操作或分区写入。见reports/hakios-device-r25.md。

原始18H17 launchd PID1测试包已构建并冻结，镜像3e58cf6cef7c28ea952d273a12bc3e961a6600a06d78c5535dcf1cd8737c6c72。内核与R24字节一致；复用完整上游init的bindfs、ramfs数据目录初始化及rc/exec交接。使用iOS14已安装卷的元数据生成95个Data目录，选取原始notifyd、cfprefsd及一个诊断任务。诊断必须由真实launchd启动，并用原始ps核对PID1与两个原始服务进程；BEFORE_EXEC不代表运行通过。165份冻结证据、独立容器解包、13项快照边界与9项摘要检查通过。P1/R20存储/R24动态工具已结案，不再复测；P2整体及launchd/基础服务仍待真机。累计仍24次HakiOS临时启动，准备没有分区写入或新boot；整轮120秒，加载另25秒，摘要保留，无需守屏。见reports/hakios-p2-ios14-launchd-ready.md。

R24同次照片确认原始18H17的df在31908ms、mount在5209ms均exit0、signal0、timeout0；父进程cleanup NONE/kill0，DYNRESULT PASS。HFSX只读根切换rc0、原始缓存头匹配、438映射rc0及精确14.8旧Mach入口均可见。120236ms/11055次PPI27后正常定时返回，无panic；168份同次冻结证据与原图已核验。动态工具子项结案，不再复测；P1/R20存储保持通过，P2整体、launchd、基础服务、图形/触摸及SpringBoard仍未通过。累计24次HakiOS临时启动，本次仅归档，无设备操作或分区写入。见reports/hakios-device-r24.md。

R24修正包已构建并冻结，镜像4f8946ab4b69cfd4eeeb39d8bf954243bc93d8b2b0b18c3a1af92205931d1bc0。保留完整HakiOS8796、原始18H17程序和已有精确14.8 IPC兼容；df等待预算由30秒增至60秒，mount保持30秒，两者及清理的预算合计94秒，整轮仍120秒。复用HakiOS已有systrace=2/systrace_name=df，结束摘要保留两项预算和最后三条已完成df BSD调用。十项实际子进程检查覆盖30/60秒两种预算，八项实际摘要检查及168份冻结证据通过。准备时尚未验证；R24现已确认原始df/mount均正常退出，动态工具子项通过。P1/R20存储不再复测，P2整体未通过；累计仍23次HakiOS临时启动，准备无分区写入。R24现已按既有授权临时启动并获Fastboot接受，累计24次HakiOS临时启动，本次无分区写入；同次照片已确认DYNRESULT PASS并正常返回；动态工具子项结案，下一步准备launchd包。准备证据见reports/hakios-p2-ios14-df-ready.md，启动记录见reports/hakios-device-r24.md。

R23同次照片确认完整HakiOS执行、HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。MACH14 platform2/sdkE0800/id200/legacy1确认旧Mach入口兼容分支生效；原始mount在5878ms正常exit0、signal0、timeout0。df在30148ms触及30秒测试时限，父进程TIME_LIMIT/kill1/rc0，内核namespace2/code9与该SIGKILL一致；导致df尚未完成的具体等待或首次加载成本仍未确定。120208ms/10968次PPI27后正常定时返回，无panic。原图、转录和163份同次冻结证据已核验。P1/R20存储保持通过，P2整体未通过；累计23次启动，归档无设备操作或分区写入。见reports/hakios-device-r23.md。

R22后的旧Mach消息兼容修正包已构建并冻结，镜像14f4a17c81b2a746d51c05fb266797135fe788d13cbc55bebbfe60bce3b1811a。沿完整HakiOS8796原有IPC路径，仅在LITHIUM_ARMV8_0、lithium_ios14_test=1、iOS平台及精确SDK14.8时允许旧Mach消息入口；未改消息复制、大小/描述符、端口权限或后续guard检查，未取消签名验证。18项实际原函数边界检查、完整内核编译、最终兼容日志调用、独立容器解包及163份冻结证据通过。R23已使用该包临时启动并获Fastboot接受，同次照片已确认MACH14分支与原始mount成功，df触及父进程时限；测试摘要已保留。P1/R20存储不再复测，累计23次HakiOS临时启动，准备与本次启动均无分区写入。准备证据见reports/hakios-p2-ios14-mach-msg-ready.md，启动记录见reports/hakios-device-r23.md。原始df/mount均exit0、signal0、timeout0，DYNRESULT PASS且正常定时返回后再推进launchd；命令接受不计作运行通过。

R22同次照片确认真实HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。df与mount均SIGKILL，父进程wait_errno0/cleanup NONE/kill0，内核退出均namespace23/code20000003000000c8。实际HakiOS源码解码为Mach port GUARD/INVALID_OPTIONS，target为消息号200；唯一匹配非零target分支是旧mach_msg消息头的兼容策略拒绝。df23067ms、mount3388ms，均非超时；120200ms/10103次PPI27后正常返回，无panic。下一步为原始iOS14.8旧Mach消息入口增加有界兼容，不改文件系统、不取消签名校验。P1/R20存储保持通过，P2未通过；累计22次启动，归档未启动或写入。见reports/hakios-device-r22.md。

R21后修正镜像已构建、独立解包并冻结，见reports/hakios-p2-ios14-exit-ready.md。修正等待错误/EINTR耗尽的分类，ECHILD不再发送kill；保留父进程等待errno、清理原因、kill结果及耗时，记录上游现有内核退出reason和签名拒绝页，不改变签名或内存策略。10项实际子进程检查、7项实际摘要检查及最终内核日志调用核验通过，153份冻结证据。新镜像72cd35b591a16073508695f4e7b81d685877c102461ce3cf7799038d86e6389f，尚未启动；电脑未检测到Fastboot，需要用户实体键返回后按既有授权进行一次120秒测试，结束摘要保留，无需守屏。P1和R20存储不再复测，累计仍21次HakiOS临时启动。

第21轮同次照片确认完整HakiOS内核和上游init执行，真实HFSX只读根切换rc0，原始共享缓存头匹配，原始18H17 dyld的438映射count3/rc0。df与mount均被SIGKILL终止，DYNRESULT FAIL stage6；信号来源尚未确定，timeout0不能排除父进程wait错误后的清理kill。120198ms/10116次PPI27后正常定时返回，无panic，摘要保留。P1和R20存储验收继续有效，P2整体未通过；累计仍21次HakiOS临时启动，本次仅归档照片，无启动或分区写入。见reports/hakios-device-r21.md。

当前实测：R20真实只读存储通过：HEADER及VERSION/DYLD/LAUNCHD全部match1，HFS只读挂载/卸载rc0，120173ms/7485次PPI27后正常定时返回且无panic。只读存储子项完成，不再复测；下一步原始18H17动态进程及共享缓存兼容，P2整体仍未通过。见reports/hakios-p2-storage-closure.md。

R19诊断调用修正已冻结：实际七项主机检查、最终上游mini_syscall调用和独立解包核验通过；使用既有授权直接复测完整文件读取。见reports/hakios-p2-storage-test-ready-probe-syscall.md。不重测P1、不重做卷维护。

当前实测：R19照片确认HFS只读挂载，VERSION读取已返回正数；诊断算哈希时误用被read清零的x1，PID1在0x100002aa4/FAR0发生SIGSEGV。下一步复用已链接上游libmini_syscall修正调用契约；三个文件与卸载仍待验证。见reports/hakios-device-r19.md。

R18映射修正包已冻结：完整HakiOS普通UFS读取沿用buf_map/unmap；11项实际策略主机检查、驱动同步/编译及最终映射调用顺序核验通过。镜像bf23f302f1e1e6a13582fd672d3f23b0b133daf169e55871890058f11493afee，133项冻结证据见reports/hakios-p2-storage-test-ready-ufs-buffer-map.md。下一轮按既有授权核验Fastboot后复测文件读取，不重测P1、不重做卷日志维护。

当前实测：R18照片确认真实HFSX只读挂载RC0/readonly1，日志修复已越过EINVAL。约13247ms、972次PPI27后kernel data abort，冻结PC为memmove目标写入、LR为ufs_request读拷贝；普通ufs_strategy未映射cluster页面，同驱动数据卷已有buf_map/unmap。下一步沿用上游映射接口修复普通读取；三个18H17文件、卸载与P2尚未通过。见reports/hakios-device-r18.md。

当前实测：R17同次照片确认完整HakiOS内核和诊断执行，userdata卷头读取8192字节match=1，120177ms/7433次PPI27中断后正常定时返回，无panic，摘要保留。START/RESULT空格修正已验证；块节点挂载返回EINVAL（规范化-22），RESULT FAIL STAGE 2。具体VFS/HFS内部分支尚未定位，HFS挂载、三个18H17文件及系统兼容仍未通过，见 reports/hakios-device-r17.md。

**用户已明确恢复修复与推进，并授权按需调整整个 userdata。** 已定位现有 HFSX 日志待初始化且日志头全零；上游只读检查主机复现 EINVAL。先执行有界日志初始化、读回核验，再用 R17 同镜像复测，见 reports/hakios-journal-maintenance-ready.md。无需重分区验证本次缺陷；不重测 P1，不集成438草案。

当前实测：R16照片确认上游libmini启动修正有效，完整HakiOS内核读取userdata前8192字节并核对match=1，120172ms/7325次PPI27中断后正常定时返回，无panic。HFS挂载返回-15（ENOTBLK），诊断传入字符节点而上游已提供块节点；18H17文件与系统兼容仍未验证。原图及VFS/节点源码证据见 reports/hakios-device-r16.md。fspec已修正为/dev/ufs_userdata，START/RESULT前缀空格已补齐，修正包已冻结；保留R16冻结包，不重测P1。

R16挂载调用修正包已冻结，见 reports/hakios-p2-storage-test-ready-block-mount.md。镜像SHA256为 95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6；完整内核与R16字节一致，loader仅COFF时间戳改变。实际诊断与摘要集成的4项主机检查及独立解包通过；R17已使用此包实测，挂载返回EINVAL，三个18H17文件未读取。冻结准备报告保留当时状态，实际结果以R17报告为准；当前用户已恢复推进；先修复卷日志再复测，不重测P1。

P1已通过：第10、11、12轮使用同一冻结镜像，三次真实timer/线程运行超过十分钟、600次等待零错误、正常返回且最终摘要保留，见 reports/hakios-p1-closure.md。此阶段的重复测试已完成。P2首个只读存储实验包现已准备，见 reports/hakios-p2-storage-test-ready.md：Mu板层接入本机warm UFS LUN0，复用Mu PartitionDxe、完整HakiOS UFS共享页桥和HFS驱动；诊断PID1从APFS测试根盘运行，只读挂载真实userdata，核对18H17版本、dyld与launchd文件内容。UEFI留在原核心、内核经PSCI在MPIDR 0x100运行，内核仍只用一个CPU。R14当时仅验证UEFI读卷头及内核执行；R16现已验证诊断PID1和内核卷头读取，HFS挂载仍未通过，见上文。

第13轮已使用首个P2镜像临时启动；照片确认UEFI/BDS/GOP，停在FIND VOLUME / EFI_UNSUPPORTED，尚未进入loader或内核，见 reports/hakios-device-r13.md。代码与冻结FAT核对发现PartitionDxe新增FAT子句柄后，BDS仍查连接前的旧列表。现已修正为连接后枚举文件系统，4项实际查找函数的协议模拟和完整打包检查通过，见 reports/hakios-p2-storage-test-ready-bds-volume.md。R14已启动该冻结镜像4c954331d209d50c1b51d595fa8e8e69a54e9db24886c1584ff0cac6b9a0db36，结果见上文。

测试约120秒，另有25秒加载停留，结束摘要保留到重启。用户无需守屏、秒表或中途抢拍，方便时提供一次结束摘要即可。通过条件为真实只读挂载、卷头及三个文件match=1、卸载成功、RESULT PASS且正常返回；失败时记录具体UEFI阶段、UFS状态、PSCI停点或内核原因。最新照片为R20存储PASS及正常定时返回的保留摘要，累计HakiOS临时启动20次；尚未确认回Fastboot。R17屏幕证据等待已解除，历史审核见 reports/local/hakios-r17-blocked-audit.json；当前按最新明确修复请求恢复工作。

438参数适配草案已准备，见 reports/hakios-ios14-shared-region-438-draft.md：复用8796保留的公开结构和完整_shared_region_map_and_slide核心，7项主机检查及当前完整源码git apply --check通过。草案尚未接入正常构建、编译新内核或打包启动；R17活动输入和冻结镜像保持原样，存储结果确认后再集成。

第17轮启动后已静态核对下一步集成，见 reports/hakios-p2-userspace-integration-review.md：完整上游已有init、bindfs、根切换及shared-region核心；HFSX布局需有界适配，原始18H17 cache的mappingCount=3使正常dyld共享映射选择syscall 438，而8796该项为nosys。该静态缺口尚未作为真机运行错误观测；用户明确继续且存储通过后沿现有核心处理旧参数契约，再验证原始dyld/launchd，保留上游minimac/AMFI/pthread实现。P3继续按现有A530/GLES/IOSurface候选台账审查，不能套用A642L驱动或将文字显示计为GPU通过。iOS14进程、系统兼容和图形均未验证；出现大规模通用服务替代或整条图形栈缺口时按既有路线约束提交具体阻塞。

活动WSL任务为 hakios-audit / hakios-prepare / hakios-check / hakios-deps / hakios-support / hakios-storage / hakios-kernel / hakios-package。旧 kernel/native-package/root-transfer 在访问WSL前拒绝，60个旧入口保持无副作用拒绝桩。iOS14私有ABI与A530 GLES候选须在P2/P3按现有差异台账验证；不恢复FactoryData旧路线。

审核过程原文保留在 reports/replan-hakios-unixnu-20261114.md；其中“UniXNU可替换基线”等原建议已由用户最新的 HakiOS 唯一基线约束覆盖。
