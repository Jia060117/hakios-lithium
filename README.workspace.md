# iOS16.5 on Xiaomi Mi MIX / lithium

基于**完整 HakiOS 移植**为 MIX 一代增加设备支持，HakiOS 是唯一活动内核。最高约束见 [AGENTS.md](AGENTS.md)，分阶段验收见 [PLAN.md](PLAN.md)，设备授权与状态见 [OPERATIONS.md](OPERATIONS.md)。旧 Apple XNU7195 路线冻结为只读对照。

用户2026-11-16批准迁移到iOS16.5/20F66/iPhone10,3/D22AP/arm64，首要目标为真实SpringBoard显示更新。内核固定HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。UniXNU和ios-sailfish仅作组件/资料来源。

## 当前阶段

迁移阶段二的完整HakiOS内核、Mu固件和加载器已构建，源码边界、主机日志检查和8份产物的目标指令扫描通过。内核窗口由512MiB改为2880MiB，并同步启动载荷/UEFI堆地址；6GB机型的全部可用RAM仍是目标，低地址动态保留区和多段接入尚未解决，不能宣布已开放6GB。新增USB2日志驱动及电脑端采集器，枚举/传输未真机验证。

详细成果、限制和下一阶段入口见 [阶段二报告](reports/hakios-ios16-migration-phase2.md)。当前loader使用编译占位身份，不能启动Darwin；原始D22AP身份、System/cryptex/Data和可启动包留待阶段三。用户空间保持UFS按需读取路线。本阶段没有查询或启动手机、没有写分区。按用户约定在阶段报告后停止。

迁移前已建立 `archive/ios14-r31-20261116` 备份分支及E盘独立校验备份，iOS14适配在 `ports/hakios-lithium/archive/ios14/`。以下保留旧路线历史记录，只供对照，不是当前执行入口或iOS16验收结论。

## iOS14历史结果（只读）

R31同次10张运行照片确认launchd[1]完成early boot并退出bootstrap/ondemand模式，SpringBoard[12]执行实际请求，backboardd[11]启动并到达HID和window server初始化。最新明确故障是backboardd向containermanagerd请求共享系统容器时反复无效连接，container error51/XPC_INVALID_REPLY后得到空路径，触发NSURL initFileURLWithPath:nil string parameter异常并以SIGABRT退出。原始18H17共享缓存结合本轮slide0xb5d0000核对，两帧准确落在BaseBoard的BSSystemContainerForCurrentProcessPathProvider libraryPath/cachesPath。冻结包已包含containermanagerd配置和原始程序；容器服务为何未应答仍待诊断，不能直接归因为漏包、persona或目录缺失。上游16.5修补使用的MCMUserIdentityCache/personasAreSupported符号在原始14.8 MobileContainerManager中未找到，禁止直接套用16版地址。两项MediaConversionService原始Info.plist及程序存在，XPCService本来为false，其Invalid XPCService dictionary不是已确认的主故障。另记录type4 persona查找失败及HID bootstrap权限拒绝；checkerboard仅匹配diagnostics、cryptexd要求research-enabled，硬件条件排除不是UFS格式故障。控制台按行回顶部覆盖，旧文字仍留在下方，不能据此认定整机或launchd循环重启。9个可读timer样本从110151ms/14562次PPI27推进到710813ms/101005次，均stalled0；未见kernel panic或桌面。本组照片未见上一轮SpringBoardUI.m:56断言或type6错误，但尚无IDENTITY FINAL，不能宣布权限/身份修复已通过。此次仅归档和原始镜像/冻结源码只读分析，未查询或启动手机、未写分区；累计31次，R31入口已消费。下一步核对完整containermanagerd实际启动/退出、Mach服务和凭据、原始14版容器接口及Data目录，结合本轮结束摘要处理显示/HID后续停点；保持UFS/HFSX与完整HakiOS基线。 见reports/hakios-device-r31.md。

