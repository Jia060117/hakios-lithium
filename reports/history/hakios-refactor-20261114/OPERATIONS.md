# 设备操作授权与当前约束

更新：2026-11-14。仅适用于本项目连接并核验身份的小米 MIX 一代 lithium。

当前状态（2026-11-14）：第十六轮照片已确认根卷以可写、journaled方式挂载并越过mount-phase-1；mount-phase-2因必需的FactoryData源目录缺失退出66，launchd主动触发panic。另已定位HFS新版同步ioctl未被驱动处理的ENOTTY警告。分析见 `reports/ios14-mount-phase2-analysis.md`。本轮物理UFS仍只读，RAM写入不跨重启保存；userdata仍为iOS14.8 HFSX。此前误入MIUI后的完整16GiB读回已与原镜像一致。

**2026-11-14 用户已确认新路线并要求写为最高项目级约束：基于完整 HakiOS 移植增加 MIX 一代设备支持，HakiOS 为唯一活动内核基线；原先7195内核只作只读对照，不再修改、构建、打包、启动或兜底。权威指令见 AGENTS.md，现行计划见 PLAN.md。**

本轮只落实项目指令、计划与状态记录，尚未构建、打包或启动 HakiOS，也没有查询或操作手机。此前“分析后停止”对应的旧路线保持停止，不能据本次确认重新推进旧7195或FactoryData修复。新路线后续工作从 HakiOS 的P0审计开始；既有必要root/清除等操作授权继续保留，不能再被解释为使用旧内核的授权。当前设备画面仍仅以最后照片为据，未核验回到Fastboot。

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

2026-11-13 17:12—17:29：安装前重新核验lithium/boot_completed、Fastboot型号MSM8996_A8、解锁状态、userdata长度250710192128字节及max-download-size=512MiB；仅执行 `fastboot -s 54223ce9 -S 256M flash userdata`。完整根卷16GiB、稀疏传输9383190364字节，36段全部成功，总耗时988.584秒，原Android userdata已替换。没有root、单独erase/format、重分区或其他分区刷写。随后保持Fastboot并临时boot新XNU；该boot步骤自身未刷分区，不能据此将本轮整体记为“无持久写入”。未运行QEMU，手机全盘读回、根挂载及用户空间尚未验证。
