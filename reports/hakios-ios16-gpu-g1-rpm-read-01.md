# GPU G1 RPM只读通道采集01 · 2026-11-25

实机采集通过。CPU614.4配置模型保持，隔离RAM；没有FIFO/doorbell、PMIC、GPU或SMMU写入。

启动时间 `2026-11-25T22:21:41.936212+08:00`，镜像SHA256 `f1d86d8cbd330c2da4a5836e1432be0a4b6a90c74e6ca3b943769a24d45b015d`。
消息RAM `0x68000 + 0x6000`，TOC `+0x5f00`，64word双遍全部有效一致。
TX offset=`0x800`、RX offset=`0xc00`，
数据长度 `0x3f8` / `0x3f8`，四个继承索引两遍全零：`True`。
本次 `142`次读取、`216`µs，一次完整VM页映射已释放；写入回调0、core写0、doorbell0。

这是存在有效TOC/FIFO且当前为空的证据，不代表通道所有权、版本协商、OPEN ACK、MX corner或bus投票已获证实。

## 退出和传输

180秒正常timer、四路stop/source丢0、drain及独立重放：`True`。
保存 `286`帧 / `80222`raw / `71070`console字节。
接收器终态 `capture_failed` / `[Errno 32] Pipe error` 保留；
一次Fastboot终点核验 `True`，时间 `2026-11-25T22:26:04.280280+08:00`。

## 实现、构建和复核

Aristotle实现`gpu/rpm/rpm-read-adapter.inc`及169场景actual-adapter专项，ASan/UBSan与AArch64对象；主代理核对源码与实际编译树字节一致。
主代理接入默认关闭`gpu_rpm_diag`、15参数专项、实际完整HakiOS链接、打包/逐层读回和全部设备操作。
实际内核SHA256 `32e10b383ec1e68932714450c196e54d73c414306af4f3da894e86e11afb9776`。
主代理独立核对128个TOC word、8个index、地址范围和USB原始帧，不把mock当硬件ACK。
4条TOC记录前缀与CPU温度printf发生交错，完整TOC字段均保留；分析器从独立标记严格解析，
原始字节和交错前缀保存在证据中，未修改原始日志或推测缺失字段。
证据与可回退补丁位于`evidence/gpu-metal-20261125/rpm-read/`。

下一步在同一RAM约束下尝试RPM版本协商和OPEN，不复用这次历史索引作为新运行的准入证明。
MX/bus、GPU时钟/GDSC/chipID、PAS13/SMMU/CP/Metal仍待实际执行。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_rpm_diag=1
```
