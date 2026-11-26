# iOS16迁移阶段三：用户空间和UFS/Data

2026-11-16。阶段三已完成：电脑端用户空间、存储布局和完整内核/固件构建通过，userdata已写入并完成有边界的采样核验，手机停在Fastboot。阶段四iOS启动调试仍须用户确认，本阶段没有启动iOS。

## 来源与上游路径

仅使用用户供给的 `D:\iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw`：16.5 / 20F66 / iPhone10,3 / D22AP / arm64。完整IPSW SHA256为 `c1091c401f67e3aa7cdfeb2cc19e6c79be3c957aae4e097a019e9ccea07d52f2`；四项实际使用的ZIP成员均完整解压核验CRC及SHA256，与私有缓存逐字节哈希对应。

活动内核保持完整HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。按上游 `make unpack`、真实DeviceTree身份生成、`make images`、完整内核和Mu流程构建。init、注入库和MiniIOSurface继续使用上游完整实现；没有运行旧7195或QEMU，也没有混入旧14版运行时适配。

原始System APFS已由上游UDIF读取器和本地固定BSD LZFSE解码器提取，六个关键原始文件经内核APFS读取器读回与解包来源比较：SystemVersion、launchd、两个containermanagerd、backboardd、SpringBoard。真实D22AP加载器已替换阶段二的占位身份。

## 存储布局

沿用原始GPT中的一个 `userdata` 分区，原始Qualcomm类型GUID不变；不创建额外GPT分区。内部布局与HakiOS flash路径相同，系统和两个cryptex在UFS按需读取，Data使用可写、区分大小写、带日志的HFSX。

| 内部区域 | userdata内字节偏移 | 字节数 |
| --- | ---: | ---: |
| 原始System APFS | 0 | 5628755968 |
| 原始OS cryptex APFS | 5628755968 | 2726297600 |
| 原始App cryptex APFS | 8355053568 | 10485760 |
| Data HFSX | 8365539328 | 13109297152 |

合计20GiB。安装稀疏文件7915843396字节；所有System/cryptex块均显式RAW/FILL发送，仅Data分配位图证明为空闲的块使用DONT_CARE。独立重读稀疏文件并比较全部RAW/FILL内容、所有跳过块和最终长度通过。DONT_CARE不意味着旧userdata内容得到保留；这次操作授权的范围是替换userdata。

小型Redwood根卷和flashboot引导卷各4MiB，保存在私有E盘。没有把完整系统或共享缓存改成RAM加载方案。Apple程序、DeviceTree、镜像和固件均不进入Git或共享材料。

## 容器、权限和服务

完整上游用户空间构建包含413项launchd配置，两项container daemon、backboardd和SpringBoard均在清单中。任务配置0644，目录/程序按上游权限生成；三个生成APFS容器均完成apfsck和内核读取器全量读回。冻结前另外直接读取实际APFS inode，核验全部任务和相关目录、init、原始System关键程序，共421项权限/所有者检查通过。

注入配置恢复上游16版同时针对backboardd、containermanagerd、containermanagerd_system的完整路径。Data目录由上游骨架生成，补齐 `containers/Shared/SystemGroup` 和 `Shared/AppGroup`；其所有者root、权限0755。`mobile` 保留实际骨架的0711、uid/gid501，没有猜成0755。Data实际镜像的文件内容、所有者、权限、目录和符号链接经导入器读回核验，fsck通过。

拟用启动参数为 `root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1`，本阶段未启动iOS。上游init挂载失败时可能使用ramfs；阶段四必须区分真实HFS可写挂载与该回退，不能把回退当持久Data成功。配置存在也不等于containermanagerd已经应答，旧error51/空路径崩溃尚无本轮真机修复证据。

## 编译修正与验证

1. 既有ld64不支持用户空间SDK所需TAPI。仅用户空间切换到本机LLVM18 Mach-O linker，显式声明ios16.5平台；内核仍用正常原链接器。没有删功能或用预编译用户程序绕过构建。
2. SDK的2696项TBD含CRLF，LLVM TextAPI不识别起始标记。私有SDK副本仅将这些文本转为LF并逐项记录前后哈希；原SDK保持不变，未改Apple程序。
3. Mu固定版本的GPT定义在 `Uefi/UefiGpt.h`；新增板层检查最初引用了不存在的头文件，已按实际接口修正并完成完整Mu构建。
4. 上游只读板层扩展为Data范围内WRITE(10)及SYNCHRONIZE CACHE(10)。板层先核验实际GPT的头/表CRC、userdata名称/类型/几何/唯一GUID和重叠，再开放写入；内核、加载器、BlockIO及底层命令各自限制Data末端。超时保留DMA并隔离该传输，不自动复用或重试写入。

