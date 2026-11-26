# R29 真机预检：未枚举到目标，尚未启动

记录：2026-11-19T23:45:57.938416+08:00。**主机准备完整通过；唯一一次只读 `fastboot devices` 返回空列表，按规则停止，没有发出 `boot`。**

## 实际发生的操作

- 只读枚举开始：2026-11-19T23:44:15.305517+08:00，结束：2026-11-19T23:44:15.342983+08:00。
- 返回码0、stdout为空、stderr为空；没有枚举到任何Fastboot设备。
- 未继续product/unlock/battery/download核验；USB日志接收器未启动；无临时启动、QUP复位/配置、I2C事务或芯片ID结果。
- 无分区查询/读写、刷写、ADB、Android/Recovery、QEMU或自动重试。
- 此前R28是最后一次已接受启动。总计仍为HakiOS52次/iOS16共21次，不增加计数。R29仅本次预检记录已结案，临时启动次数尚未消费。

枚举为空只能确认主机未看到目标，不能据此猜测电量、线材、模式或驱动原因；物理屏幕未核验。没有本轮USB运行日志，也没有硬件初始化通过结论。

## 已就绪的同一冻结镜像

路径：`artifacts/hakios/lithium-ios16/phase4-r29-touch-qup-init/lithium-hakios-ios16-r29-touch-qup-init.img`；16285696字节。

SHA256：`2dec085c75a29ea770a56231ff359ae76da0d13aa36e11836d657ea1a03b9d65`。

完整HakiOS/Mu/D22AP/413job、82来源边界、46指令扫描、42套ASan/UBSan、82组打包验证、100合并源哈希及实际APFS/整包读回均通过。新初始化core6095、实际adapter24632场景。准备报告及代码/测试分工见 `reports/hakios-ios16-touch-r29-prepared.md/json`。

本包在隔离RAM内请求一次QUP软复位及原厂I2Cv2基础配置，只有原有严格条件满足才尝试一次0x38/A3读取。默认关闭，不写GIC、外部触摸复位GPIO或供电，无IRQ/HID接入。真实复位效果、所有权、电气和ID仍未验证。R20-display仍为唯一显示基线，旧镜像及原索引保留。

## 后续

请用户确认手机仍在Fastboot并重新接好USB，确认后再用**同一冻结镜像**建立新的独立预检记录，无需重构建。保留本次空枚举记录，不清空/复用它来绕过单次检查，不自动重试、不自动重启手机。

本轮参数（未执行）：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_init=1
```
