# R21 阶段 1：触摸、GPU、GIC 和公共前置审计

> **后续状态更新（2026-11-19）：** R19 已上机测试，显示分辨率不正确，现归档且不复测。本文中把 R19 显示尺寸列作待核验的旧阶段结论，已由 R20-display 验收记录取代；R20-display 是唯一有效显示基线。

日期：2026-11-18。范围：源码/DTB/保存的 Android inventory 审查与主机侧资料核验。未查询或操作手机，未使用 ADB/Fastboot/QEMU，未写分区或固件，未修改设备运行参数。触摸/GPU 结论按“文档与 host model / 活动内核集成 / 真机证据”区分。

## 公共前置

- `ports/hakios-lithium/peripherals/boot-options.h:41-50,56-104` 是外围 boot-args 解析器，识别 `touch=`, `touch_log=`, `touch_poll=`, `gpu_probe=`, `gpu_stage=0..4`, `gpu_wd=`；解析错误时输出保持零值，相关开关默认关闭。`touch_log`/`touch_poll` 依赖 `touch=1`，`gpu_wd` 依赖 GPU probe/stage。已有对应主机用例位于 `maintenance/hakios-lithium/peripherals/boot-options-test.c:10-54`。
- `ports/hakios-lithium/patches/0019-lithium-peripheral-admission.patch:1-20` 把初始化挂到上游 input init；`ports/hakios-lithium/peripherals/kernel.inc:18-56` 在普通 thread-call 中只检查 affinity 并输出 `LITHIUM TOUCH` / `LITHIUM GPU` 的 disabled/blocked 状态。目前不访问 MMIO、不挂 IRQ、不初始化触摸或 GPU。无参数时立即返回。
- 日志采用现有 HakiOS `printf`/控制台捕获路径。`ports/hakios-lithium/patches/0010-lithium-ready-storage-smp.patch:155-180,364-366` 定义 `lithium_log_bytes` 写入受锁保护的日志缓冲区；`ports/hakios-lithium/patches/0007-lithium-boot-observer.patch:167-192` 提供 USB drain/位置路径。外围日志前缀已符合 `LITHIUM TOUCH` 和 `LITHIUM GPU`。USB 真机枚举与完整传输仍需另行验收。
- TLMM GPIO、MSM8996 QUP/I²C 与 PM8994 SPMI/RPM 在活动 HakiOS lithium 内核中没有已核实的设备后端；外围目录里 `qup-transfer.h`、`qup-i2c.h` 和 `touch-i2c.h` 是 host-tested 候选/回调契约，README 明确将板级 pin/clock/power handoff 与硬件接入列为缺项。GPU 的 SMMUv2、时钟/GDSC/调压和 SCM/PAS 适配同样未集成。GICv3 本身存在，见下节。

## A1：MIX 触摸硬件和 HID 路径

匹配 lithium 的保存 DTB/DTS 与 Android inventory 证据：

- `artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4844-4861` 配置 QUP/I²C12，基址 `0x075ba000`，输入时钟 `19.2 MHz`、输出 `400 kHz`，active pinctrl 选择 GPIO87/88 的 `blsp_i2c12`。DT 没有标明两根线哪个是 SDA/SCL，也不证明当前所有权或驱动可以工作。
- `lithium-stock.dts:4863-4883` 节点为 `ft,ft5x46_i2c@38`；保存 inventory 的 `reports/local/20261112-142718-199681/inventory.json:190-192` 也记录 `ft5x46` 位于 `12-0038`。配置倾向 FocalTech FT5x46/FTE8716，运行时芯片 ID 和固件版本未读。
- **DT 中断矛盾未解决：**通用 `interrupts` 属性在 `lithium-stock.dts:4866-4867` 引用 GPIO105；`irq-gpio` 与活动 pinctrl 在 `:4881-4883`、TLMM 配置引用 GPIO125。保存审计 `reports/hakios-ios16-lithium-peripherals-audit.md:25,64-67` 将 GPIO125 映射到 TLMM SPI208 / GIC INTID240，并记录 Android 使用命名 GPIO 的下降沿。不能把其中任一候选静默当作已验证 wiring。GPIO89 是 reset。审计记录的低电平 10ms、随后等待 300ms 来自 Android driver 流程，不是 DT 属性；实机时序/状态未测。
- `lithium-stock.dts:4868-4873` 将 VDD/VDDIO 引到 PM8994 L14，并引用 LAB/IBB；L14 在 `:6315-6335` 配置 1.8V。LAB/IBB 与显示电路共享，现有材料未证明当前 rail owner/handoff。Android 的 rail enable 顺序是驱动工作流证据，不是 DT 的启动保证。未经 ownership 核验不能对共享显示 rail 做切换。
- 保存 Android 触摸审计 `reports/hakios-ios16-lithium-peripherals-audit.md:27-29` 记载 63-byte frame（3-byte header + 10×6-byte contact），12-bit x/y、4-bit id、`0xf` terminator、事件码 0/2 active、1 up，并用寄存器 `0x00`/32 bytes 和 `0x20`/31 bytes 分两次读取。`touch-frame.h:49-64` 将未解字段保留为 opaque；`touch-i2c.h:155-173` 将两次读取的 frame-latching 标为未验证。没有采集原始触摸帧来确认该格式或两次读取的一致性。
- HakiOS 的 `/dev/hakios-touch` 是输入样本队列，不是实体 digitizer 驱动：`upstream/hakios-xnu-kernel/xnu/files/bsd/sys/hakios.input.h:2-11,18-43` 明确说明 Apple multitouch/IOHIDFamily producer 不存在，由 backboardd 中注入代码合成事件；`userland/src/inject.c:748-807,810-849` 创建 virtual service 并生成 hand/finger digitizer event。这是虚拟 HID stand-in，不是物理触摸接通。
- 坐标变换现集中为 `maintenance/hakios-lithium/peripherals/touch-queue.h:lithium_touch_map_fraction()`，两种 host adapter 共用；screen/render viewport 和 0–65535 ABI 的配置都在这处/其 `lithium_touch_geometry`。设备逻辑分辨率和面板裁切真机尚未确认。坐标共享改动见本轮 phase 2。

