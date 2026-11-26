# HakiOS iOS 16 lithium touch: QUP combined host step

日期：2026-11-19（Asia/Shanghai）。R20-display 是唯一显示基线；R19 已归档，不再复测或用于验收。

## 本轮完成

- 修正 `maintenance/hakios-lithium/peripherals-next/qup-combined.h` 的无 `SW_RESET` 触摸适配路径：首读和分段报告读取都要求已验证的 RESET、使用 repeated START、成功后以状态转换回到 RESET；发生传输/清理不确定时将适配器标记为 poisoned，后续调用立即拒绝。
- 收紧入口状态检查：`LQC_PENDING` 现在覆盖 QUP Operational 的 FIFO、service、block request 和 DONE 残留位；在没有真实清理证据时 fail-closed，避免把旧 `OUT_DONE`/`IN_DONE` 当作新事务完成。
- 修正 `qup-combined-test.c` 的故障模型：区分入口 NACK 与传输阶段 NACK，修正无 `SW_RESET` 成功路径的状态 RESET 计数，并加入残留 DONE 位测试。

## 主机验证

命令：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py
```

结果：退出码 0。既有 14 套外围回归与新增 5 套均通过 Clang、ASan、UBSan。QUP 组合测试包含 1,792 个 MMIO 失败注入、117 个错误路径注入、54 个硬截止点注入、32+31 字节分段读取、入口/传输 NACK、清理隔离和残留 DONE 位。R20-display 与 R15 冻结基线哈希检查保持通过。

## 当前边界

这仍是主机侧 QUP/FT5x46 候选模型，不是可上机的实体触摸包。活动内核的 `ports/hakios-lithium/peripherals/kernel.inc` 仍对 `touch=1` 输出 `platform_backend_unverified` 并不访问 QUP/TLMM；尚未完成真实 QUP6 所有权、时钟/引脚/供电、GPIO105/125 选择、IRQ 投递、FT5x46 ID 和原始帧读取。

主机测试不能证明 MIX 真机上跳过 QUP 软件复位是安全的，也不能证明触摸控制器在两段读取之间会保持同一帧。当前没有查询、启动或写入手机，没有 ADB/Fastboot 设备命令，没有写 userdata，也没有运行 QEMU。

## 下一步门槛

1. 先在源码中补齐明确的 quarantine/claim 生命周期接口，确保普通 release 回调不会把状态未知的 QUP 交还给其他使用者。
2. 以真实板级资源证据接入 QUP6/FT5x46 producer，保持 `touch=0` 默认关闭；完整 HakiOS 构建、包检查、冻结和独立读回必须全部通过。
3. 用户明确恢复真机测试后，先只做一次 `touch=1 touch_log=1` 的 ID/QUP/IRQ 诊断，不发送 HID；随后才分别验证轮询、GPIO 中断、原始帧和 HID 事件。

## 并行审查记录

- QUP 状态机只读审查：返回了入口/传输 NACK 测试模型混淆、poison 后 release 契约不够明确、遗漏 DONE 位的具体问题。主代理复核源码并修正 NACK 注入、claim 断言和 DONE 位入口拒绝；合并后重跑全套回归通过。耗时无法从运行环境可靠测得。
- 活动内核接入边界审查、打包/冻结路径审查：分配了互不重叠的只读范围；在本轮结束前没有收到交付，已停止。未合并其结果或据此声称接入/冻结完成。
