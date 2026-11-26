# 第十二轮 dyld 策略查询异常与兼容接口

当前更新：第十四轮照片已证明早期注册、根挂载和 PID1 查询成功，launchd 已执行至 `/sbin/fsck` 平台身份检查。下文单一查询回调的描述对应第十三、十四轮版本；第十五轮准备增加独立固件 CDHash 成员回调，见 `ios14-firmware-trust.md`。第十四轮后的误入 Android 事件另以 `ios14-root-integrity.json` 记录完整根卷读回结果。

2026-11-14。第十二轮内核 `68eb2900c9cbecdf6cc3af763dbc31ac3fd78fcb59c5516bbb84b86de83526a2`，照片归档于同轮 `reports/local/20261114-154132-200429-native-xnu/native-xnu-dyld-policy-panic.jpg`，SHA256 `e9bf21166773b20e8e62a03c5aa1ec8a6018ed7ab0a68603df32e451744e9f66`。

照片先显示 HFS 根卷挂载、root vnode 安装成功，随后 `load_init_program` 尝试加载 `/sbin/launchd`，PID 1 以 namespace 6、subcode 0x9 和 `amfi_check_dyld_policy_self() failed` 退出。该错误字符串来自 dyld，证明已执行 PID1 的 dyld 用户态代码；未证明 launchd 主程序或服务开始运行。原 SHA256 地址哈希停点已越过。

`Failed to open swap file 30` 是只读根卷上的 EROFS，独立于当前致命的策略查询失败。UFS 根设备仍只读，本轮不修改 swap 或根卷。

## 实际固件 ABI

使用已有 libdmg HFS 读取库，以只读 mmap 打开电脑上的安装源镜像；任何写回请求都会终止提取。`scripts/inspect-ios14-dyld.py` 提取的 `/usr/lib/dyld` 为 733616 字节，SHA256 `54fd7d7f710554dc976dddcdb0902fe7a779d37634c0ffc343b6febca61e6c2b`，与 IPSW 文件报告完全一致，内部版本为 **dyld-851.31**。

反汇编 `_amfi_check_dyld_policy_self`（VA 0x475b8）显示：

- 调用 `__mac_syscall`，策略名为 `AMFI`，操作码 0x5a；BSD syscall 为 381。
- 请求是两个连续的 uint64：输入标志、用户态输出地址；请求长度16字节，输出长度8字节。
- 返回非零时，dyld 0x6c54 跳至失败分支；成功时读取输出位0至6，设置七个权限布尔值。
- 输出0是有效的限制策略，关闭 @路径、环境路径、自定义共享缓存、旧式回退路径、打印环境选项、忽略插库失败及库符号替换。

公开相邻版本 `apple-oss-distributions/dyld` 的 `dyld-851.27/src/dyld2.cpp`（Git blob `85ff67f7d27ec8cda2c6c3136e5bc466a6edcc38`）第5311—5380行与实际二进制的检查和权限位含义一致。它不是固件851.31的完整源码，实际调用ABI以上述二进制为准。源摘录保存在 `reports/local/dyld-851.27-policy-source.txt`。

另从同一根卷只读提取 `/sbin/launchd` 并核对原SHA256，其直接依赖为 `/usr/lib/libSystem.B.dylib` 和 `/usr/lib/libobjc.A.dylib`，均为绝对路径；限制策略是否足以完成其余加载仍需实机验证。

## 修复及验证范围

`0024-lithium-dyld-policy.patch` 在 LITHIUM 且启用 MACF 时注册唯一名为 AMFI 的查询策略。第十三轮初版放在 `vfsinit()` 后，实机证明该位置晚于只读保护，已按下述记录修正为 `mac_policy_initmach()` 内的早期注册。复用固定 ios-sailfish 0139 的已知请求 ABI；返回标志选择0，不采用参考项目0x7b。仅实现 `mpo_policy_syscall`，不提供签名、entitlement、凭据等回调，也不改变已有验签逻辑。它不是完整 Apple AMFI 实现。

未知操作返回 ENOPOLICY；未实现的32位数据ABI返回 ENOTSUP；请求和输出使用内核 copyin/copyout，保留真实错误码。输入位不授予额外权限，空输出地址返回 EFAULT。注册失败直接 panic，PID1查询成功时输出 `LITHIUM AMFI: PID 1 dyld policy query accepted; flags=0`。

