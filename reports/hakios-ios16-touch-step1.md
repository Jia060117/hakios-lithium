# HakiOS iOS 16 触摸推进记录：Step 1

日期：2026-11-19。范围：MIX 1 触摸板级审计、现有输入链路核验和主机测试。此次没有修改触摸实现、构建新镜像或查询/操作手机。

## 当前能确认的进度

R20-display 已在真机显示 SpringBoard 主屏，屏幕比例通过用户确认；这只验证显示。R20 报告仍将物理触摸标为未验证，主机当前没有重新查询手机状态。R20 镜像保持原样。

原厂匹配 DTB 为 `artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dtb`，SHA256 `93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985`。反编译 DTS 和已有 Android inventory 交叉确认（以下行号以 `lithium-stock.dts` 为准）：

- 触摸器件节点是 `ft,ft5x46_i2c@38`，属 FocalTech FT5x46 驱动族（`:4863-4865`）；库存路径为 `75ba000.i2c/i2c-12/12-0038`。固件配置列出 FTE8716 家族及四种屏幕/供应商变体（`:4900-4935`）。当前运行芯片 ID、固件版本和实际面板变体尚未读取。
- I²C12 / BLSP2 QUP6，控制器基址 `0x075ba000`，输入时钟 19.2 MHz，DT 配置总线 400 kHz（`:4844-4861`）；GPIO87/88 为 I²C12 pinmux（`:14221-14252`），器件地址 `0x38`。这确认静态配置，不证明 XNU 接管后的控制器时钟、引脚和所有权已可用。
- Reset GPIO 为 89（`:4881`）。VDD/VDDIO 指向 PM8994 L14 1.8 V，LAB/IBB 也列为供电依赖并与显示共享（`:4868-4871`、`:6323-6335`、`:8338-8363`）；当前电压、拥有者和安全交接状态未知。因此首轮接入不能擅自复位控制器或切换共享电源。
- IRQ 描述有冲突：通用 `interrupts` 指 GPIO105（`:4866-4867`），厂商 `irq-gpio` 和触摸活动 pinctrl 指 GPIO125（`:4882`、`:14029-14042`）。既有 Android 驱动审计更支持命名 GPIO125、下降沿；GPIO105 还被 DTS 的另一设备引用。仍须真机 IRQ 计数/事件验证，代码不得把候选写成已验证事实。
- 参考帧格式为 63 字节（驱动资料与主机解析器），坐标名义范围 1080×2040（`:4877-4878`），报告读取分为寄存器 `0x00` 的 32 字节与 `0x20` 的 31 字节两段（既有匹配 Android 驱动审计）。主机解析测试覆盖此格式，但实机是否一致、两段读取期间是否锁存同一帧，仍未验证。

## 已有软件链路和缺口

HakiOS 已有 `/dev/hakios-touch` 队列：`upstream/hakios-xnu-kernel/drivers/input/hakios.input.c:194` 实现 `hakios_touch_post()`，记录 ABI 在 `userland/src/hakios.input.h:18-40`。HakiOS 注入库 `userland/src/inject.c:792-848` 创建虚拟 Digitizer 服务并分发 IOHID 手指事件。因此优先把硬件接到现有队列，不需要先另造一套内核 IOHIDEventService。此路径只证明软件接口存在，不代表 MIX 1 物理触摸已连通。

当前 `ports/hakios-lithium/peripherals/kernel.inc` 是默认关闭的准入诊断，不执行 MMIO。给现有镜像传 `touch=1` 只会输出 `status=blocked reason=platform_backend_unverified ... mmio=0 irq_attached=0 hid_events=0`；`touch_log=1` 和 `touch_poll=1` 也没有真实设备后端。不能用当前 R20/R19 包做触摸验收。

