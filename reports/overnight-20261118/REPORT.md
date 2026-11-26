# Lithium 触摸 / Adreno 530 主机开发总报告

> **后续状态更新（2026-11-19）：** R19 已上机但显示分辨率错误，归档、不复测。R20-display 是唯一通过显示验收的基线。R21 Stage 0 已完成主机整包读回，但不含实体触摸后端，尚无可用于真机触摸验收的包。本文和同目录早期计划仅作历史记录；当前状态以仓库根目录的 [PLAN.md](../../PLAN.md)、[OPERATIONS.md](../../OPERATIONS.md) 和 [R21 最终主机核验](../hakios-ios16-touch-r21-host-final.md) 为准。

后续交接：已完成[R19主机冻结、实际整包读回与离线预检](F:/IOS-MIX1/reports/overnight-20261118/R19.md)，并保留原桌面基线。R19没有物理触摸或GPU后端，其真机显示验收仍未验证。下文保留前一阶段调查、实现范围及证据。

日期：2026-11-18。仓库：`F:\IOS-MIX1`，分支 `codex/ios16-lithium`。

**交付状态：完成本机证据调查、协议/策略组件、默认关闭的内核准入诊断、活动 HakiOS 内核构建与主机验证；没有完成可用的物理触摸驱动，也没有完成真实 GPU 阶段 0/1。没有生成声称可验收这些功能的新启动镜像。**

这份报告明确列出剩余工程和原因。构造报文、mock 寄存器、CPU 数组 fence、源码编译成功均不计作手机硬件通过。

## 1. 约束执行与保留基线

