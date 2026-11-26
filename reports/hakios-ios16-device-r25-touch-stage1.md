# R25 触摸 Stage 1：继承时钟门禁停止

2026-11-19，Asia/Shanghai。用户明确回复“可以上机”后，进行了一次受保护的 Fastboot 临时启动。镜像由 Fastboot 接受，内核进入 RAM 根卷并执行 Stage 1。触摸诊断在 GCC 继承配置检查处主动停止，未修改触摸时钟或引脚。用户随后确认并提供 UEFI 结束摘要照片。本次不重试，不开新修订；后续查询或启动仍须事先说明并取得对应确认。

## 本次结论

1. 同次日志确认 `logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 bootcpu=1 cpus=1 match=1`，没有落在已知较慢的 0x100/0x101 核上。
2. Stage 1 的四次 GCC 寄存器读取返回，耗时 72us，映射两页、写入零次。返回值为 `blsp_bcr=0 bcr=0 cmd=0x80000000 cfg=0`。
3. 源码要求 `(cfg & 0x371f) == 1`，实际为 0，因此输出 `inherited_root_or_reset_unverified` 并退出。两个 BCR 的 bit0 以及 CMD 的初检掩码 `0xf1` 均通过；该复合 reason 在本次具体由 CFG 条件触发，不能笼统归因于复位失败。
4. `clocks_checked=0 pins_checked=0 reset=0 transfer=0 irq_attached=0 hid_events=0`。没有进入时钟使能、GPIO87/88 配置、QUP 寄存器快照、I2C、复位、供电、IRQ 或 HID 阶段。没有取得 FT5x46 运行时 ID 或真实触摸报告。
5. 实际继承配置不满足该候选的准入条件；这不证明触摸芯片损坏、供电异常或根时钟频率是多少。不能仅把允许值从 1 放宽为 0，再直接访问 QUP。

## 精确产物与参数

- 镜像：`artifacts/hakios/lithium-ios16/phase4-r25-touch-stage1/lithium-hakios-ios16-r25-touch-stage1.img`，16,269,312 字节。
- SHA256：`f0fd106f2146bc176f448c9a694bf4e7ca0247656c323bb68ffe128f123a336d`。
- 内核：`6488c7207463172008d11d661bf53ca86dc990d71dac8ee3c8f541ea148ab81f`。
- 冻结镜像、实际内嵌 payload、Mu/FAT/只读 loader、来源哈希和 R15/R20 在操作前重新核对通过。没有重新构建或修改冻结包。