## GICv3 与单核 affinity

- 活动上游 `upstream/hakios-xnu-kernel/xnu/files/osfmk/arm64/gic_v3_irq.c:225-266` 的 `gic_v3_spi_attach()` 读取当前 CPU 的 `MPIDR_EL1`，先 mask SPI，再写 `GICD_IROUTER` Aff3/Aff2/Aff1/Aff0（bit31 IRM 清零），settle 后 unmask。`:482-488` 请求 ARE_NS affinity routing。源码没有对 IROUTER 写入做 readback，也没有证明某个 GPIO 的 IRQ handler 确实以当前目标 CPU 注册并收到中断。
- R15 实测 `bootcpu=1 cpus=1` 的 affinity 为 `0x1`（见 `reports/hakios-ios16-device-r15.md`），所以若 SPI attach 发生在该 CPU，代码预期路由至 affinity `0x1`。这不是当前寄存器的真机读回。
- host fake GIC `maintenance/hakios-lithium/peripherals/irq-route.h:59-125` 检查目标在线、ARE、INTID/TYPER 界限、mask→route→readback→unmask 顺序；`irq-route-test.c` 覆盖 988 个 SPI offset 和故障注入。它未接入 HakiOS GIC 实际路径。触摸 INTID240 是 GPIO125 假设下的候选；GPU SPI 号/IRQ 尚无 lithium DT 或实机证据。

## B1：Metal/GPU 来源与 A530 风险

