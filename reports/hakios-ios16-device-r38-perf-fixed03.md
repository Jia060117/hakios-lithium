# R38 perf-fixed03 / frequency12 单次真机结果

**用户照片确认：本轮已通过 LOCK 和切换前校验，完成第11次 MUX 写4及立即读回，随后在切换后的最终快照比较失败，受控停在UEFI，未启动XNU。**

2026-11-21 12:31:01.235515（Asia/Shanghai）Fastboot接受 `e5dd2cd4d7fd35a18ca47ad6cc2626effa559c9036ca88ce33debe09ffbe0e43` 一次，大小16334848字节。五项只读预检通过，USB接收器先启动；修正 primary ACTIVEbit30 比较的冻结包，RAM隔离/ufs0/三核/180秒+正常Fastboot返回。没有分区、刷写、擦除、ADB、Android/Recovery、QEMU、重启命令或重试。累计 HakiOS75 / iOS16 44 次。

USB到12:33:59.966365未枚举，0帧，没有产出console/frames文件，因此无可重放的数据流；0缺口计数不代表传输完整。12:35:02.360020，在接受后241.124秒执行唯一一次只读 `fastboot devices`，枚举为空，未执行product查询，也未再次查询。后续照片补齐了本次11次写入、先前LOCK样本及最终失败阶段，见下文。最终寄存器原始值、CPU性能/实际MHz、timer/drain仍无证据。

本轮没有新的构建或源修改。主机验证及固定参数见 `reports/hakios-ios16-r38-perf-fixed03-prepared.md/json`；原始尝试目录 `reports/local/r37-attempts/r37-frequency-12`。原索引、R20及冻结镜像保持。

请先保留屏幕并回传正面照片，拍清 `LITHIUM PERF UEFI LOCK`、`END`、`RETAINED`，若出现panic则同时保留panic首行和栈。先定位本轮停点再修订，不在缺失原始状态时调整时钟或供电参数。照片保存前不要重启。四核及正常频率目标仍未完成。

## 本轮照片补证

用户本轮原图已保存为 `reports/local/r37-attempts/r37-frequency-12/screen-terminal-original.jpg`，SHA256 `85ae02aca41f309ead28eaf3ad2735375efd061f5f87a0e57f5ef2c486a452db`。phase7写PA0x06480040 requested4/observed4/valid1/wrc0/rrc0；LOCK轮询first/last0xc0118000，mask0x3ffffff8/mismatch0，poll1/elapsed50µs。结束为status4/phase8/PLL和core attempts11/observer read commands78/gates14/launch_aborted1。

实际源码中phase8是FINAL，只有写MUX及立即读回、5µs等待通过后才到达。READBACK_ERROR表明第二次完整snapshot读取成功而 `r38_perf_programmed_snapshot` 条件不符；硬件读回调失败会返回READ_ERROR2，区别明确。照片没有输出这次最终snapshot的MODE、L、USER、CONFIG、TEST、ALT、MUX、CLKCTL，因此不能锁定哪一项失败。`fault_pa=0x06480044` 仅是按固定顺序最后读取的CLKCTL地址，不证明该寄存器不符或外部abort。

LOCK行记录的是前一次等锁样本，不能证明MUX之后LOCK持续置位；MUX立即读回4也不能代替5µs后的最终MUX值。primary/cpu model Hz0是未完成验证的默认值，不是实测0Hz。未进入XNU，未走正常timer/自动返回路径；不是内核panic或Fastboot复位失败的证据。

主机下一修订补输出已经捕获的全部11项最终值、valid位图和期望MUX/ALT。保持硬件写序列和严格谓词，不根据缺失raw猜改CLKCTL或其他掩码。之前日志只保留总错误码及先前LOCK样本不足以定位这一步，现一次补齐。照片已保存后可人工返回Fastboot；后续必须单独准备的新包，不重试冻结frequency12。
