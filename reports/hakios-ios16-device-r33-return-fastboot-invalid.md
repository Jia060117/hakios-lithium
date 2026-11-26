# R33 自动返回 Fastboot 真机尝试（无效）

日期：2026-11-20（Asia/Shanghai）  
结果：用户要求本次测试视为无效并暂停，等待下次准备好后继续。

## 结果

唯一一次 R33 临时启动于 **04:49:10.782+08:00** 被 Fastboot 接受。启动前只读检查确认目标序列号 `54223ce9`、产品 `MSM8996_A8`、bootloader 解锁、电量条件字段 `battery-soc-ok=yes` 和下载窗口。使用冻结镜像 SHA-256 `7a4010a051e16c9f339d1ed5b04ea64abc7ac69e952de3072d78dab94a5a956e`，独立 RAM 诊断配置，`ufs=0`、`bootcpu=1 cpus=1`、180 秒定时返回；没有触摸事务。

USB 接收保存 41 帧、9228 控制台字节。独立重放确认帧从 offset 0 连续至 9228，缺失/重复/不完整帧/device-loss 均为 0，解码与冻结接收器输出一致。可见正常 timer 返回边界及首次 `drain=1 target=9180`。接收器在 **04:52:42** 以 `[Errno 32] Pipe error` 结束。该串流没有包含后续 handoff/UEFI prepare/commit 的可验证记录；USB 断开不作为复位成功证据。

在 **04:53:17** 对同一设备执行了已授权的唯一一次只读 `fastboot devices` 确认，列表为空；没有执行第二次枚举、重启或重试。用户随后报告手机已经没电，并明确要求将本次上机测试判为无效、等待下次。因此，电量耗尽原因和固件是否完成自动返回均记为 **未验证**，不把当前结果判为实现通过或硬件失败。

用户澄清前的运行记录将耗尽前读到的 `battery-soc-ok=yes` 保留为原始预检结果；事后报告的没电意味着该字段不足以确认本轮有足够电量完成测试。

## 操作边界

- Fastboot 启动命令一次，已接受；R33 一次性尝试已消费，不自动复测。
- 五项启动前只读 Fastboot 检查；启动后一次只读 Fastboot 枚举，结果为空。
- 分区读写/刷写/擦除、ADB、Android/Recovery、QEMU、触摸 I2C 均未执行。
- 没有任何自动重试。R20 显示基线和 R32 冻结镜像未改。
- 下次继续前需用户说明设备已充电、连接并处于 Fastboot，并另行确认一次新测试范围；本次证据保持只读归档。

## 证据

- 运行目录：`reports/local/20261120-r33-return-fastboot-approved/`
- USB 独立审计：`reports/local/20261120-r33-return-fastboot-approved/usb-audit.json`
- 自动返回只读核验：`reports/local/20261120-r33-return-fastboot-approved/return-verification.json`
- 冻结主机准备报告：`reports/hakios-ios16-return-fastboot-r33-prepared.md` / `.json`
- 接受的镜像：`artifacts/hakios/lithium-ios16/phase4-r33-return-fastboot/lithium-hakios-ios16-r33-return-fastboot.img`
- 镜像 SHA-256：`7a4010a051e16c9f339d1ed5b04ea64abc7ac69e952de3072d78dab94a5a956e`

