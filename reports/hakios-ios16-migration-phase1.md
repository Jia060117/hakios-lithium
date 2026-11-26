# iOS 16.5 迁移：第一阶段审计与 WSL 清理

记录时间：2026-11-16T17:07:47.847907+08:00。状态：**第一阶段完成，停在阶段二实施之前。**

用户已明确将用户空间目标改为 iOS 16.5 / 20F66 / arm64；该决定覆盖旧项目文档中的 iOS14 目标。完整 HakiOS 仍是唯一活动内核，旧7195路线保持只读、禁用，不运行 QEMU。系统继续存放 UFS、按需读取。本阶段只审计、写报告和执行已授权的 WSL 清理；未修改源码或活动配置，未构建、查询/启动手机或写分区。

## 审计结论

现有活动内核已经是 HakiOS 固定提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2，并非需要从7195升版。此次工作是恢复上游16.5用户空间与ABI集成，再保留MIX硬件差异。当前缺口集中在已被覆盖的集成路径、设备资源和写入/日志链路；未发现被审计的上游跟踪文件缺失。

- HakiOS上游879个跟踪文件：864个相同、15个修改、0个缺失；另有本地新增公共覆盖文件 `xnu/files/pexpert/arm/pe_serial.c`。本次远端HEAD和main均仍是固定提交。它证明来源与文件覆盖情况，不证明所有运行功能已通过。
- 73个覆盖文件、49个驱动文件、20个公共补丁输入、85个设备层文件的来源/同步检查一致；实际公共diff与构建记录SHA256一致。Mu依赖干净，提交 `6b3d90b79e191e08171bfedd3980234a7e49d340`。
- 对281个生成内核修改/新增文件按完整上游重建比较：191个等同上游、70个来自固定HFS依赖、20个本地差异；没有未知比较问题。此比较在内存中完成，未重新应用补丁或构建。
- 现有内核SHA256：`0ee8a39755da18080a0e2b4af0c9b8250ab7273dd15d0c6dc305b2603d5e903f`；其证据仍归属既有轮次。

## 文件与补丁清单

[723项项目文件清单](hakios-ios16-migration-files.csv)、[114个补丁块](hakios-ios16-migration-patch-hunks.csv)、[16项公共文件差异](hakios-ios16-migration-common-deltas.csv)、[20项生成内核差异](hakios-ios16-migration-kernel-deltas.csv)均包含对应标签或哈希。

分类为DEVICE-SPECIFIC 76项、IOS14-SPECIFIC 602项、混合45项。602项包含报告、冻结历史和禁用入口，不能理解成602项活动代码需要重写。DEVICE标签也包含必须保留的构建基础设施。根仓库没有任何提交，723项均为尚未提交的可见文件，无法准确证明每个文件由谁修改；清单描述现存项目差异。忽略目录中的实际WSL构建树另经来源和指纹检查，未当作Git文件遗漏。

| 补丁 | 处置 |
| --- | --- |
| 0001、0003、0006 | 保留ARMv8.0/PAN、GIC、加载契约、保留摘要和UFS buf_map/unmap |
| 0002 | 拆出timer/返回观测，撤下hello专用默认值 |
| 0004、0005 | 保留UFS/PSCI及故障证据，替换HFSX专用门禁和测试耦合 |
| 0007、0008、0010—0012、0015 | 归档14版路径、D20AP身份、MACH14/df诊断及进程存在性协议 |
| 0009、0013、0014、0019 | 保留有用退出/stdio/独立诊断，撤下旧结果协议 |
| 0016 | 保留BGR24转换与控制台所有权；撤下18H17显示提交翻译 |
| 0017、0018、0020 | 归档14版IOSurface ABI、selector及R31身份钩子 |
| compatibility目录 | 14版Mach/shared-region/persona等全部归档；XML权限、退出观测等通用修复仅在与上游16版比较后按需重取 |

公共文件的实际差异：

| 路径（相对HakiOS） | 状态 |
| --- | --- |
| `Makefile` | MODIFIED |
| `drivers/storage/hakios.ufs.c` | MODIFIED |
| `userland/build.sh` | MODIFIED |
| `userland/src/hello.c` | MODIFIED |
| `userland/src/init.c` | MODIFIED |
| `xnu/files/bsd/miscfs/apfs/apfs_vnops.c` | MODIFIED |
| `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp` | MODIFIED |
| `xnu/files/iokit/Kernel/MiniIOSurface.cpp` | MODIFIED |
| `xnu/files/iokit/Kernel/MiniIOSurface.h` | MODIFIED |
| `xnu/files/osfmk/arm64/gic_v3_irq.c` | MODIFIED |
| `xnu/files/osfmk/arm64/loader_return.c` | MODIFIED |
| `xnu/files/osfmk/arm64/user_exception_trace.c` | MODIFIED |
| `xnu/hakios.patch` | MODIFIED |
| `xnuloader/build.sh` | MODIFIED |
| `xnuloader/loader.c` | MODIFIED |
| `xnu/files/pexpert/arm/pe_serial.c` | ADDED |

