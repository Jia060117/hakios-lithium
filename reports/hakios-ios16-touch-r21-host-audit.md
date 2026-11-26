# MIX 1 触摸：R20-display 基线与首读主机检查

R19 已上机，用户确认其分辨率不正确，现只作归档。唯一显示验收基线为 [R20-display](hakios-ios16-device-r20-display.md)：面板 1080×2040 BGR24、步长 3240、iOS 360×680 points @3，保持 bootcpu=1 cpus=1（首读检查要求 affinity 0x1、max logical 0）。这轮没有操作手机或执行 QEMU；没有新镜像或生产内核构建。

## 触摸证据和主机结果

- 原厂 V9.5.2.0.NAHCNFA DTB SHA256 为 93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985。其 DTS 4844–4865、14221–14252 描述 BLSP2 QUP6 / I²C12、0x075ba000、地址 0x38、19.2 MHz 输入、400 kHz、GPIO87/88。已保存的 Android inventory 190–192 证实系统登记 ft5x46 / i2c-12/12-0038 / event4，不能据此证明 HakiOS 的时钟、引脚或供电状态。
- 同一 DTS 4866–4883 同时包含通用中断 GPIO105、命名中断 GPIO125、复位 GPIO89；14029–14057 的触摸 pinctrl 使用 125，13288–13313 还存在 GPIO105 的 ADV7533 模板节点，不能据此认定该模板曾启用。Russell 核对的小米公开 scorpio-n-oss 参考提交 1492c8e4373b7428bb0accc4f4881e97b36508b9 在 arch/arm/boot/dts/qcom/a8-msm8996-mtp.dtsi:113–136,197,212–214 重现冲突；drivers/input/touchscreen/ft5x46/ft5x46_ts.c:3087,3699,3887–3888 读取命名 GPIO，并用下降沿注册。旧 [stock 二进制审计](hakios-ios16-lithium-peripherals-audit.md#L25) 也记录了 gpio_to_irq 及 0x2002。125 的静态证据较强；本机电平、极性和实际投递仍未验证。
- VDD/VDDIO 同指 PM8994 L14 1.8 V（DTS 4868–4869、6323–6335），LAB/IBB 与显示节点共享引用（2009–2011、4870–4871）。Mencius 核对了 lithium.dsc:48、lithium.fdf:28、MemoryMap.c:29、lithium.config:17、0019 及 kernel.inc:29：当前可见的 Mu/XNU 输入没有 QUP6 所有权交接，现有板级 hook 仅报告 platform_backend_unverified。LK/安全世界状态、实时 GCC 时钟、GPIO87/88 mux 和上电状态仍未验证。
- [QUP 首读](../maintenance/hakios-lithium/peripherals-next/qup-combined.h#L123) 在通过外部硬件准入后，先检查控制器是否处于无残留状态的 RESET，再发一个 0x38 / 0xA3 的 repeated-START 读。首读不发 RESET 命令、不重试、不动复位脚、触摸配置或电源；成功完成 STOP 后停在 PAUSE，失败后若已开始编程则隔离控制器。轮询候选在成功 STOP 后才用正常控制器状态 RESET 重装下一事务，绝不将参考路径的 SW_RESET 包进生产读。软件可以限制轮询和调用次数，无法保证已经卡住的总线 MMIO 返回。
- [阶段调度器](../maintenance/hakios-lithium/peripherals-next/touch-stage.h#L124) 实现独立的 GPIO105/125 采样、次数和期限、跳过过期轮次，以及区分队列接收和消费者最终释放。主代理增加的 [参数和一次性首读准入](../maintenance/hakios-lithium/peripherals-next/touch-r21-probe.h#L21) 区分未传与显式 0，拒绝非法和重复参数，并独立核对 CPU 和 8 项资源前置。它们都没有绑定真实 MMIO 或 HID；split 32+31 字节的帧一致性、坐标和松手仍须实机验证。

测试命令（退出码 0，WSL Clang ASan/UBSan）：

    wsl.exe -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals-next/test-touch-r21-host.py --compiler clang

七组触摸套件加编译补丁 0020 的真实队列函数通过。QUP 普通故障注入 1792 点、错误路径 117 点；无复位首读覆盖 54 个 MMIO 故障点和 54 个期限注入点，并覆盖 NACK、总线不空闲、事务成功后无法停到 PAUSE、禁止重试。阶段调度覆盖 6 组。运行前后重新核验 R15/R19/R20 镜像、R19/R20 合同与旧严格 runner 的哈希，均一致；历史差异见 [冻结审计](hakios-ios16-touch-freeze-audit.md)。这条新测试入口只验证主机候选。

[主机补丁](hakios-ios16-touch-r21-host-audit.patch) 含本次测试所需的 12 份候选文本源码、测试和入口；SHA256 af0bcf692deca7fcec744d46cf8667aedba51e35d7018508bf8f2b81f8dbafd7。已在临时目录用 git apply --check 和 git apply 回放（均退出码 0），复核还原文件字节一致；无固件或 Apple 二进制。

## 上机前还要做什么

1. **区分显示基线与禁写策略。** R20 合同的 XNUP cmdline 包含 ufs_write=1、var_hfs=ufs_darwin_data、hfs_bench=1。loader.c:1983,2031,3030 会开放 Data 的写能力，MIX 适配把其放在物理 userdata 内从第 2042368 块起、到第 5242880 块之前的范围（4 KiB 块）。hfs-bench.inc:20 会创建、写、同步、重命名和删除文件。另有条件式 logdump 写入（loader.c:348–381），不由 Data 开关控制。新测试路径应固定 UFS 只读、禁用基准和持久 /private/var、关闭落盘日志，复用 RAM Data 路径和 USB 日志。要独立检查 loader/Mu/内核的写出口；RAM Data 能否复现 R20 主屏需要验收。
2. **给单个冻结包一个明确的选择入口。** BootShim.S:30 转交嵌入 XNUP，loader.c:2634 读取其 cmdline，3198–3204 才复制给 XNU。R20 现有路径没有可证实的 Fastboot 外层参数覆盖。下一包应只接收一个有边界、默认关闭、仅存在于 RAM 的 stage 值，并保持其他内嵌参数无覆盖入口；该机制尚未实现，暂无可执行的上机参数或命令。
3. **硬件只从静态阶段开始。** 先验收无触摸参数和静态 stage 0；再有依次通过的资源证据时做一次 ID 读；再用相同的有界采样窗口记录 GPIO105/125 的各自 high/low/edge/error 计数以及触摸帧，配合一次四角、中央、滑动、松手。轮询可能漏窄脉冲，没采到变化不排除某个脚；首轮不选 IRQ、不驱动 GPIO89、不改供电。只有帧读取和坐标成立、真实队列有可验证的松手处理后，再验收 HID 点按和翻页。
4. **另行完整构建和冻结。** 用独立的 HakiOS/Mu 工作目录、候选私有报告和新冻结输出，复用已有流程，避免覆盖旧 live 证据。按源码和依赖核验、HakiOS 内核 / Mu / D22AP / 用户空间构建、指令与回归检查、实际 XNUP/FAT/APFS 读回、离线预检顺序执行。既有 make 内层 symbol-set 例外仅按原包装的分类规则判断，不能代替完整构建和包验证。

## 并行执行记录

| 子代理 | 交付 | 主代理复核 |
| --- | --- | --- |
| Jason | 只写 touch-stage.h 与 touch-stage-test.c；6 组阶段调度测试 | 读代码和回调边界，独立编译运行 6/6，再纳入整套测试 |
| Russell | 对照原厂 DT、Xiaomi 公开源码和本地 Android 记录，区分 GPIO105/125 | 重算 DTB 哈希，核对原厂 DTS、保存的二进制审计和 GPIO 结论；公开参考不当作同版出厂源码 |
| Mencius | Mu/XNU/Xnuloader 可见所有权与 MMIO 边界 | 核对生产 hook 的拒绝路径、lithium 配置和硬件准入；不把该静态范围当作 LK/固件交接证明 |
| Ptolemy | 启动参数及 Data 写路径 | 核对实包合同和 loader 源码，主代理另核对 logdump 落盘出口 |
| Pauli | 完整构建入口、隔离路径和 make 错误边界 | 核对现有任务映射和 make 包装器，保留原冻结检查；本轮未执行生产构建 |

五项均完成，无共享源码写冲突，无补丁退回。工具未提供可比的子代理净耗时，未估填。当前无需用户操作手机。