- 本轮没有查询、启动、重启或刷写手机；没有运行 ADB、Fastboot、QEMU；没有修改 userdata、darwin 或分区表。
- 没有把 Apple、Qualcomm、Xiaomi 固件或二进制加入提交。测试 ELF、内核和反汇编留在 Git 忽略的本地产物区。公开参考仅作为源码资料，不改变唯一活动 HakiOS 基线。
- 活动来源保持 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`，XNU `8796.121.2`，iOS 16.5 / 20F66 / D22AP / arm64。不使用 arm64e，不推进旧 7195。
- 保持 WSL 16 GiB / 12 CPU / swap 2 GiB；重任务使用 MemoryMax=14G、MemoryHigh=12G、MemorySwapMax=1G、CPUQuota=1200%、JOBS=12。未改 CPU 拓扑、频率、journal、flush 或显示算法。
- `3240` 是 BGR24 面板每行字节数，`3240 / 3 = 1080` 像素；`810` 是误按四字节像素相除。本轮不修显示。

固定桌面基线：

```text
F:\IOS-MIX1\artifacts\hakios\lithium-ios16\baseline-desktop-r15.img
bytes=16265216
sha256=ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55
```

保留 R18 冻结镜像，SHA256 `8d5aa292bb747f3e5022de7a9fa5adee3bb9422d8fa296f4b6a376ca59a973bd`。检查器直接从 R15/R18 Android 镜像读回嵌入 payload，验证内核和 cmdline；两份原镜像及固定 R15 副本均未覆盖。

实际保留的完整 cmdline（不是省略版）：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

`bootcpu=1` 在本机 `{0,1,0x100,0x101}` 编号表选择 affinity `0x1`。主代理独立读已保存的 R16 `reports/local/20261118-042809-642502-ios16-r16/console.log:121`，确认为 `logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 ... match=1`。这是历史日志核验，没有查询当前手机。R15 也已有同样证据；没有把参数请求当成运行结果。

### 回退与既有 dirty 工作

- 备份引用：`archive/ios16-peripherals-before-final-20261118`，指向新收口提交前的 `362dd60`。
- 本地源码备份：`artifacts/hakios/lithium-ios16/overnight-20261118/dirty-source-before-final-checkpoints.zip`，431 个文本文件逐一解包校验，SHA256 `172df1d8a5723d8125308ceef5dbb33d45c3cd3152df1f24d3c13cfc310854a9`。不含固件/编译二进制。
- 准入集成前的 WSL 内核、workspace marker、common.diff、xnu.diff 在同目录 `before/`。
- 只提交本任务明确路径。共享 `scripts/review-hakios-ios16.py` 只暂存新增的 15 行准入校验；`.gitattributes` 只暂存本任务规则。原有 R12–R18 dirty 修改保留，未整包吞入新提交。
- 检出这些提交本身不能重建未提交的旧 R12–R18 工作；恢复时还需要已校验源码备份。先在隔离目录恢复并核验，不对当前 dirty 树执行 reset/clean。

## 2. 要求对照与实际代码

以下行号是当前文件的一基行号；更细证据在模块报告和测试 manifest。

| 要求 / 文件与入口 | 本轮交付、归属和主代理复核 | 状态 |
|---|---|---|
| 公共 2.1：`scripts/prepare-hakios-lithium-overnight.py:12,21`；`ports/hakios-lithium/patches/0019-lithium-peripheral-admission.patch:1` | 主代理：只给上游 input init 加受 lithium 宏保护的 include 和 init 调用；独立核对源文件 before/after 哈希及最终 worker 符号。 | 已编译，未上机 |
| 准入/日志：`ports/hakios-lithium/peripherals/kernel.inc:18,40`；`boot-options.h:43` | 主代理：严格解析、默认关闭、单次普通线程回调、CPU 契约拒绝、无 MMIO；实际生产 include 被主机测试包含。 | 已编译/主机通过 |
| A1 型号与接口：`reports/hakios-ios16-lithium-peripherals-audit.md:17` | Fermat 调查 stock DTB/Android 证据；主代理抽查匹配 DTB、DTS、库存及 Android 源码。 | 源码/存量资料核对完成 |
| A2 帧解析：`maintenance/hakios-lithium/peripherals/touch-frame.h:67,155` | 主代理整理 FocalTech 帧、稳定触点和失流释放；测试单/多点、异常长度、重复 ID、丢失释放。 | 协议逻辑通过；无物理输入 |
| A2 I²C 调用：`touch-i2c.h:66,136,158` | 主代理：身份/分块报告 callback 契约、有限重试/截止时间；mock NACK、timeout、短读。 | 缺真实板级后端 |
| QUP 规划：`qup-i2c.h:84,127` | Kuhn 修订/复核；主代理核对 RESET/RUN/PAUSE、IRQ 与 poll 不同前提，重跑 65,792 tag plans 和 32 前提组合。 | 主机规划器，不执行传输 |
| QUP FIFO：`qup-transfer.h:188` | Noether：带平台门槛的有界单消息 FIFO 候选；主代理抽查 Linux 来源、门槛、cleanup 并重跑所有注错。 | 1–32 字节/STOP；缺 repeated START |
| IRQ/轮询：`touch-irq.h:118,182,203,236,263` | Hume 修复读中到达 IRQ、重复计数、溢出事务性和 poll 所有权；主代理复查并重跑 16 组。 | 串行状态策略；无真实 ISR |
| HID 字段：`touch-digitizer.h:117` | 主代理：构造 hand/finger、身份、时间戳和 IOHID 字段的独立原型。 | 未接 backboardd |
| 队列/坐标：`touch-queue.h:27,64` | 主代理：实际 HakiOS 队列记录适配候选，0/90/180/270 旋转和 0..65535；独立最终复核明确整批交付等缺口。 | 未连接物理生产者/消费者 |
| 单核 SPI：`irq-route.h:1`；`touch-irq-gic-checkpoint.md` | Poincare 源码调查；主代理复核 GIC 和 988 个标准 SPI 偏移/失败场景。 | 未改活动 GIC，硬件未验证 |
| B1 来源链路：`gpu-upstream-components.md:29` | Avicenna 初查；Epicurus 纠正源码链路，主代理抽查确切 Git 对象；Hypatia 再核 Turnip gen5 排除。 | 源码审计完成 |
| B1 A5xx 前提：主调查 GPU 表、`gpu-reference-verification.md:66` | Franklin 初查 Linux/Mesa/A5xx；Epicurus 复核下载及世代边界，主代理检查资料与本机 DTS。 | 设备运行前提未验证 |
| B2 阶段 0：`gpu-probe.h:61` | 主代理：资源申请/失败清理/截止时间的回调编排模型。没有供电寄存器后端。 | 主机模型；非真实阶段 0 |
| B2 阶段 1：`gpu-submit.h:77,129,166` | 主代理：CPU 数组 ring、五字 fence 包、IRQ/poll/超时/quiesce 生命周期模型。 | 主机模型；非 GPU 提交 |
| 固件/页表：`gpu-firmware.h:37,75`；`gpu-iova.h:148,261` | 主代理：构造输入的格式/边界、map/TLB/保留页生命周期测试。没有加载固件或写 SMMU。 | 主机通过；硬件未验证 |
| A3/B4 与证据：`scripts/test-hakios-lithium-peripherals.py:21`；`scripts/check-hakios-lithium-peripheral-build.py:52` | 主代理独立编译/执行全组，校验当前源码哈希、内核 provenance、链接字符串与指令过滤；Hypatia 核查测试范围。 | 14/14 主机套件通过 |

`touch-digitizer.h` 是独立的浮点 HID 原型；拟用内核生产者的坐标策略集中在 `touch-queue.h:27` 的整数映射函数。**它们不是两条已接入驱动。正式接入前仍须统一两种原型的几何来源**，不让用户态/内核各维护一份可变校准。整批队列提交/背压、帧时间戳、消费者确认和失流恢复也尚未完成。

## 3. A1：本机触摸调查结论

原始证据详见主调查，不以兄弟机型节点替代 lithium：

- 匹配 DTB03：SHA256 `93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985`；`artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4863` 的 `ft,ft5x46_i2c`，FTE8716 家族。存量 Android inventory 记录 `75ba000.i2c/i2c-12/12-0038`。当前运行芯片 ID/固件版本未重新测量。
- I²C12 / BLSP2 QUP6，地址 `0x38`，base `0x075ba000`，源时钟 19.2 MHz，stock 400 kHz；pinctrl87/88。这些是 DT 配置，不是 XNU 已正确上电的证明。
- named IRQ GPIO125；通用 `interrupts` 仍有 GPIO105 冲突。下游驱动选择 named GPIO、下降沿，reset89；低 10 ms、释放后 300 ms 来自下游流程，不在本轮对手机执行。
- L14 / 1.8 V，LAB/IBB 与显示共享。所有权、handoff 当前值未验证，不可为触摸探测盲目重置共享显示电源。
- 报告 63 字节：3 字节头 + 10×6，12 位 XY、4 位 ID，15 为结束，事件0/2 active、1 up。stock 分 `0x00/32` 与 `0x20/31` 读取；跨两读的帧锁存需真实原始帧验证。
- HakiOS 已有 `/dev/hakios-touch` → userland injector → IOHID hand/finger 的路径，不需要从头造替代服务。此次没有接入物理来源。

### A2 仍缺的具体工程

1. 板级 TLMM pinmux、QUP 时钟/reset、电源与独占 handoff 的可审计 backend。现有 redwood Goodix SPI 不能代替本机 I²C。
2. 把 QUP 合并消息 tag 规划落实到真实 FIFO 执行：目前 FIFO 候选缺 repeated START，**不能**用两次带 STOP 的消息冒充触摸寄存器读。
3. 身份/空闲帧/按下/移动/抬起采集、GPIO/TLMM 子中断、丢边沿/轮询恢复。
4. 整帧进入 HakiOS 队列/IOHID 的交付与 backpressure，确认 UI 真正响应手指。单测生成了字段不等于事件已送达。

## 4. B1：GPU 路线和硬性边界

### 已核对的链路

UniXNU AdrenoMetal / NativeMetal 的 Metal 对象和 command buffer 在用户态经 Rust bridge/metal2vulkan 转 Vulkan；Darwin Turnip KMD 再把 BO/import/map/submit/wait 转为 NativeAdreno ABI。NativeAdreno 负责内存对象、页导入、GPU VA、命令提交和 fence；IOSurface 需要对应的页/偏移/stride/cache/lifetime 契约。**这个运输接口不是 A530 命令生成器。**

公开链路并非都强制使用 Android KGSL：已审计源码存在 DRM 路径，也存在 Darwin NativeAdreno/RedwoodGPU adapter。替换 KMD 传输层本身不能补齐某一代 GPU 的 Vulkan 实现。证据和 pin 见 `gpu-upstream-components.md`。

决定性差异：本轮检查的 Mesa 25.0.7 `742a20f48c59e8649533c84c4d49dd95b403f5da`：

- `src/freedreno/common/freedreno_devices.py:287-303` 将530归 A5XX。
- `src/freedreno/vulkan/tu_device.cc:1405-1407,1452-1455` 接受 gen6/7，其他世代返回不支持；这条 Turnip 路径不支持 A530。
- `src/gallium/drivers/freedreno/freedreno_screen.c:1046-1048` 的 gen5 → fd5 是独立 Gallium 路线，不能当成 A5xx Vulkan 已有。

因此不能靠给 A530 改 chip ID、替换少量寄存器或打开 `metal=1` 接通 A740 路线。尚未验证其它有 A530 Vulkan 支持的 fork；本报告不作“所有未来实现均不可能”的断言。

A740 的 SQE/GMU/HFI、a7xx CP 与页表/电源序列不能直接替代 A530 的 PM4/PFP/GPMU、A5xx 寄存器/缓存/VBIF。Dada gpu2 又是 A830 / arm64e / iOS26；其打包脚本不能直接用于 D22AP arm64。UniXNU devices.toml 中出现的四个8996组件名仅有有限 probe/stub 文档，审计树中没有可复用的完整实现体；不是找到一个同名驱动就能接线。

### 本机前提表

| 前提 | 已有资料 | 运行验收 |
|---|---|---|
| 身份/MMIO | stock Adreno530v5、chip `0x05030004`、`0x00b00000+0x3f000`、SPI300；DTS:11302 | 未从当前 HakiOS 安全读 ID |
| 电源/时钟 | gpucc8996pro、CX/GX GDSC、GFX CPR/RPM corner、bus vote；DTS:2737,5339,5989,10413 | handoff、使能顺序和可用频点未验证 |
| SMMU | GPU SMMUv2 `0x00b40000`，普通SID0/secureSID2；DTS:11557,11924 | context 所有权、宽度、granule、cache/TLB 完成未验证 |
| PM4/PFP/GPMU | 本机 stock 名称 `a530_pm4.fw`、`a530_pfp.fw`、`a530v3_gpmu.fw2`、`a530v3_seq.fw2` | 本轮未提取/加载；格式单测使用构造数据 |
| Zap/SCM | stock `a530_zap`、PAS13、no-map保留池 `[0x8fe00000,0x92900000)` | Mu/XNU SCM可用性、TZ接受和已有安全状态均未验证 |
| DMA/复位 | Linux有相应A5xx/cache/回收实现供参考 | 实际GPU写回、IRQ、reset、DMA quiescence未验证 |

只在正常安全服务与所有权得到证明后考虑在 Mu 阶段或 XNU 阶段使用正常 PAS 流程。本机 CP 非安全模式没有证据，不把它当成现成替代路径。访问未上电块的一条 MMIO 指令本身可能挂总线；外围循环超时无法挽救它。因此没有在缺乏前提时强行加入供电寄存器写入。

现有 `gl=0 metal=0` 保留软件 CA/IOSurface/framebuffer 路径。`gl=1` 可能仍走软件 OSMesa，不能据开关判断加速。`metal=1`/M2V 还依赖正确构建和真实后端。本轮不改这两项。

### 阶段 2–4 设计（未实现图形 smoke 脚本）

先确定可行的 A5xx 用户态实现，而不是先写会假通过的三角形脚本。拟用接口必须覆盖 BO/import IOSurface、map/unmap、cache transition、submit、fence/wait、timeout/cancel 与 DMA-safe lifetime；将 ABI/version/arm64/世代显式校验。阶段2先离屏纯色/三角形并核对 IOSurface 内容与实际呈现；阶段3才替换合成；阶段4记录同场景软件/硬件耗时、恢复和长期稳定性。现阶段只有接口设计和主机模型，**没有**可运行的阶段2–4 GPU 渲染/性能脚本。

## 5. GIC 与单核

固定上游 `upstream/hakios-xnu-kernel/xnu/files/osfmk/arm64/gic_v3_irq.c:225-265` 以当前 MPIDR 写该 SPI 的 IROUTER 后立即 unmask；`:484-485` 请求 ARE_NS，但没有证明有效ARE/route回读。部分 settle/detach 循环没有上限。

| 设备线 | GIC目标（资料与算术） |
|---|---|
| GPIO125触摸 | TLMM父SPI208 → INTID240 → IROUTER `0x6780`；GPIO125不是GIC INTID |
| QUP控制器 | SPI106 → INTID138 |
| GPU | SPI300 → INTID332 → IROUTER `0x6a60` |

目标 affinity=1、IRM=0。候选模型要求 owned line、TYPER范围、mask/settle、有效ARE、IROUTER回读后才unmask。**没有把模型接进全局 GIC，也没有确认真实 SPI 已投递到 phys0x1。** 后续只改自己拥有的线，不批量覆盖固件其它中断。

## 6. 构建、测试和失败记录

实际执行的主路径：

```powershell
# WSL的主机准备脚本在既定systemd资源限额内执行
# /usr/bin/python3 /mnt/f/IOS-MIX1/scripts/prepare-hakios-lithium-overnight.py
# /usr/bin/python3 /mnt/f/IOS-MIX1/scripts/review-hakios-ios16.py
.\scripts\run-wsl-limited.ps1 -Task hakios-deps
.\scripts\run-wsl-limited.ps1 -Task hakios-kernel
.\scripts\run-wsl-limited.ps1 -Task hakios-r18-regressions
```

主机外设全组，一条命令即可重跑（不需要手机；默认临时保存，不覆盖最终证据）：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py
```

