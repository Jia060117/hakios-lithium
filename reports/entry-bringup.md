# MIX 一代 ARM64 入口诊断实测

日期：2026-11-12。设备：lithium / Snapdragon 821。项目目标仍为 iOS 14.8 / 18H17。

**已在真机完成三次临时诊断启动，三份 512 字节入口记录完全一致；每次均正常进入 Android。** 随后进行一次普通重启，确认 boot ID 改变、诊断设备树属性消失，手机已回到原厂启动。尚未启动 UEFI、XNU 或 iOS。

## 真机结果

| 项目 | 实测值 |
| --- | --- |
| 内核加载基址 | `0x40080000` |
| 诊断入口 PC | `0x42078000` |
| 继续执行的原厂内核入口 | `0x40081000` |
| x0 / FDT 地址 | `0x42800000` |
| x1、x2、x3 | 均为 0 |
| 进入时异常级 | EL1，`CurrentEL=0x4` |
| MIDR_EL1 | `0x512f2011` |
| ID_AA64MMFR0_EL1 | `0x101124` |
| translation granule | 4K、16K、64K 均支持；字段分别为 0、1、0 |
| 物理地址宽度 | 44 位 |
| LSE 原子指令 | 不支持，Atomic 字段为 0 |
| CNTFRQ_EL0 | 19,200,000 Hz |
| SCTLR_EL1 | `0x10c00800`，M/C/I 位均为 0，即 MMU、数据缓存、指令缓存关闭 |
| 继承 SP | `0x0b1ff000`；探针不用栈，不把此值当成后续固件的可用栈 |
| 继承 VBAR_EL1 | `0x92a00000` |
| 三次捕获的异常数 | 均为 0 |

EL2 专用字段没有在手机 EL1 下读取，记录中的全 1 表示未采样，不能据此判断 EL2 不存在。

16K granule 的硬件支持已从实际寄存器确认，因此后续可以认真评估保留 iOS 14 的 16K 页语义。仍需验证 7195 内核、目标 Apple 组件和用户空间的完整分页/ABI 路径；本次结果不等于这些软件问题已解决。LSE 不能作为本机基础指令集使用。

## 镜像与执行方法

诊断镜像：`out/entry-probe/lithium-entry-probe.img`，23,425,024 字节。

SHA-256：`d600e068cbe6c975af1dccfaaa1c40b40b95a4348bd6e3e9369dfc5b4596c77a`。离线重新构建得到相同哈希。

以匹配的原厂 boot 为容器，保留 ramdisk，保留原始 Image 的第一个指令，仅修改 Image 偏移 4–7 的分支。4 KiB ARMv8.0 探针放在 Image 偏移 `0x1ff8000`，处于原厂声明的 `image_size=0x2433000` 范围内。记录完成后直接分支回原来的 `Image+0x1000`；原厂内核随后可以清零这段 BSS 范围。

20 个原厂 DTB 都保留，仅分别向 `/chosen` 增加 `mix1,entry-probe-v1`，预留 512 字节。脚本逐节点、逐属性、逐保留项比较，确认除此属性以外的语义不变。探针使用 32 位写入以适配 FDT 属性的 4 字节对齐。

探针临时安装同级异常向量，并恢复 DAIF/VBAR。模拟器已验证读取异常/未定义指令的记录和跳过路径；**手机本次只执行正常采样，未故意注入异常**。跨异常级的 trap 不由此探针处理。

原厂镜像末尾的 1,354 字节 ASN.1 Android BootSignature 对修改后的内容无效，诊断包明确去除该签名，重新计算 legacy boot SHA-1 ID。已实测解锁的 Fastboot 接受此无签名镜像。未修改安全固件或刷入签名密钥。

真机使用 `fastboot boot`，未写入 boot/recovery 分区。通过 Android 恢复后的 `adb exec-out` 二进制读取设备树属性取回数据。无需 root、清除或手机备份。

## 验证与复现

在 Windows PowerShell 的项目目录运行：

```powershell
python scripts/analyze-aboot.py
python scripts/build-entry-probe.py
wsl -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/validate-entry-probe.py
python scripts/boot-entry-probe.py --boot --runs 3
```

最后一个命令会实际重启已授权且身份核验为 lithium 的唯一手机，完成后自动普通重启并验证诊断属性消失。前三个命令不接触手机。需要 Windows Python/Capstone，以及 WSL Ubuntu 的 clang、LLD、LLVM 工具、QEMU；实际版本保存在 `entry-probe-toolchain.json`。

验证包括：boot SHA-1 ID、gzip 和原厂提取器独立往返；原厂 ramdisk 一致；Image 主体只修改分支；20 个 DTB 的属性比较；探针位于原厂内核保留范围；与已知固定 carveout 不冲突。QEMU 的 10 个执行用例覆盖 EL1/EL2、两级异常恢复、标记/版本/魔数错误、总长度边界和 FDT 未对齐。检查 x0–x3、x19–x30、SP、DAIF、VBAR 保留，以及设备树属性之外不被修改。

QEMU 结果用于验证代码，不能替代 Qualcomm 硬件实测。三次真机记录的 SHA-256 均为 `e98436054c08c851481649f0b09508af952a2d5ac17b50393c2d1016e7d39025`。

## 证据与剩余工作

- `entry-state.json`：三轮入口寄存器、能力解释、临时启动结果、最后原厂重启与属性消失对照。
- `entry-probe-build.json`：镜像/探针哈希、打包参数和验证状态。
- `entry-probe-qemu.json`：10 个执行用例结果。
- `aboot-entry.json`：原厂 aboot 的 ARM64 magic、gzip、地址覆盖及 SCM 跳转路径；派生 ELF 仅用于分析，不能刷写。
- `boot-occupancy.json`：本次原厂内核串联实验的区间检查；不是完整 UEFI 空闲内存表。
- `reports/local/`：含 USB 身份的命令日志和原始记录，不纳入版本控制。

G1 的入口与 CPU 能力部分已实测。下一步需要完成 bootloader 显示扫描输出交接、UEFI 的 FD/栈/页表/日志占用表，再构建最小 lithium UEFI。Fastboot 解压暂存区、完整 FDT memreserve、动态池的后续占用仍不能由此次记录推定为空闲。Gree 的旧 FD/FV/日志地址冲突仍然存在，不能直接启动参考镜像。
