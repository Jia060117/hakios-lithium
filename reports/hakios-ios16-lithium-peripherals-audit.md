# Lithium 触摸、GPU 与 GIC 源码调查

目标为小米 MIX 1、MSM8996 Pro、完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`、XNU `8796.121.2`、D22AP arm64 与 iOS 16.5 `20F66`。工作范围为主机。本文中 WSL 源码行号来自 `/home/jia/ios-mix1-build/hakios-lithium-ios16`，与固定上游中的行号可能相差本机移植补丁。

## 公共接口

| 路径 | 当前来源与接入判断 |
|---|---|
| 启动参数 | 加载器 `xnuloader/loader.c:3215` 和 `:3240` 取 `bootcpu`。XNU `pexpert/gen/bootargs.c:191` 通过 `PE_boot_args()` 转给 `PE_parse_boot_argn_internal`。`MiniIOMobileFramebuffer.cpp:111` 和 `:112` 使用 `TUNABLE` 读取分辨率。`loader_return.c:606` 解析返回时间。新增主机解析器与参数约定见 `maintenance/hakios-lithium/peripherals/boot-options.h:56`。 |
| 控制台 | 工作线程可用既有 `printf` 路径。XNU `osfmk/console/serial_console.c:283` 将批量正文送入 `lithium_log_bytes`，`osfmk/arm64/loader_return.c:115` 写保留环，`ports/hakios-lithium/drivers/usb/hakios.lithium_usb.c:333` 消费同一环。新的记录使用 `LITHIUM TOUCH` 或 `LITHIUM GPU`，每阶段及每类错误限额。硬中断只记数据并调度工作线程。 |
| GPIO、I²C 与 SPI | DT 有 TLMM、I²C12 与时钟/引脚映射。活动版缺 lithium 专用的触摸 TLMM 解复用和 BLSP QUP I²C 后端。上游 `drivers/input/hakios.goodix.c:907` 的 SPI 控制器及板级上电针对 redwood，不能用作本机 I²C 实现。 |
| PMIC 与时钟 | Mu 有本板显示上电路径。`upstream/research/uefi-g0245d` 的 SPMI/PMIC 库仅为参考。L14、LAB、IBB 在 XNU 接管时的状态及所有者未验证。保持显示共享电源，先确认电源已具备再访问控制器。GPU GDSC/CPR/RPM 需要独立确认。 |
| SMMU 与中断 | stock 有 GPU SMMUv2 和普通/安全 context。活动版没有 lithium GPU SMMU 后端。HakiOS 的 `gic_v3_spi_attach` 可作为按中断线注册的起点，当前未检查 `ARE_NS` 是否生效和 `IROUTER` 回读。见下文。 |

`touch=1` 才允许开启触摸，`touch_log=1`、`touch_poll=1` 是其附属选项。`gpu_stage=0` 保持默认关闭，`gpu_stage=0 gpu_probe=1` 表示只读 ID 的阶段 0 申请。`gpu_stage=1` 申请完成阶段 0 后的受控内核提交，`gpu_wd=1` 申请看门狗观测和诊断。所有活跃实现须始终有硬性超时和安全退场，不能由 `gpu_wd=0` 解除。阶段 2–4 为接口设计；最终由设备层依据证据判断可用阶段。这些开关当前从主机解析器开始。

## A1 触摸

Fermat 复核了 stock boot 与 recovery 中的 20 份附加 DTB。设备已保存根属性只匹配 DTB `03`，SHA256 `93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985`，`msm-id=0x131/0x10000`，`board-id=0x26/0`。主代理比对了 `reports/stock-firmware.md:42`、实际反编译 DTS 与 `reports/local/20261112-142718-199681/inventory.json:192`，库存记录也显示 `ft5x46` 在 `75ba000.i2c/i2c-12/12-0038`。其他板号的 Synaptics/Atmel 节点不属于匹配 DTB。Dipper 的 `st,fts` 是 STMicroelectronics 的协议，不能按名字推断为同一家。参考 Xiaomi `dipper-o-oss` 提交 `ab0b51d6d7573a0b1bcabca0e42939284ebd47b7` 的 `dipper-p0.dtsi:501`。

| 项目 | 匹配板级证据 |
|---|---|
| 控制器 | FocalTech `ft,ft5x46_i2c`。四个配置均为 FTE8716 家族，列 `chip=0xe7`、24×13，分别为 AUO/Sharp 和 Biel/Lens。运行时芯片/版本寄存器要实读，`e7/a6` 固件升级 ID 不宜用来判断运行时注册值。`lithium-stock.dts:4863`、`:4893`、`:4898`。 |
| 总线 | BLSP2 QUP6 / I²C12，映射 `0x075ba000+0x1000`，地址 `0x38`，19.2 MHz 输入，stock 400 kHz。GPIO87/88 使用 `blsp_i2c12`，DT 未给各自 SDA/SCL 名字。本机 QUP v2 总线后端未验证。`lithium-stock.dts:4844`、`:14221`、`:18267`。 |
| IRQ 与重置 | DT 通用 `interrupts` 是 GPIO105，`irq-gpio` 和 pinctrl 是 GPIO125。Fermat 在 stock `boot/Image+0x8734fc` 找到 `gpio_to_irq` 后以 `0x2002` 注册的证据，公共 Android 源码确认驱动选择命名 GPIO，下降沿。reset GPIO89，低保持 10 ms，释放后等 300 ms，正常 resume 跳过 reset。`lithium-stock.dts:4866`、`:4881`、`:14029`。 |
| 供电 | VDD/VDDIO 都引用 PM8994 L14 1.8 V。LAB 和 IBB 与显示共享。Android 参考顺序 VDD→VDDIO→LAB→IBB。实机上电前提和交接后状态未验证。`lithium-stock.dts:4868`、`:6323`、`:8300`、`:2010`。 |
| 数据帧 | 下游驱动是 3 字节头加十组 6 字节。12 位 X/Y、4 位 ID，`0xf` 结束，事件 0/2 为 active，1 为 up。Fermat 在 stock `boot/Image+0x8704d0` 和 `+0x8753b0` 复核了 63 字节请求及 SMBus 分块读取。首块 `0x00`/32 字节，次块 `0x20`/31 字节。改用一次 I²C 连续读的兼容性未验证。X/Y 名义最大 1080/2040，实际边缘及换帧一致性未验证。 |

下游参考 `LineageOS/android_kernel_xiaomi_msm8996@173c5987c779ecfc6688b38bd3427bea0400db12`。`drivers/input/touchscreen/ft5x46/ft5x46_ts.c:59` 是布局，`:1552` 是解析，`:2753` 是上电，`:3585` 是 reset，`:3722` 和 `:3910` 是 IRQ 选择。`ft5x46_ts_i2c.c:102` 实现分块读。运行条件先用已保存的板级身份、资源所有权和一次只读 identity 申请验证。采到 idle/down/move/up 的原始帧和 IRQ 计数后，再接连续事件。

固定 HakiOS 有 `/dev/hakios-touch` 及虚拟 digitizer。`drivers/input/hakios.input.c:163` 写单事件的有限队列，`userland/src/inject.c:759`、`:814` 用 IOHID 事件生成手与指集合。`drivers/input/hakios.virtio_input.c:215` 是 Virtio 参考。原生 IOHIDEventService 的参考在 `ios-sailfish@6f27e41ff249f2680540ebe8ee5ab982c5e641c9` 的 `xnu/patches/0058-sweet-hid-event-services.patch:20947`、`:21032`，其 IOHIDFamily ABI 在 20F66 上未验证。首轮可以复用上游现有注入入口，明确帧边界、队列溢出及物理来源，再单独比较原生 service。一次有效帧一个单调时间戳，保留稳定的触点身份和静止手指，显式提交抬起。以面板坐标作为输入，在一处映射到 750×1334 渲染矩形，再归一化给 digitizer。UIKit 逻辑 points 和 BGR24 扫描行字节数各有自己的单位。`3240 / 3 = 1080`。

## B1 GPU

Avicenna 以固定提交比对用户态和内核 ABI。主代理复核了 `upstream/hakios-xnu-kernel/userland/src/inject.c:1038`、`userland/src/HakiosMetalCA.m:2142`、`drivers/gpu/hakios.gpu.cpp:80` 和本机 `ports/hakios-lithium/configs/lithium.config:14`。Metal 前端与 Vulkan ICD 之间可共用接口思路，驱动 ABI 和 GPU 世代须分别对齐。

| 来源 | 已确认链路 |
|---|---|
| UniXNU | 本地 `c34f018aafd6befd5dc93c05a8dccd5bdce6724b`，AdrenoMetal `51b7003f9c05ec46f0aa4ed7e9bb47f8db893c04`，Adreno740 `031f35ccfbb3da1007f44ff42e287d78fb4973f8`。`drivers/AdrenoMetal/NativeBridgeMetalFrontend.mm:4495` 保留 IOSurface 及其页/偏移/stride。Rust `metal2vulkan` 将 Metal 命令转为 Vulkan 并提交。Turnip 的 Darwin KMD 将 BO 导入、映射、提交和等待转换成 NativeAdreno ABI v2。见 `patches/mesa/turnip-darwin-kmd.patch:1166` 和 `drivers/Adreno740/NativeAdrenoABI.h:14`。提交返回序列号，A740 ring 写回 fence，Vulkan 同步对象关联该序列。Windows Git 读到公开 HEAD `563595abedfdfa413ad583b66b6176d82007e633`，仅作参考。 |
| `hakios-arm64e-dada` | `f1ae906fcc43ba8f763663257724ba938fb820ab` 的 `gpu2/build_plugin.sh:14` 打包 NativeMetal、Rust bridge 和 Turnip。`gpu2/patches/mesa-85fda3b-dada.patch:2672`、`:2847` 经 RedwoodGPU `0x7e` 导入 IOSurface，`0x46` 提交，`0x47` 等 fence。单独的 `gpu2` 分支及 `BASES.txt` 三个工作树短哈希未解析。该包针对 arm64e，不能直接装进 arm64。 |
| `dipper-xnu` | `2854c1e22fc2d73bb7a890c2c84b80182ea5ad18` 的 `tools/build/build-userland.sh:25` 开 `GL=1`，`patches/xnu-mi8.patch:3519` 对 A630 的 RedwoodGPU 提交/完成有适配。其 HackiOS 内核固定 `8e61187098f2c26c5daee8f9f72b6416aa64b9ab`、用户态 `906425b75e02b4f99eb5d7bb1ef7c9583472d772`。`hackios-xnu-userland` 公开 HEAD `2573c4a8d80ed143e534d16b5a386efa5b06ccbb` 为迁移通知。 |
| 固定 HakiOS | `gl=0 metal=0` 从 CoreAnimation 软件 renderer，经 IOSurface 到 `MiniIOMobileFramebuffer`。有 GL 库时 `gl=1` 可走 EAGL/Mesa，允许软件 OSMesa，因此并非 GPU 已加速的标志。`metal=1` 发布 `IOAcceleratorES` 的 HakiosMetal，`METAL_PLUGIN=ca` 可用原生 PM4 后端。只有构建 `TURNIP=1 M2V=1` 并给 `metal_m2v=1` 才尝试桥接 Vulkan。固定版经 `hakios-drm` 和 HakiosGPU 的 MSM 风格 IOKit 接口提交和等待。参见 `tools/mk-turnip.sh:25`、`drivers/gpu/hakios.gpu.cpp:1093` 和 `MiniIOMobileFramebuffer.cpp:1437`。 |

Franklin 对比 Linux v6.12 `adc218676eef25575469234709c2d87185ca223a` 和 Mesa 25.0.7。主代理比对了保存的 Android sysfs、匹配 DTS 与相关 HakiOS 资源声明。`inventory.json:182` 为 `Adreno530v5`，DTS `:11302` 给 `0x05030004`、MMIO `0x00b00000+0x3f000`、SPI 300。Mesa `docs/drivers/freedreno.rst:11`、`src/gallium/drivers/freedreno/freedreno_screen.c:1043` 结合 A5xx 设备表建立 Freedreno Gallium 路线。Turnip 的支持从 A6xx 起。A740/SQE/GMU/HFI、GPU 页表上下文、寄存器和电源序列不能沿用到 A530 的 PM4/PFP/GPMU，A5xx 自己的 CP、缓存和 VBIF 序列必须按本代实现。Linux `drivers/gpu/drm/msm/adreno/a5xx_catalog.c:111` 与 `a5xx_gpu.c:640`，A740 对比 `a6xx_catalog.c:1328`。

| 前提 | 现有证据与未验证项 |
|---|---|
| 上电、时钟、总线 | stock `gpucc-8996-pro` `0x008c0000`、CX GDSC `0x008c4034`、GX GDSC `0x008c4024`，GFX CPR corner、PM8994 S2 RPM corner、bus vote。`lithium-stock.dts:2737`、`:5339`、`:5989`、`:10413`。HakiOS 后端及可用 operating point 未验证。 |
| GPU SMMU | SMMUv2 `0x00b40000`，普通 SID 0、secure SID 2。`lithium-stock.dts:11557`、`:11924`。Linux `drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c:184` 标注 MSM8996 GPU context 的处理。非安全 context 所有权、地址宽度、granule、TLB 完成在本机均未验证。XNU 管理的高地址 RAM 让物理地址宽度成为实际前提。 |
| 固件 | stock `system.img` 的 `etc/firmware/` 有 `a530_pm4.fw`、`a530_pfp.fw`、`a530v3_gpmu.fw2` 和 `a530v3_seq.fw2`。stock `boot/Image+0x103c394` 指向该组合，另有早代文件。GPMU 是 FW2 格式，与 PM4/PFP 四字节头及加载机制不同。Linux `a5xx_catalog.c:111`、`a5xx_power.c:219`、`adreno_gpu.c:528`。 |
| Zap | stock 为 `a530_zap`、PAS 13，引用 `no-map` 保留池 `[0x8fe00000,0x92900000)`，见 DTS `:11280`、`:18292`。需要匹配的 `a530_zap.mdt`、`a530_zap.b00`–`.b02` 或 stock 提供的对应 ELF。预留池内的可用范围、SCM 是否可用及 Xiaomi TZ 是否接受，从 HakiOS/Mu 路径均未验证。正常 PAS 可分别评估从 Mu 或 XNU 调用，均需要明确内存与服务契约。CP 非安全运行没有本机证据。无正常认证或明确已有有效状态时，停止阶段 1。 |
| 缓存与复位 | CPU 写入、SMMU 表、GPU 缓存完成是三项独立工作。内存可见后再敲门铃，GPU 写回要同步后再由 CPU 检查，超时进入 quiesce 并保留尚有 DMA 引用的页。Linux `a5xx_gpu.c:33`、`:213`、`:1013`、`:1351` 与 `io-pgtable-arm.c:268`。单核不能替代任一屏障。电源关闭状态访问 MMIO 的总线响应未验证，循环超时无法救回一条卡死的总线读指令。 |

固件只从本地已提供的匹配 stock 镜像取出，若构建确实需要，放入已有 Git 忽略区 `artifacts/local-firmware/lithium-a530/` 之前先检查忽略规则。记录来源文件、版本、SHA256 或设备取样日志，避免将固件随源码补丁导出。现有主机原型不需要固件文件。

## 单核与中断

Poincare 比对 DT、加载器和运行日志。主代理复核 R16 `console.log:121` 和现有 GIC 源码。`bootcpu=1` 在本机表 `{0,1,0x100,0x101}` 上选择 `0x1`，`cpus=1` 使 XNU 枚举一个逻辑 CPU，`0x0` 归驻留存储服务。R15/R16 都测到 `logical=0 mpidr=0x80000001 affinity=0x1`，R18 镜像参数读回一致。

匹配 stock `lithium-stock.dts:4785` 和 Linux `msm8996.dtsi:3715` 都描述 GICv3。HakiOS `gic_v3_irq.c:410` 通过 redistributor affinity 找当前核，`:508` 设置 nonsecure affinity routing。较早运行中的 PPI27 计数证明本地计时器路径，SPI 分发未验证。现有 `:237` 的 `gic_v3_spi_attach()` 以当前 MPIDR 设置单线 `IROUTER`，马上解除屏蔽。候选补丁必须校验该线属于本模块、`GICD_TYPER` 覆盖该 INTID、实际 `ARE_NS` 生效、目标 route 回读正确，再解除屏蔽。固件可保留其它 SPI 的配置，不能批量写全局路由。

| 来源 | GIC 配置目标 |
|---|---|
| 触摸 GPIO125 | 从 stock 的 TLMM 父 SPI 208 进入 GIC，INTID 240，父线 level-high，GPIO 子线 falling edge。QUP 自己的 SPI 106 是 INTID 138。DTS `:13204` 和 `:4851`。 |
| Adreno 530 | stock SPI 300 → INTID 332。主线 DTS 使用 level-high。先确认模块状态和完成 fence，可从受限轮询开始。 |

INTID 240、332 的 `IROUTER` 偏移为 `0x6780`、`0x6a60`。本次预期单一亲和目标 `IROUTER=0x1`，`IRM=0`。GPIO125 只是 TLMM 子中断号。不能作为 GIC SPI 参数。

## 后续主机实现与证据边界

后续工作及完整要求对照见 `reports/overnight-20261118/REPORT.md`。新接入的实际内核代码仅为 `ports/hakios-lithium/peripherals/kernel.inc` 的默认关闭准入诊断；它不调用硬件，遇到触摸/GPU申请输出 blocked/unverified。`maintenance/hakios-lithium/peripherals/README.md` 逐项区分协议逻辑、主机模型和缺失的生产连接。14组最终主机测试见 `accepted-host-tests/passed.json`，不构成物理触摸或GPU阶段0/1验收。

独立复核发现的具体接口缺口：QUP FIFO候选只支持1–32字节单消息/STOP，尚不具备 `touch-i2c.h` 要求的寄存器地址写入加 repeated START 读取；触摸队列还需帧时间戳、整批提交/背压和真实消费确认。GPU提交中的固定fence地址仅属于CPU数组mock，不能上硬件。

R16运行时亲和证据由主代理再次直接核对：`reports/local/20261118-042809-642502-ios16-r16/console.log:121` 实际为 `LITHIUM RUN A logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 bootcpu=1 cpus=1 expected_affinity=0x1 match=1`。这补足了较早 `OPERATIONS.md`/`project.json` 仍写R16等待证据的记录差异；只证明该次已保存日志，不查询或推断当前手机状态。