最终留证时实际另用了既定 systemd 限额和 `--build-dir /mnt/f/IOS-MIX1/reports/overnight-20261118/accepted-host-tests`；该目录必须新建，ELF 不提交。重跑请用另一个新目录。精确 clang 18.1.3、逐套件 stdout/stderr/退出码/耗时/二进制哈希和29个源文件哈希见 `accepted-host-tests/passed.json`。

| 检查 | 结果与边界 |
|---|---|
| 外设14组 | 14/14，ASan+UBSan，stderr空；全组构建/执行服务59.301秒。都是主机逻辑/mock；admission测试包含实际kernel.inc但替代OS接口。 |
| 旧生产回归链 | `hakios-r18-regressions` 退出0，服务2分3秒；递归保留旧USB/console/observer/IPC/HFS/CPU/display测试。21份通过报告的哈希已快照。不是手机回归。 |
| 源码审查 | 84项源码/加载边界通过，实际input init差分和parser/body哈希一致。 |
| 完整活动内核 | 正常 HakiOS dependency + kernel 流程得到新链接产物；worker符号和两类blocked字符串在实际二进制中。 |
| 新内核指令过滤 | LSE/PAC/PAN/UAO目标过滤0命中；159处ARMv8安全hint；20个过滤器正反例。只排除逐字节对照源汇编的256字节K256数据表。这不是覆盖所有CPU扩展的完备证明。 |
| 包/真机 | 没有重建413项用户态、Mu和新的Android启动包；没有宣称旧R18整包检查覆盖新内核包。没有上机。 |

