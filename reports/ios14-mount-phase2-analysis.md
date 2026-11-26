# 第十六轮 mount-phase-2 panic 分析

日期：2026-11-14。用户要求：**“分析原因，分析完之后先停下来，不用继续推进了”**。本次仅分析和记录，未修改移植实现、构建或打包镜像、运行模拟器、查询或操作手机。分析完成后暂停，后续工作需等待用户恢复推进的指令。

第十六轮已越过 mount-phase-1，停在 mount-phase-2 的必需 FactoryData bind 挂载。直接因果链是：源路径不存在 → BindFS lookup 返回 ENOENT(2) → mount 以 66 退出 → launchd 判定关键启动任务失败，三秒后主动触发 userspace panic。

## 照片与对应启动

- 启动时间：2026-11-14T17:48:21.109190+08:00。
- 冻结目录：`reports/local/20261114-174820-870395-native-xnu`。
- 内核 SHA256：`f2ea131e460ce9a98351cd1f48396ca0e448f208058d7818f0f8f5647bee38e5`。
- 镜像 SHA256：`de646823a6f48c2e08ff6a3ccbf19ab0009f8cb96034816e561661769ae0dabc`。
- 本次照片归档为同目录 `native-xnu-mount-phase2-panic.jpg`；照片哈希和观察时间见同目录 `observation.json`。
- 照片可见 `lithium_root_cow=1 vm_compressor=2`，与第十六轮启动参数相符；顶部早期 BUILD 输出不在画面中，不将照片描述为镜像哈希的独立证明。

照片显示 `/dev/ufsroot0 on / (hfs, local, journaled, noatime)`，没有 read-only 标记；随后经过 data-protection、finish-obliteration、commit-boot-mode，最终明确报告 mount-phase-2 失败。这证明第一阶段已通过、根卷报告为可写且启用日志，不等于完整日志同步协议和长期运行稳定性已通过。

## 直接原因：必需的 FactoryData 源路径缺失

源路径：

```text
/private/var/hardware/FactoryData/System/Library/Caches/com.apple.factorydata
```

目标路径：

```text
/System/Library/Caches/com.apple.factorydata
```

照片明确显示源路径 lookup failed error: 2，mount 输出 No such file or directory(2)。panic 原文为：

```text
userspace panic: boot task failure: mount-phase-2 - exited due to exit(66)
panic(cpu 0 caller 0xfffffff00780e29c)
```

现有真实 `/sbin/mount` 反汇编 `out/mount-phase1/mount-disassembly.txt` 支持该链条：

- `0x1000043cc`、`0x1000043d4` 分别载入目标路径和 `/private/var/hardware/FactoryData` 前缀，随后按 `%s%s` 拼接源路径。
- `0x1000043f8` 设置 `w2=1`，`0x1000043fc` 调用 bind helper `0x100006304`；与前面的 Pearl 挂载 `w2=0` 不同，FactoryData 是必需项。
- helper 在 `0x1000063b8` 调用 mount；`0x1000063c8` 检查 errno=2，`0x1000063d0` 在必需标志非零时转入错误处理。
- `0x100006414` 设置错误转换参数 -1，转到 `0x1000044f0`；该函数调用 `_sysexit_np`。实际退出码66由照片直接确认。

只读扫描 `/home/jia/ios-mix1-build/ios14-root-hfs.manifest.bin` 的329334条记录：目标目录存在，`/private/var/hardware` 存在且为空，未找到 FactoryData 源目录或其内容。现有 preboot seed 只复制 `/usr/standalone/firmware`，没有补充 FactoryData。根卷准备尚未提供第二阶段要求的这部分目录布局，足以解释本次失败；不需要假设手机上的文件后来损坏或丢失。

仅从目录缺失不能得出“建一个空目录就能完成系统启动”的结论；后续服务是否要求真实硬件数据未验证，本次不实施修复。

## 独立问题：HFS 同步 ioctl 不匹配

照片中的 `flushing fs disk buffer returned 0x19` 对应 ENOTTY(25)，即该设备不支持收到的 ioctl 请求。

- 实际构建树 `src/bsd/lithium_hfs/core/hfs_journal.c:468-481` 构造 `dk_synchronize_t`，调用 `VNOP_IOCTL(..., DKIOCSYNCHRONIZE, ...)`。
- `hfs_journal.c:497` 正是照片中报错的打印位置；随后继续日志头写入流程，并非在该行直接 panic。
- XNU `bsd/sys/disk.h` 中 `DKIOCSYNCHRONIZE` 是 `_IOW('d',22,dk_synchronize_t)`；旧 `DKIOCSYNCHRONIZECACHE` 是 `_IO('d',22)`，请求值不同。
- 实际驱动 `src/bsd/dev/lithium_ufs.c:474-483` 只处理旧请求，其余返回 ENOTTY。冻结的 `evidence/out/mount-phase1/cow-validation/lithium_ufs_ioctl.asm` 也只匹配旧请求 `0x20006416`，默认值为 `0x19`。
- 生成该处理分支的本地位置为 `scripts/prepare-ios14-native-cow.py:69-78`。本次未修改。

因此同步警告有明确代码原因，是现有兼容层尚未覆盖的接口；当前照片的致命链条仍是 FactoryData bind 失败。不能将“journaled”挂载标记或既有主机测试解释为完整同步语义已经正确。

定义另与 Apple 对应版本公开源码交叉核对：`https://raw.githubusercontent.com/apple-oss-distributions/xnu/xnu-7195.141.2/bsd/sys/disk.h`。实机因果判断以本地冻结产物、构建源码和用户照片为依据。

## 其他日志与结论边界

- `GetMasterBlock: Error 16` 是 EBUSY；屏幕随后仍报告根挂载及后续启动阶段，不能把它当成本次直接panic原因。
- `data volume missing, but not required in env: 0`、可选 restore-datapartition 缺失、No SEP 提示之后均继续执行；它们不是此次被报告的致命停点，也不证明相关子系统已完整实现。
- `handshake structure not initialized` 出现在 panic 报告中；没有证据将它归为此次 mount 失败的起因。
- 控制台输出有环绕，不能把整张照片自上而下视为完整时间顺序。
- 本次没有观察到 SpringBoard。没有证据表明 RAM 写入层已经耗尽，也不能仅凭这张照片证明整个存储栈稳定或重新核验全部 userdata。
- 第十六轮物理传输实现仍只读，文件写入由有界 RAM 层承载；其持久性仅限当前开机。此前误入MIUI后的完整16GiB读回一致记录仍在 `ios14-root-integrity.json`，本次没有重复读取手机。

最终状态：原因分析完成，用户要求暂停。未进行第十七轮准备或设备操作。
