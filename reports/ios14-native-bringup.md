# MIX 真机 XNU 引导记录

更新：2026-11-14。第十六轮照片确认根卷以可写、journaled方式挂载，mount-phase-1通过；mount-phase-2因必需FactoryData源目录缺失退出66，launchd主动panic。另有HFS同步ioctl不匹配的ENOTTY问题。分析已完成，按用户要求暂停；SpringBoard尚未验证。详见 `ios14-mount-phase2-analysis.md`。

| 实机轮次 | 内核构建标签 | 最后实际输出 | 结论 |
| --- | --- | --- | --- |
| 1 | 38a7cfa8a32290ac | XNU LITHIUM 07: entering arm_vm_init | 首次真机进入XNU；01—05/07日志已由照片确认；arm_vm_init内停住 |
| 2 | 9dbf416f504459c6 | XNU LITHIUM 08: VM22 TTBR1 switched | early_random、页表分配、段保护、physmap均已返回；停点在最终TTBR1与屏幕别名交接处 |
| 3 | 6088e623eb9eb58d | VM screen L1 后 PANIC；PC=0xfffffff007306c34 | 新增诊断提前访问尚未生效的 physmap 地址，读取 L2 失败；未到 VM22 或 AT/PAR，原第二轮停点尚未解决 |
| 4 | ae93c92257f7b114 | 09/11/13/15/16/19；zone_init 后 pmap_ttne PANIC | 屏幕别名及内存初始化已继续执行；故障VA落在未保留的屏幕L2块中，需要补齐kernel_map保留登记 |
| 5 | 482965bab13cd19ee | SGI1 verified；DLIL/lo0 scheduled；TCP AES PANIC | 0016 保留区修复通过；TCP 初始化调用 AES 时 g_crypto_funcs 为空 |
| 6 | 895b81f94804434a0 | !BSD；Waiting on IOMedia / Apple_HFS | 0017 真实 AES CBC/ECB 修复通过，已达根磁盘匹配；没有根盘，尚未挂载文件系统 |
| 7 | a092ee4ebf1c6625de | UFS no contiguous DMA memory；probe result 12 | 7195 分配接口拒绝 ZERO/PERMANENT 标志；未访问 UFS 寄存器或发出 SCSI 读命令 |
| 8 | 5af454ac622685801 | GPT header + entries CRC verified；read-only probe result 0 | DMA修复通过，LUN0容量和userdata首块读取成功；探测结束后仍等待根设备 |
| 9 | 1afe0a00b73c4ac26 | HFSX CRC32 0x6143e5cf；BSD root ufsroot0；MD5Init panic | 正确根卷头已读回；HFS UUID调用空g_crypto_funcs，尚未完成根挂载 |
| 10 | efd37ae450521734 | journal_create写入EROFS；HFS22/root19，多次重试 | MD5修复通过；只读根卷日志初始化冲突，未挂载 |
| 11 | c4b03899cba1fe486 | HFS mounted；vfs_mountroot succeeded；root vnode installed；SHA256_Init panic | 日志修复与根挂载实机通过；地址哈希访问空SHA256 provider |
| 12 | 68eb2900c9cbecdf6 | HFS mounted；load_init_program；PID1 dyld AMFI query failed | SHA256停点已越过；dyld用户态已执行，launchd主程序尚未验证 |
| 13 | b78c4e043e78f92d4 | mac_policy_register写freehint；FAR 0xfffffff00717f0ec | 注册时机过晚，写只读策略表panic；尚未到根挂载或dyld |
| 14 | 319bd7dd2fc2ac511 | AMFI通过；fsck平台身份检查失败 | launchd启动任务检查已运行，fsck主程序尚未执行 |
| 15 | 6e0731e3d3a128a75 | launchd/fsck/mount已运行；mount-phase-1退出66 | 平台信任缓存通过；根卷升级可写失败，必需的preboot固件bind源不存在 |
| 16 | f2ea131e460ce9a98 | 根卷可写且journaled；mount-phase-1通过；mount-phase-2退出66 | 必需的FactoryData bind源目录缺失；另有HFS新版同步ioctl未处理；分析后按用户要求暂停 |

两轮均为已授权的fastboot boot临时镜像，没有刷分区、root、清除数据或重新分区。两轮USB均未提供ADB/Fastboot接口；返回Android由用户长按电源完成，不能计为自动恢复。