构建特殊情况如实保留：上游原始 make 的 kext export/symbol-set 检查仍有 Error 1/2，日志 `:652-804`；既有 `tools/make-kernel.sh` 只按其已有规则接纳这些指定目标失败并报告已生成内核。本轮未放宽该规则，未见新的编译器/链接 `error:`。不能写成“原始make零错误/零警告”。

最终新内核（本地构建，**不是可直接刷写镜像**）：

```text
artifacts/hakios/lithium-ios16/overnight-20261118/host-admission-build-v3/hakios-kernel
bytes=14918024
sha256=8e9209c1ff077ba6ad16cbd467c6cceb3cb6b16c0357667e513b913190fd9b4b
```

检查器命令使用 `scripts/check-hakios-lithium-peripheral-build.py --output <新的ignored目录>`；它不会打包或查询手机。证据见 `build-evidence.json`，有明确 `boot_ready=false`、`touch_hardware_backend=false`、`gpu_hardware_backend=false`。当前活动树包含任务开始前已有的显示R16–R18源码；本轮未改它们，也不能把此内核视作仅相对R15改变触摸的已验证包。

失败与修正：

1. 早期admission主机fixture的同名mock变量遮蔽已修正，随后3组定向与最终14组通过；没有据失败结果继续上机。
2. 首轮反汇编向 `/mnt/f` 逐行写入，180秒超时，保留部分输出；改WSL本地临时文件后一次复制。
3. 第二轮过滤误把 ADRP 的符号注释 `whitelist_selftest.cases` 当 CAS 指令。改为解析指令助记符，PAN/UAO仅检查实际system-register操作，并加入8接受/12拒绝控制例；第三轮18.135秒通过。未修改内核来回避过滤器。
4. 三个下载结果实际为HTML挑战页，已排除；用确切Git对象复核。旧“Turnip .cc不存在”说法撤回，替换为可复现的gen5排除证据。
5. 冻结R18合同引用的一些可变主机报告因本轮重建/回归发生哈希漂移。列表写入build-evidence；不改写冻结合同、不宣称旧报告provenance仍相同。原镜像/payload验证单独通过。