2026-11-16T04:54:23.739584+08:00，R31 XML权限与persona联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。完整HakiOS重新编译，接入原始签名XML读取与完整persona初始化；系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计31次HakiOS临时启动；本轮未写分区。R31入口已消费，不自动重试。 见reports/hakios-device-r31.md。

R31 XML权限与服务身份修正包已就绪。完整HakiOS重新编译，接通原始SpringBoard签名XML读取，并复用完整persona子系统在启动前建立type6身份；17项生产函数与摘要检查通过，788项文件已冻结并独立核验。系统继续UFS/HFSX按需读取，R30引导根卷内容与字节哈希保持。权限、身份和UI实际结果待真机；本轮无手机查询、启动或分区写入，累计仍30次。下一次只临时启动R31同一个服务/显示/SpringBoard联合包，结束摘要保留。见[修复说明](reports/hakios-r31-identity-integration.md)和[就绪报告](reports/hakios-ios14-ui-ready.md)。

R30运行中三张照片确认原始launchd[1]完成early boot并进入system bootstrap，随后原始SpringBoard在+[SpringBoardUI load]、SpringBoardUI.m:56触发NSInternalInconsistencyException；PID11、20、21反复abort，桌面初始化失败，继续等待不能消除相同断言。照片中的0x1c9469ebc减同次cache slide0x23f68000，准确落在原始18H17的+[SpringBoardUI load]+0x1a0；原始代码首先引用com.apple.springboard-ui.client权限标记，但尚未观测具体运行时查询结果，不能将缺失entitlement写为已确认根因。同时多项服务报persona type6查询失败。缺launchd.plist/NRN cache后仍继续early boot，不可当作此次明确崩溃的直接原因。三个timer样本50.069、110.137、170.161秒均stalled0，照片未见kernel panic、结束摘要、服务诊断结果或图形界面；运行结论仍限于这些同次证据。本次仅归档与只读原镜像分析，无手机查询、启动或分区写入，累计仍30次。R30入口已消费。下一步优先核对SpringBoard早期进程权限查询与上游集成，再处理persona兼容；保留现有UFS布局和完整HakiOS基线。 见reports/hakios-device-r30.md。

2026-11-16T04:09:33.061024+08:00，R30权限修正联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。复用R29同字节完整HakiOS内核与loader，系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计30次HakiOS临时启动；本轮未写分区。R30入口已消费，不自动重试。 见reports/hakios-device-r30.md。

R29用户运行中照片确认原始launchd[1]反复拒绝system域任务，报error122：Path had bad ownership/permissions，其中包括com.hakios.lithium.svc_test。独立读取同次冻结包的真实APFS inode确认：335项任务配置和LaunchDaemons目录均为0777，uid/gid均为0；问题是组/其他用户可写的权限，而非非root所有者。Windows暂存目录的0777经copytree保留进ext4并进入镜像。此前逐项读回仅证明输入与镜像一致，漏查launchd所需的权限语义，属于本轮打包缺陷。延长等待不能消除此拒绝。原图、局部转录、inode清单和745项冻结文件已核验。照片不是最终摘要；未观察到界面、诊断成功、panic或正常返回。本次归档无设备操作，累计仍29次；下一步修正联合根卷目录0755、plist0644及可执行文件0755，按上游root所有者规则打包并直接核验镜像元数据，不修改UFS布局，不绕过launchd权限检查。 见reports/hakios-r29-permissions-analysis.md。

2026-11-16 03:54:43（北京时间），按用户“那就上机吧，手机已连接”的指令，重新核验冻结镜像及授权MIX的Fastboot身份、解锁、userdata容量、电池启动条件与下载容量后，执行R29联合UI临时启动一次。发送与Booting均返回OKAY；尚未收到同次屏幕结果，不能据此认定内核、服务或桌面运行通过。同包尝试服务诊断、backboardd、SpringBoard和显示，物理触摸采样未接通、A530加速未验证。累计29次HakiOS临时启动，本轮无分区写入、旧7195或QEMU执行。R29入口已消耗，不自动重试。 见reports/hakios-device-r29.md。