参考版本记录：UniXNU `c34f018aafd6befd5dc93c05a8dccd5bdce6724b`；HakiOS kernel 锁定 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`；HakiOS userland 旧入口 HEAD `2573c4a8d80ed143e534d16b5a386efa5b06ccbb`（README 说明已并回主仓库）；dipper-xnu `2854c1e22fc2d73bb7a890c2c84b80182ea5ad18`；hakios-arm64e-dada `f1ae906fcc43ba8f763663257724ba938fb820ab`；Linux A5xx 实现采用本机已固定 v6.12 commit `adc218676eef25575469234709c2d87185ca223a`；Mesa 使用本机固定 25.0.7 commit `742a20f48c59e8649533c84c4d49dd95b403f5da`。核对细节见 `gpu-reference-verification.md` 与 `gpu-upstream-components.md`。网络可用，缺失参考使用临时浅层、无 checkout clone；没有纳入项目目录。

- UniXNU 的 NativeMetal 是用户态 Metal object/front-end 与 bridge；NativeAdreno 给 Turnip 提供 buffer/import/map、submit/fence、device-info 等 IOKit ABI/服务。该硬件实现对应 A740/SM8550，不能作为 A530 backend。
- Dada 的 `gpu2` 链包含 Metal frontend → Rust/native bridge → metal2vulkan → Vulkan/Turnip，并在 Mesa patch 中增加 Darwin/NativeAdreno 后端；该项目目标为 Adreno830、SM8750、iOS26、arm64e。其 ABI 和 GPU generation 与 MIX 的 A530、iOS16.5 arm64 不同，代码仅可用于架构参考。
- dipper-xnu 的已公开资料针对 SDM845/A630；不是 MSM8996/A530 验证。旧 HakiOS userland 仓库当前只保留“moved”说明，新的 GPU 源码需看 kernel 主仓及对应插件链。
- 当前 `gl=0 metal=0` 路径让 CoreAnimation 保持软件绘制：`upstream/hakios-xnu-kernel/userland/src/inject.c:1032-1068`；单独 `gl=1` 会用 Mesa OSMesa software renderer（`userland/build.sh:41-50,320-325`），不等于启用 Adreno。Metal 插件是否命名/加载受 `metal=` 路径控制（`userland/src/inject_sb.c:747-766`，kernel framebuffer bridge 在 `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:2021-2029`）。Stage 0/1 和 Stage 2–4 的真机渲染仍未验证。
- Linux v6.12 提供可追溯的 MSM/A5xx Android DRM 驱动、PM4/PFP/GPMU 和 secure ZAP/PAS 路径；这证明存在来源材料，不证明可以直接在 HakiOS 或 Xiaomi TZ 下调用。Mesa 25.0.7 把 A530 归 A5xx，但 Turnip 初始化只放行 gen6/gen7；同版本 Freedreno Gallium 有 A5xx `fd5_screen_init`。因此 `metal2vulkan` 或改 transport 不能单独补出 gen5 Vulkan backend。
- A530 硬件必要条件按证据状态：Linux 来源中的 PM4/PFP/GPMU 文件名可查；MIX 实际固件文件/版本未取得；ZAP 是否由当前 TZ 留存加载、SCM PAS 能否从此 UEFI/XNU 环境使用未知；GDSC、GPU/AHB/RBBM clocks、regulator、电源时序、GPU SMMUv2 context/cache maintenance 未验证。未经逐项证实，Stage 0/1 保持无 MMIO fail-closed。不要把固件复制进仓库。

## 调查结论与风险排序

| 优先级 | 项目 | 现状 / 主要阻塞 | 下一最小证据 |
|---|---|---|---|
| P0 | 维持桌面基线 | R15 是已目视到锁屏/桌面级别的基线；R19 新屏幕尺寸仍未真机验证。 | 未来恢复设备权限后先无新开关确认基线。 |
| P1 | 触摸 wiring | FT5x46/I²C12/0x38 与 GPIO87/88 有 DT/inventory；GPIO105 vs GPIO125 IRQ 冲突。 | 在可操作手机时从同一匹配 DTB/Android driver 导出完整 phandle/pinctrl/IRQ、driver 使用 GPIO 与 raw frame，确认 rail handoff。 |
| P2 | HID 端到端 | 当前只能走 HakiOS 虚拟 backboardd service；没有实体 controller producer 或 IOHID consumer 验收。 | 明确 kernel producer 到现有 queue ABI 的接口和生命周期，再做主机与设备分离验收。 |
| P3 | A530 Stage0 | 缺电源/时钟/GDSC/SMMUv2/PAS 地址与 owner；不安全试读可能造成 XPU/总线故障。 | MIX Android DT、clock/regulator/IOMMU/firmware inventory 和安全访问条件。 |
| P4 | A530 Stage1/Metal | A530 gen5 Turnip path 缺失；PM4/PFP/GPMU/ZAP、Darwin backend、IOSurface/fence/cache 尚未闭合。 | 先实现/审查独立 A5xx backend 与协议合规测试，再考虑任何设备提交。 |

### 仍未验证 / 需要以后提供的材料

- 触摸 runtime chip ID、controller firmware version、raw idle/down/move/up report、split-read frame latch 性质、GPIO105/125 的实际 driver path 与电平/边沿、GPIO89 reset 所有权与电源时序。
- lithium 的 QUP12/TLMM pin ownership 和 HakiOS 可用时钟/复位入口；PM8994 L14 与共享 LAB/IBB 在 UEFI→XNU 的 rail handoff。
- A530 PM4/PFP/GPMU/ZAP 文件是否来自 MIX Android/vendor firmware，版本与 secure loading path；GPU clock/regulator/GDSC、SMMUv2 stream/context mapping、DMA/cache policy、IRQ/SPI、reset/timeout。固件只在设备提取后临时供本机使用，不加入 Git 或分享包。
- 触摸物理事件进 backboardd/IOHID，GPU Stage0 寄存器读取、Stage1 fence/IRQ、IOSurface 合成、SpringBoard 加速都未真机验证。

## 并行调查阶段记录

- Dewey：A1 lithium DTB/Android inventory 与 HID path 审查，返回可复核结论；主代理抽查了 DTS 第 4844–4883、14221–14235、6315–6335 行及 HakiOS input/inject 源码，引用存在且含义吻合。无文件改动。工具没有提供精确单代理耗时。
- Wegener：B1 Metal/GPU 链路审查；多轮限时等待后仍运行，未交付可复核结论，主代理已停止。主代理转用固定版本源码、先前带来源行号的两份 GPU 审查和浅层远端 HEAD 独立核对关键结论；未接纳代理文件。
- Bernoulli：B1 A530/A740 可行性审查；限时催收后仍运行，已停止。主代理独立核对本地 Mesa/Linux pinned source 和现有来源报告；未接纳代理文件。
- Darwin：GIC affinity 审查；限时催收后仍运行，已停止。主代理核对 `gic_v3_irq.c:225-266,482-488` 与 fake-GIC host model；未接纳代理文件。
- 调度：四个只读任务并行启动；三项在多次等待后被停止，流程继续。单代理精确运行时间未由执行工具返回，不能伪报；只读浅层 clone 在系统临时目录，不属于项目改动。

## 阶段 1 冻结/回退

本报告为本阶段唯一项目输出；对应的 `R21-phase1-audit.patch` 可独立移除本报告。阶段 1 未修改内核/loader/DTB、boot-args、userdata、分区或镜像。固定桌面基线与候选镜像 SHA256 在主报告和阶段 2 测试记录再次核验。