## 7. 并行执行记录

采用同一工作区的独立文件补丁集，不共享写文件；不声称使用了不存在的独立worktree。调查与模型修订分批并行，后续验收统一在主工作区。主代理负责共享parser/日志接点/准备脚本、抽查、构建和证据。未记录完整agent起止时间的项目标记未记录，不用测试运行时冒充研发耗时。

| 子代理 / 分工 | 交付、耗时证据 | 主代理复核、退回/冲突 |
|---|---|---|
| Fermat / A1 | DTB匹配、FocalTech/Android资料；独立用时未记录 | 抽查DTB03、DTS/库存/下游驱动；硬件handoff保留未验证 |
| Avicenna / B1链路 | Metal/IOKit/Turnip来源；独立用时未记录 | 复查真实GPU/userland入口，后由Epicurus修正源码范围表述 |
| Franklin / A5xx | Linux/Mesa/固件/SMMU/ZAP差异；独立用时未记录 | 对照DTS和Linux；未把参考序列当作本机运行证据 |
| Poincare / GIC | 单核编号和SPI路由源码；独立用时未记录 | 抽查MPIDR和IROUTER，真实投递仍未验证 |
| Turing/Chandrasekhar/Euclid/Pasteur / 有界候选轮 | 取得参考资料，但时限内没有可接纳的代码/报告；独立用时未落盘 | 已停止/重新分配，没有等待它们阻塞收口，也未把未交付内容记为成果 |
| Hume / IRQ策略 | `touch-irq-review.md`、仅改IRQ .h/test；用时未独立记录 | 实读/重跑16组；修复读中IRQ跟进、溢出与poll所有权后接纳；无共享文件冲突 |
| Kuhn / QUP规划 | `qup-review.md`、仅改规划.h/test；用时未独立记录 | 核对poll前提、状态/长度计划；全组独立重跑接纳；无冲突 |
| Epicurus / B1二次审计 | 两份GPU报告；用时未独立记录 | 撤回错误“缺.cc”前提、核对pin/世代；没有把不存在的8996backend记为可复用 |
| Noether / QUP FIFO | `qup-transfer.md`、独立.h/test；用时未独立记录 | 复核门槛/cleanup与42/37处MMIO注错、64种长度方向；仅以明确subset接纳 |
| Hypatia / 最终独立复核 | `final-independent-review.md`；29/29当前源哈希匹配，未另跑测试；用时未独立记录 | 主代理接纳A2/B2未完成和repeated START缺口；另读R16console:121补实其提出的运行证据疑问 |

