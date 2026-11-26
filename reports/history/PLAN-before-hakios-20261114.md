# iOS 14 在 MIX 一代上的实机引导计划

更新：2026-11-14。目标：lithium / MSM8996pro / 原生 iOS 14.8（18H17）。

按用户最新指示，第一验收是实机正确进入 XNU，第二验收是实机正常引导 iOS 14。QEMU 不再执行，也不作为本计划任何门槛；已有模拟结果仅保留为源码调试历史。

## 当前事实

- lithium 专用 Apple XNU 7195.141.2 已完整构建，补入真实 SHA1、PRNG、单 CPU 注册和 libpthread 服务。
- 新增实际 XNU 物理入口、arm_init、MMU/内存初始化及 panic 的 BGR24 屏幕日志，完整构建和静态指令检查已通过。
- 首轮实机照片已确认进入XNU；后续已完成内存初始化、SGI1投递及初始线程调度，第八轮读通UFS和GPT。N2的timer IRQ和持续调度稳定仍未通过。
- 用户本地 IPSW 已按 BuildManifest 核验为 iOS14.8 / 18H17，支持 iPhone7/8。选定 iPhone10,1 / D20AP 为后续用户空间来源；原 n71ap 材料保留独立，不能混用身份。
- D20AP 原始内核为 7195.140.44，普通 ARM64 subtype0。移植源码 7195.141.2 与其并非完全相同，私有组件须按实际依赖核对。

## 顺序与验收

| 阶段 | 工作 | 只接受的实机证据 |
| --- | --- | --- |
| N1：进入 XNU | 原厂启动链 → 直接 ARM64 shim → 移植 XNU；单核、512MiB RAM、无根盘 | XNU 自身物理入口标记和 arm_init 编号日志，构建哈希对应本次镜像；MMU 开启后继续执行 |
| N2：内核正常初始化 | 按真机最后日志修复页表、GIC、timer、CPU/线程、BSD 初始化 | 内核日志经过初始化停点；时钟、中断和任务切换确实运行；异常包含定位参数 |
| N3：根文件系统 | 将已核验IPSW文件转换为完整HFSX，写入已授权userdata，由XNU只读挂载 | XNU 挂载指定根盘、读取 dyld 与 /sbin/launchd；不能把文件存在当成启动 |
| N4：iOS14 用户空间 | 对应版本的私有服务、dyld shared cache、必要内核接口和进程初始化 | 真机 PID1 launchd、dyld 和必要服务运行，记录实际错误并逐项修复 |
| N5：正常引导与操作 | 显示/图形/HID；SpringBoard、设置与系统应用 | SpringBoard 可持续运行30分钟，真实触摸可操作，设置及一项系统应用打开，重复启动5次 |
| N6：扩展可用性 | UFS 持久化、联网、电量/充电、音频和电源管理 | 对应硬件功能逐项实测；通话/相机/完整外设另外评估 |

N1/N2 先使用直接加载路径，复用固定 ios-sailfish 的 ADT、payload、loader 与启动代码；UEFI 不再作为进入 XNU 的前置条件。第一轮不携带 iOS 根盘。只有实机日志需要时才补相应平台服务，不凭模拟停点扩大工作。

## 第一轮具体输入

- 原厂 Android boot v0 容器、原厂20个 DTB、原厂 ramdisk 保留；操作采用已授权的 fastboot boot。
- shim 和 payload 位于已测入口0x40080000之后，完整末端必须低于实测 FDT 0x42800000。
- 内核管理窗口：[0x100000000, 0x120000000)，来自真机上方 DDR。检查已取得的固定保留区、动态池分配窗口及 XBL/aboot PT_LOAD 均不冲突；这不是完整引导器堆分配图。
- 显示：PA0x83401000、1080×2040、stride3240、BGR24。页表必须提供有效的 Normal-NC 映射，并在撤销 TTBR0 后换到实际7195低全局区旁的保留别名。
- GICD0x9bc0000、GICR0x9c00000、stride0x40000、虚拟timer INTID27、CNTFRQ19.2MHz；只描述一个实际启动 CPU。
- 所有源、内核、镜像和 ADT 哈希写入构建报告，启动命令与设备标识写入本地日志。

## 失败定位

1. 没有 shim 标题：检查真实入口、boot容器、解压、DTB及执行状态。
2. shim 有标题但没有 XNU 入口标记：检查段复制、入口地址、boot_args、缓存/EL状态。
3. XNU 入口标记后停止：按 MMU和 arm_init 最后一条日志定位，检查真实 CPU寄存器与页表；只修这个停点。
4. 内核已进入 BSD但根盘失败：才推进文件系统、块设备、根镜像和对应内核接口。
5. launchd/dyld 或服务失败：按真机进程/内核日志处理私有 ABI 与系统依赖。

Fastboot 接受命令、苹果图案、shim 标题或静态扫描通过均不证明进入 XNU。USB 消失也是预期现象之一，不能据此判定成功或失败；需读取手机屏幕的内核日志。若暂时只能人工读取屏幕，则如实记录观测，不将未知状态写成成功。

## 证据与复现

- reports/ios14-local-inputs.json：本地 IPSW、D20AP、原始内核及根盘成员。
- reports/ios14-xnu-lithium-kernel.json：最新源码构建；reports/ios14-xnu-lithium-instructions.json：静态指令检查。
- scripts/build-ios14-native-boot.py：WSL直接加载镜像构建；out/native-xnu/package.json：具体输入和范围。
- scripts/boot-ios14-native.py：先校验镜像；加 --boot 才临时启动；reports/ios14-native-device.json记录实机结果。
- OPERATIONS.md：现有授权继续有效，无需重复确认 root、必要清除或临时启动。
- reports/history/PLAN-before-native-20261113.md：旧阶段安排归档，不再驱动当前路线。

最新实机结果见 reports/ios14-native-bringup.md 和 ios14-native-history.json：第十二轮挂载完整iOS14.8 HFSX根卷并执行PID1的dyld；因AMFI查询缺失退出。第十三轮接口注册过晚，写入只读策略表时更早panic。现修订0024，改为mac_policy_initmach内注册；26项接口及11项真实注册/只读保护检查通过，下一轮先验证早期注册返回，再观察根挂载与PID1查询。根卷内容未改变，launchd主程序和SpringBoard仍未验证。详情见 reports/ios14-native-amfi-policy.md。timer IRQ与持续调度仍未验证；恢复Android需原厂recovery重建userdata。
