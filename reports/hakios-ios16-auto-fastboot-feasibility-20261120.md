# 定时结束后自动返回 Fastboot：主机端可行性审查

日期：2026-11-20（Asia/Shanghai）。范围：源码、构建契约和原厂参考资料审查；没有查询、启动或复位手机，没有修改生产源码或构建镜像。

结论：可以设计并加入；控制逻辑属中等集成工作量，包含板级复位验证后的整体复杂度为中等偏高。现成的定时返回和 USB 排空机制可以复用；尚需实现并验证 MIX 1 的 bootloader 模式选择及正确的硬件复位路径。不能把现有普通冷重启当作已实现 Fastboot 返回。

## 主代理：结束路径核对

本轮读取的准备树为 `/home/jia/ios-mix1-build/hakios-lithium-ios16`；它是当前完整 HakiOS 正常构建源，不能因此断言其每个文件都与旧冻结包所复用的 loader/Mu 字节相同。

- `xnu-src/xnu-8796.121.2/osfmk/arm64/loader_return.c:557–596`：正常定时结束先执行 LS 数据库收尾、Data 保存与各驱动 stop，然后排空 USB。第一次排空预算 5000 ms，输出结果后再做 250 ms 的尾部排空。
- 同文件 `:585–587`：第一次排空结果目前仅保存在局部变量，第二次结果被丢弃；不能认为 UEFI 已有可靠的“全部排空成功”标记，更不等于主机已持久保存日志。
- 同文件 `:439–479`：正常返回及 panic 的公共收尾最终均调用 `loader_jump(0, 0)`；`:483–488` 的 checkpoint 使用独立 reason 2。新增功能需要明确传递退出资格，不能仅凭 reason 0 或日志中的 timer banner 自动复位。
- `xnuloader/loader.c:2518–2644, 3734–3830`：独立内核核返回后，loader 的观察核恢复执行，收回运行状态并处理日志。功能适合放在有效返回之后的 UEFI 端，复用既有交接，不直接从仍在收尾的 XNU 中重启硬件。
- `xnuloader/loader.c:423–469, 4653–4657`：Lithium 当前有意保留摘要并永久等待；提示用户手动电源＋音量下。停在摘要是代码设计。
- `ports/hakios-lithium/firmware/lithiumPkg/Library/ResetSystemLib/ResetSystem.c:6–18`：仅提供通用 PSCI reset/off；ResetPlatformSpecific 和 ResetSystem 忽略传入 Data，Warm 也转到 Cold。因此传入字符串 bootloader 不会触发这里的专用模式处理。
- `ports/hakios-lithium/firmware/lithiumPkg/lithium.dsc:54,68`：该库是 HwResetSystemLib 绑定，配置包含 ResetSystemRuntimeDxe。还需核对新隔离 Mu 实际链接产物，不能只凭 DSC 宣称硬件路径验证通过。
- `artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/contract.json`：当前 R32 包仍复用旧只读 loader 和隔离 RAM Mu；需要改变其中任一实现时，必须制作新包和新契约，不修改 R20/R32 冻结产物。

## 复位机制证据与缺口

- 子代理在本地原厂 lithium boot.img 中只读解析了 20 个候选 DTB；选取的复位元数据一致：IMEM 父地址与 restart_reason 偏移可推导出 `0x066bf65c`，主 PM8994 PON 节点带 `qcom,store-hard-reset-reason`。这是原厂包静态证据，不能等同于当前运行固件或所选 DT 已核实。
- 固定 MiCode 提交 `1492c8e4373b7428bb0accc4f4881e97b36508b9` 的 `msm-poweroff.c:293–318` 会同时设置 PMIC 的 bootloader 原因与 IMEM 值。在上述属性成立且 dload 关闭时，它选择 PMIC hard reset；不能声称必须 warm reset。该提交是 MSM8996/Xiaomi 的生产者参考，不是 lithium aboot 消费端证明。
- 当前 UEFI 内存表没有显式 IMEM 映射，复位库没有 PMIC 原因处理。PSCI 实际触发何种复位、原因能否保留、已安装 aboot 如何读取和清除原因，都未验证。不能据此直接实现“写一个魔数＋现有 reset”的捷径。
- 仓库历史 attempt 02 曾准备过 IMEM 写入后 cold reset 的路径，但记录为未直接回 Fastboot，且 UEFI 执行本身未证实；它不能证明写入发生，也不能用来断言失败原因或 warm reset 能修好。

## 建议的实现边界（尚未实现）

1. 新增默认关闭的 `return_fastboot=1`；摘要等待可设独立有界参数，例如 `return_fastboot_delay=10`。未显式开启仍保留旧行为。
2. 只有显式开关、确认的正常 timer 返回、存储收尾结果符合本包策略以及两次日志排空均成功时，才置一次性的返回资格。当前 RAM 包没有 Data 挂载与 UFS 写入；以后 UFS 包必须另定义并验证同步失败的处理。
3. 内核向 loader 传递有版本、明确有效性的状态，loader 在检查返回状态和日志后才进入倒计时；状态缺失、panic、checkpoint、hung、drain 失败均保留摘要。不能只搜索一行文本作为资格。
4. loader 通过来源支持的 MIX 1 复位实现选择 bootloader；不写 misc/userdata/darwin 或分区表，不调用主机 Fastboot/ADB，也不自动加载或重试下一轮镜像。
5. 复位调用若返回，记录失败并停留摘要，不循环重试、不降级成普通重启。复位本身会丢失仅存 RAM 的摘要，故应保留倒计时并在复位前完成主机采集；设备排空成功不能冒称主机已写盘。若要强保证主机持久化，需要额外的主机确认协议。
6. 只读/关闭开关、正常退出、panic/checkpoint、排空失败、非法参数、状态格式错误、复位返回等分支做主机端测试；构建并读回新的完整 HakiOS/loader/Mu 包后，再单独验证 Fastboot 去向。

这能减少每轮手动按键；不能把任意用户程序退出视为测试结束，也不能在内核硬锁死或电源故障时保证执行。

## 并行执行与复核

- 主代理：结束回调、跨核返回、排空结果传递、摘要等待、Mu 绑定和 R32 包契约审查。
- 子代理 McClintock：独占 `evidence/fastboot-return-audit-20261120/reset/`，调查原厂 bootloader 原因、PMIC/PSCI 与本地 UEFI 参考；不写生产源码、不操作手机。
- 子代理原始证据及未验证清单见 `evidence/fastboot-return-audit-20261120/reset/final.md/json`。主代理抽查了实际分支/PMIC存储/映射来源，并以独立解析器复核 DTB，不直接照收结论。
- 主代理首次逐行范围校验发现两个历史 JSON 来源的审查范围尾行越界，已退回一次修正；这是引用元数据缺陷，未发现对应来源哈希不符。最终复核记录见 `evidence/fastboot-return-audit-20261120/reset-main-verification.json`，主线保护检查见 `main-review.json`。本报告不把源码证据等同于真机复位成功。

## 当前交付状态

只有可行性审查和报告；没有新开关实现，没有新镜像，没有真机测试。R20 显示基线与 R32 冻结包继续保留。需要真机核实的核心项目是：bootloader 启动原因能否在当前 UEFI 复位中保留、PMIC 复位方式是否匹配，以及复位后实际进入 Fastboot 而非 Android。