本次未发生需要回退生产硬件改动的合并冲突。模型补丁分组接纳后作统一完整内核/全组测试与既有回归，不把提交/文档变化当作新硬件改动反复构建。未执行逐个单文件补丁的整核重建；此处记录实际验收粒度，不把一次最终构建说成多次逐项构建。本轮分配的agents均已结束；独立复核的结论没有照单全收，R16争议已用原日志复核。

## 8. 早上上机清单：条件满足后才执行

**当前没有新的可刷写触摸/GPU包。不要现在用新开关期待手指可用或GPU读ID；准入代码只会报告阻塞。无需为重复确认这些已知缺口安排手机轮次。** 基线/R18保留，明早先看本报告，优先完成合并I²C消息和板级handoff。任何新包必须再走正常完整集成/检查/冻结入口；没有绕过现行revision/包白名单。

下列矩阵是后续具备backend和已冻结包后的单变量计划；所有行都保留第1节完整基线cmdline，不删计时/UFS/USB/显示参数。没有在本轮执行任何一行。

| 顺序 | 精确附加参数 / 唯一变量 | 前提、应保留的日志与含义 |
|---|---|---|
| 0 可选基线 | 不加新参数 | 如需要验收新包默认关闭行为，先核 `LITHIUM RUN A ... affinity=0x1 ... match=1` 和桌面；既有运行正常无需为本轮报告重启。 |
| 1 触摸申请 | 基线 + `touch=1` | 当前准入预期只输出下方blocked行。只有真实QUP/板级backend完成并冻结后，才可测身份与帧采集。 |
| 2 原始帧 | 第1行 + `touch_log=1` | 仅增加日志变量；采idle/down/move/up及边界，不用刷屏当作成功。当前不会有raw报告。 |
| 3 轮询对照 | 第1行 + `touch_poll=1` | 与第1行同样的其他设置，只改强制poll；需真实采集adapter。若poll有帧而IRQ无，才转查GPIO125/TLMM/ARE/route；不能凭缺日志直接定故障。 |
| 4 GPU阶段0 | 独立基线 + `gpu_probe=1` | `gpu_stage`默认0；当前只blocked。真实stage0前必须证明电源/clock/SMMU/secure所有权和有界安全访问；保留ID/timeout/cleanup。 |
| 5 GPU阶段1 | 第4行 + `gpu_stage=1` | 只在真实stage0通过、正常固件/PAS/映射和恢复契约完成后；核fence/IRQ或poll、超时与quiescence。不能把当前mock包提交。 |
| 6及以后 | 暂不提供可执行图形参数 | 先解决A5xx用户态渲染路线，再独立安排IOSurface纯色/三角形、合成和性能；不能直接开metal或改chip ID试运气。 |