第三轮同样只执行临时启动。用户照片已与冻结内核标签核对，FAR=0xfffffff027dc4008 对应故障指令 ldr x1,[x23,#8]；诊断使用 phystokv 得到最终 physmap 地址，但当时 TTBR1 尚未切换。详细证据见 ios14-native-panic-6088e623.md。恢复推进后已重新核验MIX回到Android，随后执行第四轮临时启动。

第四轮照片中的构建时间14:11:29及新增诊断顺序与冻结内核一致，顶部BUILD标签已被循环日志覆盖。VM table PAR=0xff00000101dc4b00；TLBI前后screen PAR均为0x4400000083401b00，均可翻译到真实显示PA0x83401000。VM24、09、11、13、15、19依次可见；这证明屏幕指针切换、TTBR0撤销及arm_vm_init/pmap_bootstrap返回。两次PAR相同，不能据此证明第二轮停点由陈旧TLB造成；新增刷新顺序保留为页表交接的正确约束。

随后pmap_ttne报告target_level=3、addr=0xfffffff0038ec000、tte=0x0060000082000605。该地址属于屏幕别名块[0xfffffff002000000,0xfffffff004000000)，描述符也与本次VM screen L2完全相同。7195非KTRR分支的pmap_virtual_region只保留low globals首个32MiB，未保留相邻屏幕块，故kernel_map把该处视为可分配。0016补丁将原low globals保留区扩展至64MiB，并断言不侵入向下取整后的内核镜像区域，不关闭pmap断言或允许覆写显示映射。详细记录见ios14-native-panic-ae93c922.md。

第一轮照片内加载器和XNU日志叠加，是两个控制台写在同一缓冲区。PHYS/BOOTARG在加载器内曾按32位格式截断显示，实际完整64位地址已在payload和boot_args中传入。第二轮已清除旧加载器正文并加入内核64位参数输出；没有按屏幕截断数字更改真实装载地址。

当前页表参数来自MIX已测资料：RAM上方窗口[0x100000000,0x120000000)，显示0x83401000/BGR24/stride3240。最终屏幕别名位于7195的LOW_GLOBAL_BASE_ADDRESS +32MiB，映射一个物理32MiB块0x82000000，实际显示偏移0x1401000。第三轮检查实际L1/L2描述符、TCR和AT S1E1W/PAR_EL1结果；通过地址翻译后才测试一次字节写入和切换屏幕指针。

只有实机结果用于验收。构建/静态检查用于约束输入，当前未启动任何QEMU。核心证据为ios14-native-history.json及每轮reports/local目录内冻结镜像、kernel、ADT、commands.json、result.json。第一轮照片另附SHA256，见历史记录。

第五轮的 PC=0xfffffff0076d74d4 位于 aes_encrypt_key128 的 ldr x0,[x8,#0x78]，x8=0；LR=0xfffffff0075159c0 位于 tcp_init 的 TCP Fast Open AES 设置调用之后，FAR=0x78、ESR=0x96000005。0017 复用固定版本 BearSSL 位切片 AES，为旧 XNU 的 CBC/ECB API 提供真实实现；18 个 NIST/FIPS197 向量及原地操作、复制上下文等检查通过。没有以空操作绕过加密；GCM/XTS 等接口不在该补丁范围。

第六轮照片构建时间 14:27:16 对应冻结内核 895b81f94804434a0501d9ed32bc37739b66e443dabb95d2eb5ffce76ddae1f1；镜像 bae51cc27d8e1c363a31e3606f04ff914415e29ed533414017dac678a80930e9。照片见 reports/local/20261113-142948-947051-native-xnu/native-xnu-root-wait.jpg。可见 low globals + framebuffer reserved (64 MiB)、SGI1 verified、DLIL 和 lo0 线程至少调度一次、!BSD 和根匹配等待。没有证明 timer IRQ 投递、持续调度稳定、根盘挂载或 launchd 启动。用户随后手动恢复 Android，ADB 核验 lithium / boot_completed=1。

六轮均为临时 fastboot boot，没有 root、清除或持久刷写。N1 已有真实进入证据；N2 内存初始化和初始 SGI/线程检查通过，当前推进 N3 存储和根文件系统。根匹配中的 Apple_HFS 是当前内核的匹配条件，不是对 IPSW 文件系统的检测结果：真实 iOS14.8 OS DMG 是 APFS，逻辑磁盘 7,272,429,056 字节，电脑只读读取详情见 ios14-root-inputs.json 和 ios14-root-files.json。

## 第九轮根卷安装与启动（17:29）

完整根卷SHA256 `396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`，16GiB；稀疏传输SHA256 `d9d1bb002bb53259c718ff2b1ca7f6132609ee420e2a619711ef130c11807751`，9383190364字节。36段Writing均返回OKAY，988.584秒完成；最后一段含大段零填充，实际Writing耗时535.085秒。写入日志：`reports/local/20261113-171243-160743-root-install/flash.log`。仅userdata被持久替换，不能再将本轮整体描述为仅临时启动。

此前16:48的限资源增量构建生成新内核，旧启动包没有同步更新；安装前离线检查因此停止，尚未执行手机命令。随后通过 `scripts/run-wsl-limited.ps1 -Task native-package` 顺序完成1703600条指令扫描、alias与DMA检查、重新打包和离线启动校验，全部通过。当前内核SHA256 `1afe0a00b73c4ac2615fbcaab8425a27f6892ec9f35f41d99301ffe8108cb859`；启动镜像SHA256 `b33ea650354596bd3a46357bc72bf4d6afef72ab60e2b4be2e251790e6ffcb14`，16318464字节。

写入完成后直接从Fastboot执行第九轮临时启动，Sending及Booting均返回OKAY。冻结目录：`reports/local/20261113-172930-455158-native-xnu`。启动参数：`debug=0x14e serial=3 cpus=1 keepsyms=1 -v rd=ufsroot0 lithium_root_crc=0x6143e5cf launchdsuffix=release`。boot报告中的partition_writes_performed=false仅描述临时boot步骤；已完成的userdata写入记录在ios14-root-install.json。

当前屏幕证据待取得，USB未提供ADB/Fastboot；不据此判定XNU进入、根卷挂载或launchd成功。下一次观察需核对 `HFSX root header CRC32 verified 0x6143e5cf`、`vfs_mountroot succeeded`、`root vnode installed`、`load_init_program` 或实际panic。先保留照片；退出实验用电源＋音量下进入Fastboot，恢复Android须原厂recovery重建userdata。

2026-11-14更新：第九轮HFS卷头已读回，MD5Init空加密表异常已定位并修复；第十轮已临时启动，屏幕待观察。详见 `ios14-native-panic-1afe0a00.md`。

第十轮两张照片及SHA256见同轮result.json。屏幕下方后续UFS/挂载输出属于失败重试，不能算作新一轮启动或新panic。本轮没有再次写userdata。

第十一轮：2026-11-14T15:20:19.179173+08:00。内核SHA256 `c4b03899cba1fe486e7e27770ffcd302a40c4ebe2c5e09797ff571439c54f8c1`，启动镜像SHA256 `5dad1e4abf6ca3534645ca6b3a8f9b8588e5ea41ab4e6bcaf430860c36a7e9ca`。冻结目录 `reports/local/20261114-152016-948715-native-xnu`。仅执行指定MIX的fastboot boot，未重刷userdata；根卷SHA和CRC保持不变。启动命令成功不代表根挂载成功，等待屏幕证据。

第十一轮照片和PC/LR定位见ios14-native-panic-c4b03899.md。用户随后确认进入Fastboot，电脑核验MSM8996_A8。

第十二轮：2026-11-14T15:41:32.335323+08:00。内核SHA256 `68eb2900c9cbecdf6cc3af763dbc31ac3fd78fcb59c5516bbb84b86de83526a2`，镜像SHA256 `bb82b1da1e17c6cae8d91c79e6ffe37e0017ce9a5a13dc3c723929ac27b4b96a`。冻结目录 `reports/local/20261114-154132-200429-native-xnu`。沿用原HFSX镜像，只执行临时fastboot boot，没有新增userdata写入。发送成功不代表实机已越过SHA256或进入launchd。

第十二轮照片已确认根挂载与PID1 dyld执行，因AMFI策略查询失败退出，详见ios14-native-amfi-policy.md。用户已返回Fastboot并再次核验设备。

第十三轮：2026-11-14T16:08:02.943098+08:00。内核SHA256 `b78c4e043e78f92d48cebc0ee780bce5aa9d72a49b8637b05889fea9f5ea0c9b`，镜像SHA256 `d2a95010ba2a72b5836603e95a86fcada3f0be3f740e3629279dc3a742b25d3e`；冻结目录 `reports/local/20261114-160802-831700-native-xnu`。AMFI查询返回零可选权限，26项检查通过；旧MD5/SHA256/HFS/DMA/页表检查及1704693条指令扫描通过。仅临时启动，未新增分区写入。Sending/Booting均OKAY，USB未出现ADB/Fastboot接口，屏幕证据待用户提供。

第十四轮：2026-11-14T16:17:50.319339+08:00；内核SHA256 `319bd7dd2fc2ac51164ebfeb68e4b70afbb4fc626e77cee1f6af7178070794b1`，镜像SHA256 `ebd69b72ff2e7e6b6a0a1f657a249585090632ce56454c7b48e93c04f91739e1`。冻结目录 `reports/local/20261114-161750-210922-native-xnu`。真实注册/只读边界和native调用顺序检查通过，仅临时boot，没有新分区写入；屏幕证据待取得。

第十四轮照片已确认：HFS根挂载、AMFI dyld查询flags=0通过；launchd执行到posix_spawnp后的csops状态检查，报 /sbin/fsck 缺少平台身份。已根据真实launchd反汇编定位，不把spawn成功当作fsck主程序运行。随后用户误入小米/MIUI启动标志或动画，已回Fastboot，根卷完整性待实际读回。

第十四轮后误入Android检查完成：用户仅看到小米/MIUI标志或动画。2026-11-14 17:05完成整个16GiB userdata根卷读回，SHA256与安装源 `396e0b17...194ef6b3` 一致；fsck/launchd/dyld文件也分别匹配。无需重刷。仅RAM诊断中init设置磁盘只读；未挂载userdata、未持久root、未新增分区写入。完整报告为ios14-root-integrity.json。

第十五轮：2026-11-14T17:06:43.661461+08:00；内核SHA256 `6e0731e3d3a128a75b5bdff00a343ac3a0acec057b3222cdd8c30266c517420e`，镜像SHA256 `7e9bef39d06305e0a0a136a8594ec0d9d4271de67596fa0bac6ad1927d7c5638`。固件缓存身份支持及现有检查通过，1704766条指令扫描通过；仅临时fastboot boot，Sending/Booting成功，屏幕待观察。冻结目录 `reports/local/20261114-170643-581033-native-xnu`。不能将命令成功计作fsck/launchd或SpringBoard实机通过。

第十五轮屏幕结果：照片native-xnu-mount-phase1-panic.jpg及SHA见同轮result.json。fsck明确跳过OS容器检查；mount_hfs返回EROFS，随后必需的preboot固件目录bindfs lookup返回ENOENT，对应mount-phase-1 exit66用户态panic。原卷全部16GiB在本轮前读回一致；本轮无新增持久写入。用户恢复Fastboot后已核验54223ce9 / MSM8996_A8。

第十六轮：2026-11-14T17:48:21.109190+08:00。内核SHA256 f2ea131e460ce9a98351cd1f48396ca0e448f208058d7818f0f8f5647bee38e5，镜像SHA256 de646823a6f48c2e08ff6a3ccbf19ab0009f8cb96034816e561661769ae0dabc，镜像18530304字节；冻结目录 reports/local/20261114-174820-870395-native-xnu。新增64MiB有界RAM写入层和593块preboot副本，已通过原卷329334项比对、复制文件验证、fsck、真实strategy/adapter故障测试及最终内核调用检查。物理UFS读传输和GPT/DMA函数逐字未改；未新增持久写入。启动参数含lithium_root_cow=1 vm_compressor=2。Sending/Booting均OKAY，USB未出现MIX的ADB/Fastboot；根卷可写、日志和mount-phase-1结果待照片确认。

第十六轮照片后续观察：`native-xnu-mount-phase2-panic.jpg` 已归档，哈希及观察详情见同目录 `observation.json`。根卷报告可写且journaled，launchd经过第一阶段挂载、data-protection、finish-obliteration和commit-boot-mode；第二阶段必需FactoryData源路径lookup返回ENOENT，mount退出66后触发用户态panic。原始根卷清单也缺少该源目录；`0x19`同步警告另对应HFS调用新版DKIOCSYNCHRONIZE而驱动只接收旧版DKIOCSYNCHRONIZECACHE。主机测试未覆盖全部实机目录及ioctl需求，不能据既有通过结果称存储栈完整通过。用户明确要求分析后停止，本次仅完成只读定位、照片归档与记录更新，没有继续修复、构建、打包、模拟或设备操作。