2026-11-16：[联合界面包已就绪](reports/hakios-ios14-ui-ready.md)。完整HakiOS新内核已编译、打包、独立解包并冻结745项文件；同包接入333项原始任务配置、2项后台诊断和两个18H17适配库，同时尝试backboardd、SpringBoard和显示。修正原14显示提交、IOSurface结构/方法及BGRA32到MIX BGR24的转换；15项接口/摘要用例和18项像素边界检查通过，嵌入APFS卷354项逐项读回一致。系统继续从现有UFS/HFSX按需读取。

首次启动窗口15分钟，仅在出现新进展时延长，最多30分钟；候选界面出现后保留，故障/结束摘要保留，无需守屏或中途抢拍。FT5x46物理采样尚未接通，A530加速未通过；软件绘制仍待真机，Data暂为ramfs，内核内存窗口仍512MiB。手机已通过[Fastboot只读核验](reports/hakios-ui-device-preflight.json)，随后已执行R29一次临时启动，累计29次，本轮未写分区；该单次入口已消耗。以前的[准备报告](reports/hakios-ios14-ui-integration-preparation.md)保留为历史快照。

[R28](reports/hakios-device-r28.md)进程检查通过：原始ps在6.487秒正常退出，首次完整快照找到PID1 launchd、notifyd和cfprefsd，LAUNCHRESULT PASS；硬件181.310秒后正常定时返回，无panic。进程存在性子项结案，无需延长或重复；完整Mach服务、图形、触摸及SpringBoard尚未验证。原图和199项冻结证据已核验，累计仍28次；此次仅归档，无设备操作。下一步联合推进服务请求、显示、物理触摸及SpringBoard，系统继续UFS按需读取。

完整 HakiOS 依赖、MIX 平台补丁、Mu 板层、内核与上游 loader/hello 已构建。首个临时启动镜像通过静态检查；9项路线隔离回归及上游设备/config/驱动清单检查通过。详细输入、哈希和上机步骤见 [首包记录](reports/hakios-platform-test-ready.md)。

P0输入与完整HakiOS构建已完成，P1平台验收通过：同一镜像连续三轮真机运行超过十分钟，timer IRQ持续增长，600次线程等待均零错误，并正常定时返回。自动采集和结束摘要保留已验证。三轮原图、精确测量和冻结输入见 [P1验收记录](reports/hakios-p1-closure.md)。P1重复测试已结束。

[R20](reports/hakios-device-r20.md)已验证真实UFS/HFSX只读挂载、三个18H17文件内容匹配、卸载成功和正常定时返回；[只读存储子项](reports/hakios-p2-storage-closure.md)通过，存储复测结束。前置卷维护仅初始化两个4KiB日志元数据块并核验读回，未重分区或格式化；HakiOS测试自身保持只读。

[R21](reports/hakios-device-r21.md)真机确认只读根切换与原始dyld的438映射rc0；df、mount均被SIGKILL终止，原因待定位。正常120198ms返回，无panic。P1和R20存储保持通过，P2、launchd、GPU、触摸和SpringBoard未通过。

[R22](reports/hakios-device-r22.md)确认父进程kill0；原始df/mount被内核GUARD/INVALID_OPTIONS终止，消息号200被旧Mach消息兼容策略拒绝。正常120200ms返回，无panic。累计22次HakiOS临时启动；下一步有界适配原始iOS14.8的Mach消息入口，P2未通过。

[R23](reports/hakios-device-r23.md)确认旧Mach消息兼容分支生效，原始mount在5878ms正常退出；df在30148ms触及父进程30秒时限后被清理，具体等待仍待定位。整轮120208ms正常返回，无panic；累计23次HakiOS临时启动，P2整体未通过。

[R24](reports/hakios-device-r24.md)确认原始df（31908ms）与mount（5209ms）均正常exit0、无超时或清理kill，DYNRESULT PASS，120236ms后正常返回、无panic。[动态工具子项](reports/hakios-p2-dynamic-closure.md)结案；下一步沿完整HakiOS推进原始launchd。P2整体和系统界面尚未通过，累计24次临时启动。

