# HakiOS lithium 首个实机测试包

2026-11-14T19:37:31.901869+08:00。工程重构、完整依赖准备、内核/加载器/固件构建与临时启动包静态核验已完成，**可进入 MIX 真机平台测试，尚无 HakiOS 真机通过证据**。

唯一内核基线是完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU `8796.121.2`。Apple XNU 8796、完整 HakiOS patch/files/drivers、HFS、pthread 和 firehose 均沿上游流程准备。原始 checkout 保持 clean；98 份旧源码、75 份原脚本及6份历史文档逐字节保持冻结。没有执行旧7195内核或 QEMU。

本机差异限于 ARMv8.0 能力选择、GIC stride、BGR24 framebuffer 串口组件、MIX Mu 板层和加载契约。复用来源及哈希见 `hakios-firmware-sources.json`、平台补丁 provenance 和 `hakios-dependencies.json`。完整 Mu 固件使用其正常依赖，BDS 直接 LoadImage/StartImage HakiOS EFI；BootServices 保留供上游日志返回机制使用。首包不启用 UFS、GPU、触摸及其他未适配的设备驱动。

## 固定测试输入

| 项目 | 值 |
| --- | --- |
| 临时镜像 | `artifacts/hakios/lithium/lithium-hakios-test.img` |
| 镜像长度 | 15679488 字节 |
| 镜像 SHA256 | `b051e96c41443e119d73efc54fb4e9a2380cd3c0ca7a3047318441c11e4e1768` |
| 内核 SHA256 | `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698` |
| 加载器 SHA256 | `5bc80cae7ec9a45367fc2e6cf1ee6a103cd0ce31aa28d3c528421bc226992132` |
| 固件 SHA256 | `25b59b8260c439331fa84f4a26e637ab8d48da165197c13a6a3edbb5ee7c0d6e` |
| XNUP SHA256 | `967d668435f473bd389df665a180f1926654814938049e281bc8a0b7974eeb98` |
| 初始程序 | 上游静态 `userland/src/hello.c`，通过 mockfs 作为进程1 |
| 初始参数 | `cores=1 serial=3 -v hello_wait=600` |
| XNU 独占 | `[0x100000000,0x120000000)`，Mu 分配 HOB，加载器核对描述符 |
| XNUP 保留 | `[0x120000000,0x124000000)` |
| EFI FAT | `[0x42a00000,0x42b00000)`，1MiB只读 RAM 卷 |

原厂20份 DTB 未修改；EFI FAT 回读、Linux Image/XNUP 头、压缩镜像回读一致。内核、loader、hello 执行区检查未发现非兼容 PAN/UAO、LSE 或 PAC 指令。159 个 XPACLRI 属于 ARMv8.0 安全 HINT 编码；SHA256 K256 的256字节常量与固定源码完全一致，已按数据区处理，未将反汇编数据误判为指令。

内核编译与链接通过；完整上游 `tools/make-kernel.sh` 仅接受它明确规定的 kext 符号集检查失败，其他构建错误均拒绝。最初递归 Make 自动提升并发到5时已停止，随后显式 MAKEJOBS=--jobs=2；缺少 python 命令的主机依赖已修复。最终流程保持 WSL 8GB/4CPU/2GB swap、每任务6GB/200%CPU、并发2、大任务顺序执行。

## 实机步骤与验收

首包就绪时（19:37）预检未发现手机，尚未发送启动命令。20:06首轮临时启动已获Fastboot接受，但用户报告立即自动重启到MIUI启动画面，未取得HakiOS运行证据；同次结果和冻结输入见 [首轮真机记录](hakios-device-r1.md)。以下哈希是首包历史输入，后续包以对应打包记录为准。手机 userdata 仍按既有记录为 iOS HFSX，普通重启不能保证进入 Android。后续核验原授权 MIX 处于 Fastboot 后运行：

```powershell
Set-Location F:\IOS-MIX1
python scripts/boot-hakios-lithium.py
python scripts/boot-hakios-lithium.py --boot
```

新入口重新核对授权设备标识、产品、解锁状态和 userdata 几何，再对冻结 SHA256 的 HakiOS 镜像执行一次 `fastboot boot`。不持久刷写。记录屏幕中的 HAKIOS UEFI/BDS、XNULOADER、内核日志和 `Hello from user space on Darwin!`；单次最小程序成功只证明平台闭环。P1仍需真实 timer IRQ、线程定时睡眠/唤醒及切换连续10分钟、3次复现。内核返回后加载器保持日志画面，需按键回 Fastboot。

## 兼容边界

原始 D20AP DMG 已重新只读挂载并核对真实 dyld、launchd、backboardd、SpringBoard 的 14.8/18H17 身份与 Mach-O 依赖。commpage 定义差异和实际共享缓存字符串见 `hakios-ios14-boundary.json`，字符串存在不能证明接口兼容或 GL renderer 可达。

A530 的有限候选是固定 ios-sailfish 中的同代 buffer/submit/SMMU/固件硬件组件，加 Mesa freedreno GLES，再按 HakiOS IOSurface 接口集成；该候选原针对iOS16，18H17 EAGL/CoreAnimation ABI及GL回退仍需验证。没有采用 A642L 的 GMU/RPMh/a660 路径，也没有宣称 A530 Metal 已支持。P0 闭合的是输入、方案和加载实现，P2/P3仍须验证 iOS14 私有接口与图形闭包；需要重建通用服务或整条私有图形栈时按 PLAN 提交具体阻塞。
