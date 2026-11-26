# R20-display 触摸胶合与源码核对阶段

日期：2026-11-19。仅主机源码、构建测试与只读来源核对；没有查询、启动或刷写手机，没有 ADB/Fastboot 设备命令、分区/userdata 操作或 QEMU。GPU 实现未修改。

## 结论与范围

R20-display 是唯一有效显示基线。用户已明确 R19 上机分辨率错误，R19 归档，不再启动、重试、修复或补验收。R20 使用 1080×2040 BGR24、stride 3240、360×680 points、scale 3；单核基线保持 bootcpu=1 cpus=1，已确认 affinity 0x1。

本阶段完成了生产用途的触摸胶合候选和 HakiOS 队列绑定，但没有把它们接入生产内核。真实 MMIO 访问条件尚未解决，不能把主机模型当作硬件后端。现有已构建的生产内核仍只有静态 Stage 0 与高阶段拒绝入口，没有新的完整触摸镜像。

## 代码与测试

| 文件与入口 | 本阶段工作 | 完成人及主代理复核 |
|---|---|---|
| maintenance/hakios-lithium/peripherals-next/touch-runtime.h:124、:211、:263、:305 | 组合单次 A3、32+31 分段帧、stage 状态机、日志、集中 R20 映射和原子投递；不自行授予平台 verified 位；运行全程保留 claim，失败隔离、不重试、不触发 SW_RESET/供电/复位 | Godel the 2nd；主代理阅读回调、期限、claim/sublease 与 cleanup，独立完整复跑；Pasteur the 2nd 审阅未发现阻断缺陷 |
| maintenance/hakios-lithium/peripherals-next/touch-runtime-test.c:233 | lqt_io 接口故障注入，覆盖未知 ID、分段读取、Stage 2 不投递、Stage 3 原子批次、期限与清理 | Godel the 2nd；主代理完整复跑，确认 178 个 MMIO 失败点与 178 个回调到期点通过 |
| maintenance/hakios-lithium/peripherals-next/touch-hakios-sink.h:40、:103 | 绑定 hakios_touch_post_batch；最新精确重复只返回回执，队列不足整帧拒绝，未知结果永久隔离；调用期限不延长运行期限；不声明 HID 消费确认 | 主代理；Pasteur the 2nd 独立审阅并运行测试 |
| maintenance/hakios-lithium/peripherals-next/test-touch-batch-patch.py:392 | 实际执行补丁 0020 的队列函数和上游 touch-feel/count-fingers，加入解析→R20 映射→delivery→队列集成；覆盖期限恢复、提交前/后的错误与不重放 | 主代理；独立审阅指出的两个覆盖缺口已补齐，最终完整回归通过 |
| maintenance/hakios-lithium/peripherals-next/touch-static.h:15、touch-static-test.c:34 | 删除没有证据的 SDA=87/SCL=88 对应断言，改为 i2c_pins=87,88 pin_roles=unverified | 主代理；源码与最终回归核对 |
| scripts/test-hakios-lithium-peripherals-next.py:34、:36 | 复用测试入口，加入 runtime 与精确源码集成，记录传递依赖哈希，不新建 R 轮次流程 | 主代理；最终证据与当前来源逐项比较，61 项无漂移 |

完整测试命令（必须选一个新的证据目录）：

    wsl.exe -d Ubuntu --cd /mnt/f/IOS-MIX1 --exec python3 -B scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/reports/touch-runtime-host-20261119-r1

退出码 0，14 套既有外围测试 + 12 套后续测试，共 26 套通过 Clang ASan/UBSan。24 个命令记录是 1 次旧回归、11 次 C 编译、11 次 C 执行、1 次 Python 精确源码集成，不是 24 套测试。证据：reports/touch-runtime-host-20261119-r1/passed.json；SHA256 14e9b517164bb3353d5c940781984db65b8416927ff3ce4d05a0b69a4151f34e。

首次新增集成测试因 FRAME_SIZE/FRAME_BYTES 常量拼写不一致编译退出 1，已修正；随后单项与最终完整测试均退出 0。没有放宽检查。模型 MMIO 回调返回 false 不证明真实总线异常可以恢复。

## 固件侧所有权：已缩小问题范围

Turing 审计完整 Mu 组件闭包。主代理抽查 scripts/build-hakios-support.py:12、:39–52，ports/hakios-lithium/firmware/lithiumPkg/lithium.dsc:56–84、lithium.fdf:19–59、Library/MemoryMapLib/MemoryMap.c:29–32，并核对实际固件字节。

- Mu 固定提交 6b3d90b79e191e08171bfedd3980234a7e49d340。lithium 只导入公共 DSC 的 Components 之前部分。
- 包含的 25 个组件没有 QUP6/I²C/TLMM 驱动或 I²C Master/Host/IO 协议提供者。其他机型的二进制包装不在 lithium FV 内，不能当作本机实现。
- R20 继承的 30 个 firmware 源文件哈希全部与当前端口一致；R20 Image 的 0x1000 起 0x300000 字节固件 SHA256 为 15c5711f16cc3b5322655a5ec382689a71f4510b42add065e42af3dc2abdbcae，与冻结记录一致。没有以当前工作区固件替换 R20。
- 常驻 loader 没有 QUP6/I²C/TLMM 服务。下一实现可由 XNU 单独持有 QUP6，先轮询、全程保留 claim，无需寻找不存在的 UEFI I²C provider。

这一结论只覆盖已包含的 Mu/loader 软件客户端；不证明安全世界已放行、QUP 可访问或触控已供电。

## 芯片、时钟与 GPIO 来源