维护目录里已有参数解析、帧解析（`touch-frame.h:67`）、触点状态机、坐标变换（`touch-queue.h:33-73`）、mock I²C/QUP、IRQ 策略和整批交付模型（`peripherals-next/touch-delivery.h:33-60`）。它们没有接入 HakiOS 活动内核：缺真实 QUP MMIO 执行、BLSP 时钟/复位、TLMM 配置、GPIO IRQ 注册、供电所有权检查和物理队列生产者。现有 QUP 单消息 FIFO 候选也不能替代触摸所需的“寄存器写入 + repeated START + 连续读取”组合传输。

## 主机验证

独立运行完整扩展回归：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py
```

结果退出码 0：原有 14 组与扩展 4 组均通过，使用 Clang 18、警告视为错误、ASan 和 UBSan；R19 冻结输入前后核验通过。覆盖解析、触点生命周期、mock 总线故障、IRQ/轮询策略、坐标/队列策略、组合 QUP 和交付模拟。**这只说明主机模型通过，不代表驱动编译进当前运行镜像、I²C 可用或物理触摸成功。** 未重跑整套 HakiOS 内核/镜像构建，因为活动硬件后端还不存在。

并行只读审计分别覆盖 DTB/Android 资料、HakiOS HID 接入、现有主机组件。主代理交叉检查 DTB 哈希、DTS 节点、活动准入钩子和测试输出；本次没有接纳或合并子代理代码。

## 后续分步计划

1. **主机实现阶段**：先实现并 mock 测试 QUPv2 的组合寄存器读取/repeated START、超时和失败清理；随后写只在明确平台前提成立时才准入的 lithium 后端。默认保持关闭，绝不复位或更改共享电源；无法证明硬件状态时必须拒绝启动。将帧解析结果接入 HakiOS 已有触摸队列，并测试背压、丢帧后的抬起释放和坐标映射。
2. **真机只读采集阶段**：等新后端完成构建、静态检查和冻结后，使用临时 Fastboot 启动，不刷分区或改 userdata。第一轮只记录控制器识别、I²C 错误、原始帧和 IRQ 计数；测试前后各采集 idle，然后单点按下/抬起、短滑动、多点和四角。先不触发硬件 reset，不切换电源。若读报告成功但 IRQ 不增，再用单独的轮询对照定位 IRQ；若控制器或资源前提失败，立即停止，不再访问设备。
3. **输入链路验收阶段**：只有原始帧可信后才发 down/move/up 到 `/dev/hakios-touch`，核对 backboardd 虚拟服务已打开、HID 事件与屏幕位置对应，并检查单点、多点、边界、抬起和重复触点。日志事件计数不能代替屏幕实际响应。

真机阶段需要的可判定证据：`LITHIUM TOUCH` 中控制器 ID/固件版本、QUP 初始化/combined-transfer 状态、每类错误计数、原始帧长度及有限样本、IRQ/轮询读数和丢中断数、解析后的接触状态、入队/丢弃数，以及 backboardd 服务打开和下游事件结果。首个可用镜像制备前不会要求用户为当前“blocked”镜像进行无效触摸测试。

## 尚需真机确认

- XNU 接管时 QUP6/I²C12、GPIO87/88 和相应时钟的状态/所有权；不能仅凭 DT 推断可直接访问。
- 触摸芯片运行时 ID、固件版本和实际 FTE8716 面板配置。
- GPIO125 是否为有效触摸中断，TLMM→SPI208→GIC INTID240 是否实际投递到当前 affinity `0x1`；以及 GPIO105 冲突的运行时解释。
- L14/LAB/IBB 现有状态、reset GPIO89 的 pinmux/所有权；避免影响当前显示。
- 两段读取的帧一致性、空闲/按下/移动/抬起的真实报告，以及坐标边界。
- 物理触摸到 HakiOS 队列、backboardd 与 SpringBoard 的端到端事件。

下一步按项目阶段门槛先完成主机侧后端和检查；需要设备时再使用经过冻结的新包和单次临时启动。当前仍不查询或操作手机。