当前实际已编译的申请日志（不是未来硬件成功日志）：

```text
LITHIUM TOUCH status=blocked reason=platform_backend_unverified bus=12 address=0x38 poll=0 raw_log=0 mmio=0 irq_attached=0 hid_events=0
LITHIUM GPU status=blocked reason=power_smmu_scm_backend_unverified requested_stage=0 watchdog_requested=0 mmio=0 software_fallback=1
```

CPU契约错误则为 `status=blocked reason=cpu_contract`；参数不合法或分配失败则 `status=disabled`。`gpu_wd=1` 目前仅记录申请，不代表硬件watchdog存在；`software_fallback=1` 表示未尝试改动软件路径，不是一次真实GPU故障后回退的验收。`gpu_stage=0` 单独出现仍是关闭，不能把它当成probe。

## 9. 未验证汇总、所需输入及风险排序

优先顺序建议：先触摸polling最小闭环 → owned IRQ → UI手指交付；GPU继续独立源码/硬件前提调查，不阻塞软件桌面。当前没有依据承诺某个日期能完成A530加速。

| 排序 | 剩余工作 / 风险与量级 |
|---|---|
| P0 | 保留可进桌面单核baseline；不动未知保留内存、共享电源和安全状态。错误寄存器访问可能整机abort/挂总线。 |
| P1 触摸 | QUP repeated START/FIFO整合、TLMM/clock/rail handoff、真实报告一致性和队列交付。多模块集成，不能只改boot-arg。 |
| P1 GPU电源/安全 | GDSC/RPM/CPR、SMMU所有权、SCM/PAS/TZ许可、匹配固件及DMA-safe复位。可能遇到硬性平台障碍，未验证。 |
| P2 GPU执行 | A5xx CP/PM4/PFP/GPMU、页表/cache、ring/fence/ISR/watchdog。当前仅协议模型，工作量大。 |
| P3 图形栈 | 检查的Turnip不支持A5xx；评估A5xx Gallium/GL或另行实现所需用户态，而非直接移植A7xx/A8xx。最大长期工程风险。 |
| P3 验收 | 真手指、旋转/边界、跨帧丢失、DMA故障恢复、真实渲染与稳定性/性能；全部待真机。 |