Kant 提供来源定位；主代理按固定提交重新下载四份源文件、记录 SHA256 并抽查。缓存及来源清单在 reports/local/touch-board-source-20261119/，不纳入补丁；未下载固件载荷。联合证据为 reports/hakios-ios16-touch-owner-source-audit-20261119.json。

- 匹配 stock DTS：artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4844–4883。QUP6/I2C12=0x075ba000，地址 0x38，GPIO87/88 使用 blsp_i2c12，reset GPIO89。
- generic IRQ 为 GPIO105（:4867），named irq-gpio 为 GPIO125（:4882）。小米驱动读取 named GPIO 并 gpio_to_irq（ft5x46_ts.c:3087、:3699），所以 125 静态依据更强，但仍不认定为实测中断。先轮询，之后只读采样两脚。
- DT 四份配置 chip=0xE7（:4901、:4911、:4921、:4931），前两份 vendor=0x3B（:4902、:4912）。ft5x46_ts.c:30–33 定义 A3 为芯片寄存器、A6 为版本、A8 为 vendor；:149 定义 FTE716_CHIP_ID=0xE7。E7 是预期值，实际响应未验证。Stage 1 保留任何实际读到的 ID；Stage 2/3 才要求平台身份策略匹配。
- 驱动在 ID 读取后进入固件加载 helper（ft5x46_ts.c:3579–3592）；不能仅凭屏幕亮或 ID 成功假定报文固件就绪。首次探测不复用该初始化/升级路径。
- 上述小米源码固定提交为 MiCode/Xiaomi_Kernel_OpenSource 1492c8e4373b7428bb0accc4f4881e97b36508b9。pinctrl-msm8996.c:28–58、:1825–1828 给出 TLMM stride 0x1000、IO+4 bit0、mux shift2，GPIO87/88 的 I²C mux=3。TLMM 基址为 0x01010000（stock DTS:13204）。GPIO105/125 输入采样地址为 0x01079004/0x0108d004，只读 bit0；GPIO89 不写。
- reports/local/ios16-migration-phase2/gcc-msm8996.c:946、:1873、:2176、:3517 给出 GCC offsets：QUP6 root 0x30020、CBCR 0x30008、BCR 0x30000、BLSP2 AHB status 0x25004、vote 0x52004 bit15。GCC 基址 0x00300000。clock-local2.c:66–75 定义 enable/off/update/reset 位。这些都不是本次启动的运行态测量。

## 未验证与下一阶段

1. 真实板级 MMIO provider 尚未实现，不能伪造 lqt_io 八项 verified 前提。优先确定 QUP/GCC/TLMM 的 Device-memory 映射和访问条件，再核验继承的时钟/复位/源选择、GPIO87/88 pinmux、共享电源；首次不主动写 regulator、reset GPIO 或根时钟。
2. 活动 XNU osfmk/arm64/loose_ends.c:739–754 的 ml_probe_read/ml_probe_read_64 是 panic stub。machine_routines.c 的 ml_nofault_copy 也不是 MMIO 总线保护。copyio recovery 不能据此宣称覆盖 external abort 或永不返回的总线访问。布尔 callback 或软件期限不是这种保证。
3. 当前 hakios_touch_post_batch 使用阻塞 mutex，新 sink 明确标注。线程调度、真实回调耗时、终止后的抬起投递/消费确认仍要解决。没有 downstream fence 实现，resync=NULL；不把队列接收说成 HID 已释放。
4. runtime 测试的 QUP/GPIO/sink/fence 是模型；另一个集成测试执行实际队列源码。未执行完整 runtime→真实队列→backboardd/HID 端到端链路，没有物理触摸证据。
5. QUP 可访问后，先一次 A3，再有界 32+31 原始帧和 GPIO105/125 采样，最后开启 HID。真实 ID、分段帧一致性、是否需要固件补载、IRQ 路由、触控效果均未验证。本轮没有对应触摸冻结包，因此不给出真机启动命令。

本阶段没有重建生产 kernel/loader/Mu/userland。此前 Stage 0 内核 SHA256 仍为 3e130b765b47ec06ac5e5392d5f94301d22ed43fa37b937827b65ab220180c93；它的 21 套测试是历史构建证据，本轮 26 套不能改写为新的生产构建。

完整回归前后 R20 SHA256 均为 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82，R15 均为 ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55。

## 并行执行与回退

- Turing the 2nd：Mu/loader 所有权与组件审计；主代理复核 DSC/FDF、构建脚本、30 份源哈希、R20 固件字节。接受，未写文件。
- Kant the 2nd：时钟、TLMM、身份和 IRQ 来源；主代理重取固定提交的四份源码、保存哈希并核对 DT。接受，运行态仍未验证，未写文件。
- Godel the 2nd：只写 runtime 头文件和测试。主代理读代码并完整复跑，另由 Pasteur 复核；无共享写入冲突。
- Pasteur the 2nd：只读审阅 sink/runtime，实际跑单项 ASan/UBSan；两个 sink 测试覆盖建议由主代理补齐。无阻断缺陷，保留 mutex、真实 fence、物理平台限制。
- 主代理：队列绑定、精确源码集成、引脚标签修正、统一测试入口、最终回归、独立来源复核、状态记录及补丁导出。各代理准确耗时未记录，不编造；无代理补丁被回退。

Checkpoint：reports/hakios-ios16-touch-runtime-host-20261119.patch，是选定文本文件相对 HEAD 的工作快照，含这些文件已有的未提交内容；不是整仓库备份。Git staging 未修改，不导出固件或二进制。

当前用户无需操作手机。下一阶段先做板级访问/时钟/pinmux 接入；实际后端完成完整构建、整包检查及独立冻结后，再进入真机阶段。