生成内核的本地差异（完整代码diff保存在忽略的本地证据目录，不包含固件）：

| 路径 | 标签 | 处置 |
| --- | --- | --- |
| `bsd/dev/hakios.ufs.c` | DEVICE-SPECIFIC | KEEP |
| `bsd/kern/bsd_init.c` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `bsd/kern/kern_exit.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `bsd/kern/kern_proc.c` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `bsd/kern/sys_persona.c` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `bsd/kern/syscalls.master` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `bsd/miscfs/apfs/apfs_vnops.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `bsd/vm/vm_unix.c` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `iokit/Kernel/MiniIOMobileFramebuffer.cpp` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `iokit/Kernel/MiniIOSurface.cpp` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `iokit/Kernel/MiniIOSurface.h` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `makedefs/MakeInc.def` | DEVICE-SPECIFIC | KEEP |
| `osfmk/arm64/gic_v3_irq.c` | DEVICE-SPECIFIC | KEEP |
| `osfmk/arm64/loader_return.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `osfmk/arm64/user_exception_trace.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `osfmk/ipc/mach_msg.c` | IOS14-SPECIFIC | ARCHIVE_REASSESS |
| `osfmk/vm/vm_fault.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `pexpert/arm/pe_serial.c` | DEVICE-SPECIFIC|IOS14-SPECIFIC | SPLIT |
| `pexpert/pexpert/arm/protos.h` | DEVICE-SPECIFIC | KEEP |
| `pexpert/pexpert/arm64/BCM2837.h` | DEVICE-SPECIFIC | KEEP |

## 上游集成差距及最新日志

R31确认的是：backboardd请求共享系统容器得到无效应答/error51，随后空路径传给NSURL，触发异常与SIGABRT。**容器服务未正确应答的底层原因仍未证明**；现有证据不能直接归咎于UFS格式或芯片慢，也不能承诺换16就自动消失。

上游 `userland/build.sh:136` 默认给 `containermanagerd` 和 `containermanagerd_system` 都注入适配库；当前14包只有backboardd与SpringBoard的14版适配。上游 `userland/src/inject.c:1008` 对16.5的 `MCMUserIdentityCache personasAreSupported` 进行版本/原始地址校验后处理。这是应恢复的16版集成，14版不能套其固定地址。下一阶段沿完整上游集成；真机需证明真实容器请求成功，不能以进程存在代替。

另发现迁移时必须消除的环境冲突：本地任务器把构建并发设为 `JOBS=12`，而上游 `userland/build.sh:107` 将 `JOBS` 解释成launchd任务标签列表。直接复用该环境会把“12”当成服务选择。编译并发与用户空间任务列表必须分开传递。旧14流程曾显式读取标签，因此这是迁移集成风险，不能倒推为R31既定根因。

## 设备、内存、显示与日志边界

1. 现有MIX硬件事实、原厂DTB/保留内存、GIC/timer、PSCI交接、UFS读取、面板BGR24和加载修复保留。物理FT5x46采样尚未接通，A530加速尚未验证。
2. 手机活动内核仍为单核、512MiB窗口。WSL16GiB只影响电脑构建，不增加手机可用内存。上部实测RAM为 `[0x100000000, 0x1bd950000)`；2GiB窗口 `[0x100000000, 0x180000000)` 仅为候选。现有XNUP `0x120000000` 和UEFI堆 `0x124000000` 必须一并迁移，并联查BootShim、loader、打包断言及保留区，不能只改内存大小。
3. 首次可见界面复用上游16软件绘制路径，保留BGR24输出；按实际画面更新验收。arm64而非arm64e，仍需检查实际程序/共享缓存的指令需求；Mach-O架构标签本身不能证明所有指令兼容ARMv8.0。
4. UEFI `LithiumUfsDxe.c:37` 当前为ReadOnly，写入返回EFI_WRITE_PROTECTED，Flush未形成真实持久化保证。已通过的读取无需重复验收，但不能计为可写Data通过。上游loader/kernel写保护还假定darwin分区名称、APFS类型、4096块大小和布局边界，不能直接照搬给现有userdata。
5. 完整日志链路需从launchd前开始接入，标记溢出/截断并可导出。现有1536KiB内核缓冲、loader返回分支的1MiB长度限制及UFS只读状态需联合核对；不能把照片当作完整日志，也不能假设MIX已有运行时USB导出。后续保留结束/故障摘要，无需守屏或抢拍。

## 固件与UFS候选布局

已从用户IPSW内部BuildManifest核验16.5/20F66、iPhone10,3/10,6，选择iPhone10,3/D22AP。IPSW大小6,165,698,198字节，BuildManifest SHA256为 `7dd041dc7ac05f2e1f041013024f9062b8166d916c708ff8b46aca5e089e091f`。这不是完整固件哈希校验；阶段三消费时再核验完整IPSW、所需成员及已缓存DMG之间的一致性。只使用用户提供固件；Apple程序/固件不进入Git或共享包。

候选使用现有userdata前20GiB内的逻辑区域，其他GPT分区不动；以下**仅为设计，未写入手机**。保留原System及cryptex只读，Data单独可写，不把整个系统搬到RAM。

| 区域 | 字节偏移 | 字节长度 | 模式 |
| --- | ---: | ---: | --- |
| System APFS | 0 | 5,628,755,968 | read-only |
| OS cryptex APFS | 5,628,755,968 | 2,726,297,600 | read-only |
| App cryptex APFS | 8,355,053,568 | 10,485,760 | read-only |
| Data HFS+ journaled | 8,365,539,328 | 13,109,297,152 | read-write after board write/flush and range guards implemented |

System长度来自UDIF中Apple_APFS条目（起始512字节扇区34、数量10,993,664），不是带GPT的DMG整盘长度。Data起点为2,042,368个4096字节块，符合上游布局边界；这不代表可照抄其他机型GPT/LBA。实施时重新核验本机几何，完成板层有界写入/真实flush和布局保护后，再验证 `/private/var` 可写与跨重启持久化。若首次UI排错暂用上游ramfs，须单独说明不持久，不计为UFS Data通过。

## WSL清理结果

- 两对完整镜像均先作逐字节SHA256一致性核验，再将WSL重复文件原路径换为指向E盘既有归档的符号链接：`ios14-root-hfs.img`、`ios14-root-hfs.sparse.img`。两份原始归档仍在 `E:\IOS-MIX1-artifacts\ios14-d20ap`，链接目标已在清理时验证；依赖E盘挂载，归档不是新建的独立项目备份。
- 清除了pip、pnpm下载缓存和apt下载缓存。保留所有源码、构建依赖、冻结包、失败镜像、日志、手机单次入口记录及其他项目；未触碰ace6t-build。
- 已知文件分配空间释放 **19,223,838,720字节（17.90GiB）**；WSL文件系统实际已用空间由94.43GiB降至76.58GiB，减少17.85GiB。未压缩VHDX，因此不声称Windows上的VHDX文件同步缩小。
- 审计开始时实际运行内存占用约0.8GiB，并无大型构建在跑；磁盘占用与运行内存需区分。最终复核Ubuntu状态为Stopped，未为复核而重新启动；无需额外终止，未操作其他发行版。资源上限仍为16GiB/12CPU/2GiB swap，autoMemoryReclaim=dropCache。

## 备份与下一阶段停点

根仓库没有提交，单建分支不能保护当前未跟踪文件。阶段二开始时先创建仓库外的完整项目/设备适配/关键配置及现有本地补丁的独立校验备份；大型Apple输入保留仓库外并记录哈希，按需另存。随后审查Git暂存清单，创建不含Apple二进制的初始源码快照和备份分支，旧14配置、补丁、报告和可用产物归档。**本阶段未创建备份或分支。**

阶段二：先备份，再将AGENTS/PLAN/OPERATIONS/project.json与用户明确的16.5决定同步；恢复固定上游完整ABI/服务路径，重取MIX硬件改动，处理JOBS冲突、内存交接、BGR24、写入边界和日志契约；按正常流程完整编译，逐项记录编译修复，不删功能。完成后报告并停止。

阶段三：核验用户IPSW并按上游准备arm64用户空间、System/cryptex/Data布局及正确权限，确保两个container daemon集成、共享系统容器目录和可写 `/private/var`；冻结校验包后，在既有userdata写入授权范围内重新核验本机并替换旧14镜像。完成后报告并停止。

阶段四：统一集成包真机启动，尽量完整采集launchd起点日志；区分内核前停点、daemon崩溃、SIGILL、内存或图形问题，每次针对明确问题修复，保留变更日志和结束摘要。优先观察真实SpringBoard画面持续更新，触摸和GPU能力分别记录；不重复P1、只读存储及进程存在性验收。完成该阶段约定工作后报告并停止。

阶段二首要风险是扩大手机内存时的保留区/交接地址冲突；后续主要风险为可写Data/容器服务真实请求、ARMv8.0指令及软件显示路径。没有新的真机结果前，不承诺何时能进入桌面。

最终核验：723项基线文件SHA256全部未变，16项操作意图/保护记录未变，R31仍已消费；未创建新boot入口。原始证据在 `reports/local/ios16-migration-phase1`（Git忽略），公开清单仅为源文件元数据与报告。当前等待用户核验第一阶段报告并确认阶段二。