后续需要的资料（本轮不从手机提取）：

1. 现有Android备份中的触摸runtime chip/firmware/vendor、IRQ125/105选择、GPIO87/88具体SDA/SCL、pinmux/clock/regulator交接信息；本仓库已能提供DTS和部分库存，不重复索取已有文件。需要真实idle/down/move/up的63字节帧及两块读一致性证据。
2. GPU当前handoff电源/时钟/secure context状态、普通/安全SMMU配置和保留区所有权；只读安全性亦需前置审查，不能先盲读再证明。
3. 如开发确需固件，从匹配本机stock备份取得 `a530_pm4.fw`、`a530_pfp.fw`、`a530v3_gpmu.fw2`、`a530v3_seq.fw2` 和实际zap组合（如 `a530_zap.mdt`/`.b00`–`.b02` 或对应ELF）。本地放 `artifacts/local-firmware/lithium-a530/`，先检查Git忽略；记录版本/来源/SHA256，不加入Git/patch/分享包。不按兄弟机名假定必须是单个mbn。
4. 真实SPI投递/路由回读、板级有界MMIO、DMA/cache/TLB以及reset/quiesce证据。单核运行不替代缓存一致性。

## 10. 提交和交付入口

- `8cdfa22`：默认关闭boot-option主机约定。
- `da5e40b`：冻结镜像/payload与可变报告provenance分开审计。
- `3cf7bdf`：本机触摸、GPU、GIC来源调查。
- `362dd60`：初版IRQ主机策略（后续修正有独立提交）。
- `dbc3957`：来源核对、触摸/QUP/GPU/IRQ组件和修正，仅源码与报告。
- `4093168`：默认关闭准入、内核构建检查、源绑定14组测试manifest和独立复核。
- 本报告及最终manifest另作收口提交，最终commit id由终端输出；不在文件里写自己的循环hash。

主要入口：本报告、`build-evidence.json`、`accepted-host-tests/passed.json`、`final-independent-review.md`、主调查报告和各模块review。所有产物都明确区分源码/主机通过与真机未验证。