实际C代码13项GPT场景、读写方向、边界溢出、flush、状态/tag/residual和超时处理经ASan/UBSan主机模拟通过。Windows安装器另有19项GPT异常/截断测试和真实文件读锁测试；这些是主机检查，不是UFS硬件耐久性证据。最终内核、加载器、板层及自编译Mach-O用户程序合计46份目标指令扫描通过；扫描范围不替代真机SIGILL定位。

阶段三冻结内核SHA256：`eb4a2080a5c472a90008a90ed63a23be75a370efa418f1af02aac5ada99a1885`；Mu FD：`ba1923fd382e5a3eac97b6e099e5adf9920cd08fff5deb4e56011b973d542902`；真实身份loader：`cc40442372089bbd42a9bccd8542b55e3f86108a17914ac4209dcbdca53876e4`。阶段二冻结目录保留。

## 设备安装与阶段边界

电脑已重新核验目标Fastboot身份、解锁、电池和userdata容量，并通过原厂辅助内核只读取得实时GPT。该辅助包使用逐字节一致的原厂Linux内核/DTB，只提供固定GPT与头部采样，不挂载块文件系统、不实现块写入；它不是另一活动内核或HakiOS验收。

前两次安装流程均在flash前停止。19:34首次辅助启动后没有ADB，补齐了与此前已验证辅助包相比漏带的本机公钥；第二次已成功枚举ADB，但读取器误把固定只读命令也限制为root，实际adbd为UID2000。通过只读sysfs/proc读回确认原始分区几何、两层块设备RO保护和无磁盘挂载后，修正为仅setup需要root，其余命令允许shell但要求几何及两层只读标志均匹配，并开放对应节点只读权限。实际C路径通过ASan/UBSan检查；修正后前后两次真机采样均成功。第一次无ADB的单一根因仍不能仅凭补钥匙断定。两次故障保存在 `hakios-ios16-install-attempt1.json` 和 `hakios-ios16-install-attempt2.json`。

第三次流程于19:48开始，实时GPT头/表CRC、原始userdata类型/位置/容量及无重叠检查通过，回到Fastboot后锁定稀疏文件并重新校验完整SHA256。仅执行一次 `fastboot -S 256M flash userdata`，30段全部成功，耗时284.595秒，无自动重试、其他分区刷写或GPT变更。镜像SHA256为 `8b54b946b0f7336cb9cfd097127b627213b943f47174761a22b07e43e7ad06ce`。

写入后再次启动只读辅助包，读回的24KiB GPT样本与写入前逐字节一致；System、OS cryptex、App cryptex、Data四处各8KiB头部与冻结来源逐字节一致，共32KiB。此采样不能称作全盘读回，也不证明XNU中的HFS可写挂载和flush已经工作。手机已通过辅助ADB回到可枚举的Fastboot。安装结果保存在 `hakios-ios16-install.json` 和 `hakios-ios16-install-attempt3.json`。

累计4次stock辅助boot命令被接受，其中3次有ADB运行证据；历史HakiOS启动次数仍31次，iOS16启动0次。`hakios-ios16-userspace-ready.json` 保留为安装前冻结记录，其device_install_pending是生成时状态；最终安装状态以本阶段报告及安装报告为准。

阶段三按用户要求报告后停止。阶段四确认后，下一步用本阶段冻结内核、Mu、真实D22AP loader和小引导卷打包联合镜像，从内核/launchd前开始采集日志，联调原始服务、backboardd与SpringBoard。下一风险是扩大内存窗口的真实启动、UFS Data写入/flush及挂载、容器服务应答和软件绘制；同时验证USB枚举/日志。当前XNU窗口2880MiB，6GB全部可用仍未实现；辅助Linux ADB成功不等于HakiOS USB日志成功。USB真实传输、物理触摸、A530加速和SpringBoard显示均未通过，不能沿用旧iOS14证据。

机器可读证据：`hakios-ios16-userspace-inputs.json`、`hakios-ios16-userspace-build.json`、`hakios-ios16-storage-ready.json`、`hakios-ios16-phase3-build-checks.json`、`hakios-ios16-userspace-ready.json`及对应主机检查报告。
