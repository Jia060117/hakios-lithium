# R33 自动返回 Fastboot 真机测试（二次尝试）

日期：2026-11-20（Asia/Shanghai）  
镜像：`artifacts/hakios/lithium-ios16/phase4-r33-return-fastboot/lithium-hakios-ios16-r33-return-fastboot.img`  
SHA-256：`7a4010a051e16c9f339d1ed5b04ea64abc7ac69e952de3072d78dab94a5a956e`

## 结果

启动前只读 Fastboot 核验通过：序列号 `54223ce9`，产品 `MSM8996_A8`，解锁状态为 true，`battery-soc-ok=yes`，下载窗口充足。Fastboot 于 **04:59:57.288+08:00** 接受唯一一次 R33 临时 RAM 启动。参数保留 `bootcpu=1 cpus=1`、`ufs=0`、180 秒 timer、自动返回测试开关和10秒摘要延迟。没有触摸事务。

启动后约 **245.4 秒**，一次只读 `fastboot devices` 枚举发现同一序列号 `54223ce9` 处于 Fastboot，随后 `getvar product` 返回 `MSM8996_A8`。因此，主机侧确认了“本次临时启动后，同一设备重新出现在 Fastboot”。没有发第二次 boot、reboot、刷写或分区命令。

USB 保存 41 帧、9226 控制台字节。独立解码与冻结接收器完全一致，首偏移0、末偏移9226，缺口/重复/残帧/device-loss 均为0。日志含 `loader_return_after expired`、正常 timer 返回边界和首次 `drain=1 target=9178`。接收器 **05:03:29** 报 `[Errno 32] Pipe error` 后结束；该错误不否定已存字节的完整性，也不能证明 UEFI prepare/commit。USB 串流没有返回记录、最终双 drain/返回 cookie、UEFI prepare 状态和 commit 终态；不能从此处补推这些缺失值。

用户随后明确确认测试期间没有按电源或音量键。结合启动后同一设备重新进入 Fastboot 的只读观察，本次 R33 隔离 RAM 配置的**自动返回 Fastboot 已获真机验证**。USB 串流仍没有 UEFI PREPARE/COMMIT 的完整运行字段；这是固件内部取证缺口，不改变本次硬件端点结果。该验证仅适用于显式启用的 RAM 诊断配置，**不代表 UFS 系统配置已支持自动返回**。

## 操作边界

- 启动前5项只读 Fastboot 预检；一次 Fastboot 临时启动；启动后一次只读 `fastboot devices` 和一次 `getvar product`。
- 分区读写/刷写/擦除、ADB、Android/Recovery、QEMU、触摸 I2C 均未执行；无重试。
- 冻结 R20-display 和 R32 镜像保持不变。此轮前次 R33 无效尝试仍保留其独立记录。
- 该包只覆盖独立 RAM 诊断配置；不能据此声称 UFS 系统配置也支持自动 Fastboot 返回。

## 证据

- 完整主机记录：`reports/local/20261120-r33-return-fastboot-attempt2/`
- 设备命令及时间：`commands.json`
- USB 原始流/解码：`console.frames`、`console.log`、`usb-audit.json`
- USB 接收器状态：`usb-status.json`（Pipe error 原样保留）
- 返回枚举：`return-verification.json`
- 本次运行结果：`result.json`