`scripts/validate-ios14-native-amfi.py` 已完成26项主机检查：运行实际适配器和真实 XNU `__mac_syscall`，以受限用户内存模型检查参数边界、错误传递、重叠/非对齐缓冲区、静态和动态策略路由及解锁；ASan/UBSan通过。主机 copyin/copyout 为模型，不能替代真机地址空间验证。

此外检查最终 Mach-O 的真实 MAC 操作表，仅一个非零回调且指向该查询函数；策略名称和注册调用正确，输出确实从零值复制8字节。非LITHIUM及关闭MACF时编译为空。真实固件 wrapper、syscall号、成功/失败分支和七个权限位均与预期匹配。新内核SHA256 `b78c4e043e78f92d48cebc0ee780bce5aa9d72a49b8637b05889fea9f5ea0c9b`。

验证报告为 `reports/ios14-native-amfi-validation.json`，纳入打包和临时启动输入冻结链。下一轮仅从已核验的MIX Fastboot临时启动，观察PID1查询成功日志及后续共享缓存/动态库加载结果；不据离线通过声明launchd已运行。

第十三轮已于 2026-11-14T16:08:02.943098+08:00 执行临时启动，Sending/Booting均成功。镜像SHA256 `d2a95010ba2a72b5836603e95a86fcada3f0be3f740e3629279dc3a742b25d3e`，屏幕观察仍待取得。

## 第十三轮实机回归与注册时机修复

新增照片SHA256 `e636fa36a717dcd72fda99880c3122bcb7a355e40935b190fa1e08814082be28`。PC `0xfffffff0077e5dbc` 位于冻结内核 `mac_policy_register+0x3b8`，指令为 `str w8, [x9, #0xec]`。x9为 `0xfffffff00717f000`，FAR `0xfffffff00717f0ec`，正好是 `mac_policy_list`（`0xfffffff00717f0d8`）加0x14的 `freehint` 字段。ESR `0x9600004f` 表示当前EL的数据写入遭遇三级权限错误。

实际7195启动路径为 `mac_policy_initmach()` → `machine_lockdown()` → `arm_vm_prot_finalize()` → `bsd_init()`。移动平台的 MAC 表和条目位于 `__DATA_CONST`；finalize将其改为只读。第十三轮从bsd_init注册时写表触发panic，尚未到HFS注册、根挂载或dyld。上一轮26项检查使用了注册函数替身，未覆盖这个启动生命周期，因而未发现该问题。

已修订同一0024补丁：移除bsd_init注册调用，在 `mac_policy_initmach()` 原有扩展加载完成后、`mac_late=1` 前调用注册。策略设置 `MPC_LOADTIME_FLAG_NOTLATE`，误用晚期注册会先返回EPERM。保留原策略表只读段归属和ARM页保护代码。

验证新增11项，运行从真实源码提取的 `mac_policy_init`、`mac_policy_initmach`、移动平台 `mac_policy_register` 和 `__mac_syscall`，保留无关锁/标签/分配依赖的主机替身。用真实mprotect保护表与条目，验证早期注册后仍可执行查询且不写表；无NOTLATE的旧顺序在独立子进程中按预期触发只读写异常。还检查现有扩展先加载、重复/容量满/标签槽耗尽、晚期注册被拒等路径。ASan/UBSan通过；共26项接口检查和11项注册检查。

最终内核额外检查实际调用顺序、bsd_init已无注册调用、mac_policy_initmach仍有注册调用、策略表仍在 `__DATA_CONST`，并以旧冻结内核故障指令作为反例。新内核SHA256 `319bd7dd2fc2ac51164ebfeb68e4b70afbb4fc626e77cee1f6af7178070794b1`。真机能否继续到dyld仍待第十四轮照片。

第十四轮于 2026-11-14T16:17:50.319339+08:00 临时启动，Sending/Booting均OKAY。镜像 `ebd69b72ff2e7e6b6a0a1f657a249585090632ce56454c7b48e93c04f91739e1`，冻结目录 `reports/local/20261114-161750-210922-native-xnu`；屏幕结果待观察。
