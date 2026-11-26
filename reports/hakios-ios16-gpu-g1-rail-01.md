# GPU G1 GFX 受控供电循环 01 · 2026-11-25

**本轮通过：GFX 使能、ready 观察、恢复关闭。** 未打开 GPU consumer，尚未读取芯片 ID。

- 单次 RAM 临时启动：`2026-11-25T22:10:29.850489+08:00`，CPU614.4 MHz配置模型，XNU3核。
- 镜像：`e88d93a51934d8b9a703a710c4d789a9f6c82abdc2a029d80baa73743c50109e`。
- SID3 / PPID317 / APID52，唯一 PMIC 寄存器 `0x1746`：写 `0x80`，随后恢复原值 `0x00`，共2次。
- GFX STATUS `0x1708` 从 `0x00` 到 `0x81`，至少两次 ready；MX 两次前置和后置均 `0x81`。
- 供电数字配置逐项双遍回读，恢复后原值一致；voltage/MX/GDSC/GPU clock 写入均0。
- 核心和清理 error0，elapsed `30099` µs，`16980` read/write32 accesses，120 observer reads，2 PMIC writes。
- 这是 PMIC 数字 ready 证据，不是模拟电压测量，也不是整个 GPU 上电或 CP 验收。

## 生命周期与证据

正常180秒timer、四路stop温度、源日志丢失0，USB drain和独立逐字节重放：`True`。
保存 `284`帧 / `79286` raw bytes / `70198` console bytes。
接收器终态 `capture_failed` / `[Errno 32] Pipe error` 原样保留。
一次Fastboot devices/product核验：`True`，时间 `2026-11-25T22:14:39.181486+08:00`。
未单独捕获固件内部 Prepare/Commit；不会用USB断开单独代替自动返回证据。

## 实现与复核

- Aristotle：`gpu/g1-status-binding.h`、`g1-status-core.h`；420场景 / 2,971,158 checks的专项和freestanding AArch64对象。STATUS来源绑定到固定QTI authored common-driver源码镜像及本机TYPE1c/SUB09，镜像并非官方发布来源，该来源限制保留。
- Gauss：rail packager/runner草稿；中途服务配额中断。主代理接手路径兼容修正、实际打包/读回和执行，没有等待或自动重试。
- 主代理：`gpu/g1-rail-adapter.inc`、默认关闭`gpu_rail_cycle`准入、19参数专项、真实adapter mock接缝专项、完整HakiOS链接、冻结与全部设备操作；独立复核真实轨迹、120 observer读、两次写入和USB。

完整内核SHA256：`d2ab3bc31a010fb597aeeb9174cd9d6afac05b95f44579b257513a8bd9f6f5f9`。
源码可回退补丁与构建证据：`evidence/gpu-metal-20261125/rail01/`。

## 仍需推进

MX/RPM ACK、GPU bus/clock、CX/GX、chipID、PAS13、SMMU、CP/fence以及Metal计算/绘制/界面合成都未获本次硬件证据。
下一步先连接RPM通道与GPU专用总线投票，保留CPU614/R20/low01及旧冻结。
本轮不使用物理UFS、分区操作、刷写、ADB或QEMU。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=1 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_rail_cycle=1
```
