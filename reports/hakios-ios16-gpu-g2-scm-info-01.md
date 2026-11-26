# GPU G2 SCM INFO 01 · 2026-11-26

本轮完整验收：True。INFO 查询通过：True。
单次隔离 RAM boot 接受时间 `2026-11-26T03:54:48.002067+08:00`，镜像 SHA256 `bc0e4d9e220afbfa743648239b22876d9536f3cfe1cd89fcd860ac4d2ba90056`。
CPU 保持 614.4 MHz 配置模型。只执行 INFO 查询，0 GPU aperture 访问、0电源写、0 PAS操作、未装载固件。

## 原始调用结果

| index | x0 transport | x1 available | x2 | x3 |
|---|---|---|---|---|
| 0 | `0x0` | `0x1` | `0x0` | `0x0` |
| 1 | `0x0` | `0x1` | `0x0` | `0x0` |
| 2 | `0x0` | `0x1` | `0x0` | `0x0` |
| 3 | `0x0` | `0x1` | `0x0` | `0x0` |

总结：`{"error": "0", "calls": "4", "abi_confirmed": "1", "availability_known": "0x7", "available": "0x7", "elapsed_us": "156", "gpu_mmio": "0", "pas_operations": "0", "pas13_supported": "unverified", "firmware_loaded": "0", "retry": "0"}`。
命令级 availability 不能证明 PAS13 认证可用、固件签名接受、CP执行或 Metal 加速。

## 生命周期

180秒正常 timer/USB drain：True；四路 stop 温度记录：4。
源日志争用丢字节：[0]。
USB独立逐字节重放：True，229帧 / 63632 raw / 56304 console字节。
接收器原始终态 `capture_failed`；唯一同机 Fastboot 核验：True。

## 改动

新增默认关闭 `gpu_scm_diag=1`，使用独立内核线程；保持旧诊断和复位门禁。
每次 SMC 前后保存输入及四个返回寄存器，最多4次，transport失败立即停、无重试或ABI回退。
200ms预算只检查已经返回的调用，不能强制中断不返回的安全世界调用。
实际完整HakiOS内核构建、链接反汇编和最终Android/FAT/APFS/FV读回均已完成。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_scm_diag=1
```
