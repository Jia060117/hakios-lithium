# HakiOS iOS 16 lithium 触摸 Step 5：QUP/I²C 安全隔离主机验证

日期：2026-11-19（Asia/Shanghai）

## 结论

完成主机侧 QUP/I²C 释放与隔离契约强化。遇到传输状态不确定、返回长度异常或安全交接失败时，适配层保留独占 claim 并将控制器隔离；后续访问必须被拒绝，直到显式恢复。可靠完成的 OK/NACK 路径才允许释放。此变更只作用于外围主机候选和测试，**没有接入活动 HakiOS 内核，也没有生成可进行物理触摸验收的镜像**。

R20-display 是唯一显示基线：用户确认显示正常；R19 已上机但分辨率错误，现归档、不再复测。R20-display 与 R15 冻结镜像哈希在测试前后保持一致。

## 主机验证

执行命令（最终通过的复跑）：

```powershell
wsl.exe -d Ubuntu --cd /mnt/f/IOS-MIX1 --exec python3 scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir /mnt/f/IOS-MIX1/reports/touch-step5-host-run-20261119-r3
```

退出码 0。14 项既有外围回归与 5 项新增套件均通过；所有 C 测试以 Clang、AddressSanitizer 和 UndefinedBehaviorSanitizer 构建。新增覆盖包括 QUP 组合传输的 1,792 个 MMIO 故障注入点、117 个错误路径、54 个入口 MMIO 故障点及 54 个硬截止点，以及触摸投递、SPI affinity、A5xx fence packet 和 FT5x46 帧/状态机测试。结构化逐项输出保存在 `reports/touch-step5-host-run-20261119/passed.json`。

`git diff --check` 通过。冻结镜像 SHA-256：

- R20-display：`3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`。
- R15 desktop baseline：`ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`。

补丁导出于 `reports/hakios-ios16-touch-step5-quarantine-host.patch`，SHA-256：`9f77e477a8aba3f963565181fc0d8d161cc2fcb6e700045e76e98708c0e19863`。补丁只覆盖 `touch-i2c.h`、`touch-i2c-test.c`、`qup-combined.h`、`qup-combined-test.c`；工作区其余既有变更保持原样。本阶段未创建提交，避免把仓库中其他未提交工作混入。

## 范围与未验证项

- `touch-i2c` 隔离语义及 QUP combined-transfer 故障模型通过主机测试；该结果不能证明 lithium 的 QUP6 可安全接管。
- QUP6 当前所有者、UEFI 到 XNU 的交接状态、时钟、pinmux、供电和真实总线恢复能力仍未验证。
- 触摸 IRQ GPIO105/125 定义冲突尚未由运行时证据解决；GPIO89 复位和共享供电状态未验证。
- FT5x46/FTE8716 家族、I²C12/QUP6、地址 0x38 是静态 DTS/Android 资料结论；目标机运行时 ID 与分割读帧的一致性未验证。
- 没有查询、启动或写入手机；没有刷写分区、操作 userdata 或运行 QEMU。
- 没有完整 HakiOS 生产构建：本阶段未集成活动内核 producer，R21 仍是无真实触摸后端的只读 Stage 0 包，不能用于触摸验收。

## 并行执行与复核

主代理重跑完整 19 项主机测试，并检查冻结基线哈希、差异范围和导出补丁哈希。独立只读复核在 `qup-combined.h` 发现一个 P2 状态语义问题：QUP 超时被提前映射为普通 I/O 错误，导致上层 timeout 分支不可达；也指出 QUP 适配器的隔离 helper 缺少直接测试。主代理修正了返回映射：cleanup 不确定时返回 `CLEANUP_UNCERTAIN` 并隔离；cleanup 已知安全时保留 `TIMEOUT` 分类；另为真实 `lqc_touch_checked_release` 和 `lqc_touch_quarantine` helper 增加直接测试。复核未发现可确认的错误释放路径。修正后完整回归再次通过。代理耗时没有精确计时；按本次会话记录约一分钟。

## 下一步

先建立可核验的 QUP6 资源所有权/UEFI→XNU handoff 契约，并确定安全访问、恢复与隔离的具体内核接口；不能以寄存器读数看似空闲作为所有权证明。然后将已审查的传输与隔离策略接入一个默认关闭的 lithium producer，补齐实际活动内核构建和包冻结。达到这些主机侧门槛后，才按既有授权从 Fastboot 临时启动新包；不刷写分区、不写 userdata。