实际读回的内嵌参数如下。USB 片段没有记录 loader 最终拼接后的完整 boot-args 行，因此不能把以下内容称作同次控制台完整命令行；CPU 参数有同次运行日志佐证。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1
```

本包为隔离 RAM 诊断，Mu 不包含物理 LithiumUfsDxe，loader 为固定只读版本，没有 root_apfs/var_hfs/ufs_write/ufs_shared_pa 参数。日志确认根卷 md0 和辅助 md1 均为 4MiB RAM 卷。包含的软件路径未连接原有 UFS 系统/Data；没有执行分区查询、读写、刷写、擦除、格式化或 ADB/Android/Recovery 操作。Mu 之前厂商固件/TZ 的行为仍不在本次观测范围。

## 操作与采集

本地私有身份记录匹配；只读检查包括 Fastboot devices、product、oem device-info、battery-soc-ok 和 max-download-size。全部成功，随后只执行一次 `fastboot -s <private-device-serial> boot <frozen-private-copy>`。未运行通用旧轮次入口，未覆盖历史启动 guard 或全局 ready 合同。

- 身份检查：19:30:19–19:30:20。
- 单次启动 intent：19:30:36.648506+08:00。
- Fastboot 接受返回：19:30:38.111442+08:00。
- USB 枚举、专用串号及接口核验成功；保存 58 帧。
- 接收器：19:31:10.385678+08:00 因 `Pipe error` 退出；未重新枚举或重新启动接收器。
- 原始帧 16,704 字节，解码文本 14,848 字节。独立重解码与保存文本逐字节一致；缺口、重复、不完整帧及设备报告丢失均为 0。
- 未采到 timer expired、normal return 或 drain 标记。因此只通过已保存 USB 前缀的传输核验，**完整运行日志未通过**。

关键控制台行：

```text
LITHIUM TOUCH stage=1 phase=gcc_precheck next_phys=0x00325000
LITHIUM TOUCH stage=1 inherited blsp_bcr=0x00000000 bcr=0x00000000 cmd=0x80000000 cfg=0x00000000
LITHIUM TOUCH stage=1 end status=stopped reason=inherited_root_or_reset_unverified reads=4 writes=0 mappings=2 elapsed_us=72 clocks_checked=0 pins_checked=0 reset=0 transfer=0 irq_attached=0 hid_events=0
LITHIUM RUN A logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 bootcpu=1 cpus=1 expected_affinity=0x1 match=1
```

源码定位：`ports/hakios-lithium/peripherals/touch-stage1.inc:176` 为一次性和执行上下文门禁，`:189` 开始 GCC 读取，`:194` 为本次失败条件，`:198` 才是未到达的时钟使能阶段。控制台对应第102–105、112行。

## UEFI 照片及结束原因

原图逐字节保存并校验 SHA256 `f25b11e9c6f1a3c3b2db51a3dfd648a2904585097c3670e1202a34b8a5d50108`。可辨认文字：

```text
LITHIUM IOS16 RETURN RAW BYTES 68684
RETURN REASON UNCONFIRMED
PANIC CAUSE NOT RECOVERED
PMRD NOTIFYSYSTEMSHUTDOWN ...
PMRD NOTIFYSYSTEMSHUTDOWN DONE
PMRD PM QUIESCE TOOK 0 MS
PMRD POWEROFF ALL DRIVERS TOOK 0 MS
PMRD POWEROFF TOTAL 9 MS
RAW LOG RETAINED IN RAM
SUMMARY RETAINED UNTIL REBOOT
```

照片确认 session 已回到 UEFI 摘要，并保留了系统关机路径的尾部；不能据 `PANIC CAUSE NOT RECOVERED` 单独判定发生 panic，也不能认定正常 180秒 timer 完成。屏幕 PMRD 的时间戳不能当作本次启动耗时。

UEFI 报告 RAM raw log 为 68,684 字节，USB 只有 14,848 字节；两者是不同日志通路，不能简单相减计作 USB 丢帧，但明确不能宣称已取得屏幕所称全部原始内容。当前没有读取手机 RAM 或任何分区以补齐尾部。

USB 第126–129行显示 RAM 路径 `mount ramfs on /private/var` 返回 No such file or directory，目录初始化也失败；之后仍在打印 RAM 镜像目录。只读源码审查发现该通用 init 执行测试、run_rc 后会调用 `reboot(RB_HALT)`（`upstream/hakios-xnu-kernel/userland/src/init.c:1549`、`:1566`）。它与照片的关机路径相容，**只是候选解释**：同次 `init: done` 和 syscall 调用未被收全，尚不能确认实际触发者。该 RAM init 问题不能套用到已通过的 R20 UFS 桌面基线。

## 下一步及未验证事项

先做主机修订方案，取得必要依据后再冻结下一候选；本轮不自动修改或上机：

1. 核对 vendor 对 QUP6 RCG 的源选择、HID 编码、UPDATE/ROOT_OFF 和时钟投票顺序，设计有依据的显式初始化/交接，而不是依赖当前未成立的继承条件或直接放宽 CFG 门禁。仍需解决动态所有权和供电/NoC 准入。
2. 让隔离诊断在完成或停止后可靠保留结果、排空 USB 并明确返回原因，避免继续通用 RAM init 自检/关机路径而丢失终态。不能为修日志重新接入物理 UFS。
3. 以上具备主机检查、完整构建及包读回后，再说明精确变更和测试目标，请用户确认单次测试。R25 已消费，不重复启动，不复测 R19。

未知：触摸控制器状态/固件、QUP 访问、电源/NoC、根时钟实际频率、GPIO105/125物理 IRQ、I2C 收发、HID/backboardd 投递、全部RAM和稳定性。本次没有操作触摸或验收桌面。R20-display 仍为唯一已通过显示基线；R15/R20 字节保持不变。

## 并行执行及主代理复核

- Popper the 2nd 在启动前只读复核 Stage1 日志判据，指出 begin 之前已检查 affinity、成功/停止要以 end 字段判断，QUP VERSION 不等于触摸 ID；主代理抽查实际源码和同次日志。
- Bernoulli the 2nd 在对话中断前收到独立日志审计分工，恢复时代理状态为 not_found，没有可核验交付，不计作审计通过。
- 恢复后的独立复核由 Leibniz 进行，只读检查保存帧和失败条件；实际交付记录见同名 JSON。主代理另行运行既有 `hakios_usb_audit.audit`，核对 frame/console 哈希和具体 CFG 条件。
- 设备操作、独占 intent、私有副本、USB 接收和状态/报告由主代理完成。所有代理均不操作手机，未发生代码合并或源码改动。

## 本地证据和回退

私有证据目录：`reports/local/20261119-r25-touch-stage1-approved/`，含 commands、authorization、offline-preflight、result、原始 frames/console、冻结接收器、USB状态/审计、原图、转录及操作前状态文件。设备标识仅留在被忽略的目录，不写入共享报告。

独占 guard：`reports/local/ios16-r25-touch-stage1-boot-intent.json`，保留 accepted 历史并追加最终结果，禁止自动重试。实际单轮 runner 位于 `reports/local/run-ios16-r25-touch-stage1.py`，同样只保存在本机。

本阶段只新增证据和更新当前状态，不改活动内核、触摸实现、打包脚本或冻结产物；没有重跑构建，沿用并核对上阶段构建证据。开工前 Git staged/dirty 内容和 index 保留。本轮导出独立状态/报告补丁并验证正反向还原，不包含固件、镜像、原始照片或私有设备标识。