[原始launchd测试包](reports/hakios-p2-ios14-launchd-ready.md)已构建并冻结：保持R24完整内核，由上游init完成HFSX根切换、Data目录初始化和PID1 exec，加载原始notifyd/cfprefsd及诊断任务。165项冻结证据与独立解包通过；真机交接和原始服务进程待验证。系统卷保持物理只读，本轮无需改分区。

[R26交接修正包](reports/hakios-p2-ios14-launchd-stdio-ready.md)已冻结：修正原始launchd对终端stdout的启动条件，保留上游原始错误/abort/系统调用摘要。完整内核与R24/R25一致；具体原因和稳定PID1仍待真机。R26已结束并归档；此冻结包不再启动。R27/R28均已结束归档；R28已取得进程存在性PASS，完整服务仍待验证。

## 活动结构

| 路径 | 用途 |
| --- | --- |
| `config/hakios-lithium.json` | 唯一基线、来源/依赖固定、系统目标、P0门槛 |
| `ports/hakios-lithium/` | MIX配置、平台补丁、完整Mu板层与加载契约 |
| `scripts/hakios.py` | 状态、审计、隔离checkout、检查及有门槛的上游构建 |
| `scripts/run-wsl-limited.ps1` | HakiOS任务；默认检查，固定资源并拒绝并行大任务 |
| `project.json` | 活动HakiOS状态、目标、设备最后观测；旧运行标志归档 |
| `upstreams.lock.json` | 固定HakiOS，标明其他项目只作资料/组件 |
| `upstream/hakios-xnu-kernel/` | 保持原样的完整上游来源 |
| Ubuntu `/home/jia/ios-mix1-build/hakios-lithium` | 从固定来源克隆的完整隔离构建树，映射devices/lithium |
| `reports/hakios-*.json` | 来源、配置、隔离工作区的可追溯记录 |
| `reports/history/hakios-refactor-20261114/` | 重构前状态和脚本原文、308个项目文件的哈希清单 |
| `ports/lithium/`、`boot/darwinboot-ios14/` | 原位冻结的7195历史实现，只读对照 |

原厂固件、DTB、运行时内存、显示、分区及iOS14输入继续保留。原构建树和镜像原位保留，禁止重建、打包、启动或兜底使用。旧执行脚本已封闭，其原文存为只读.txt；详见 [入口说明](scripts/README.md)。

## 使用

```powershell
Set-Location F:\IOS-MIX1
python scripts/hakios.py status
python scripts/hakios.py audit
& scripts/run-wsl-limited.ps1 -Task hakios-prepare
& scripts/run-wsl-limited.ps1 -Task hakios-check
python scripts/test-hakios-workflow.py
```

`hakios-kernel`在有效P0证据下执行完整上游构建；`hakios-deps`、`hakios-support`、`hakios-storage`、`hakios-dynamic`、`hakios-package`分别准备依赖、构建支持组件、构建存储诊断根盘、构建完整init动态测试根盘、静态核验镜像。准备checkout和生成defconfig不构建内核、不启动设备。WSL为16GB、12个逻辑处理器、2GB swap；任务内存上限14GB、CPU配额1200%、依赖与内核并发12，大任务顺序执行。资源值统一来自[配置](config/hakios-resources.json)。

R24原始动态工具子项已经通过，P1与R20存储不再复测。R28原始ps与所选进程存在性子项已结案；服务请求、显示、物理触摸与SpringBoard进入联合集成，继续使用UFS按需读取。准备入口为`& scripts/prepare-hakios-ui.ps1`，沿统一资源配置与同一互斥WSL任务执行，当前生成配置与显示补丁草案、编译诊断，不构建或启动新内核。测试采用结果保留，无需守屏或抢拍。

保留上游许可证。固件、IPSW、系统镜像、原始设备日志和构建产物仅本地保存，由.gitignore排除；本项目不提供Apple系统镜像。
