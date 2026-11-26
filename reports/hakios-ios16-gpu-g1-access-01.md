# GPU G1 APID 写归属采集 01 · 2026-11-23

实机采集通过；GPU 尚未上电。CPU 保持 614.4 MHz 配置模型。
单次临时 RAM boot 接受时间 `2026-11-23T18:19:49.515262+08:00`，镜像 SHA256 `402375d3e5762753d022735bd8d70a57371a6ef8087a9316f03cb4fbbf05e794`。
本轮 0 PMIC command、0 GPU/PMIC 电源写入，不访问 GPU aperture 或 SMMU。

## 本轮读数

13 字段双遍 26 读，valid/stable `0x1fff`。GFX APID52 写归属 EE0，MX APID127 归属 EE4。
GFX channel CONFIG0/STATUS1（DONE）支持继续准备 GFX 使能；不证明 RPM bus 已可用或电源已 settled。

| field | 物理地址 | pass0 | pass1 |
|---|---|---|---|
| 0 | `0x0400f000` | `0x20010000` | `0x20010000` |
| 1 | `0x0400f8d0` | `0x00031700` | `0x00031700` |
| 2 | `0x0400f9fc` | `0x00011700` | `0x00011700` |
| 3 | `0x0400a7d0` | `0x00000000` | `0x00000000` |
| 4 | `0x0400a8fc` | `0x00000004` | `0x00000004` |
| 5 | `0x045a0004` | `0x00000000` | `0x00000000` |
| 6 | `0x045a0008` | `0x00000001` | `0x00000001` |
| 7 | `0x008c4034` | `0x00222001` | `0x00222001` |
| 8 | `0x008c4024` | `0x00222001` | `0x00222001` |
| 9 | `0x008c403c` | `0x80008000` | `0x80008000` |
| 10 | `0x008c4044` | `0x80000000` | `0x80000000` |
| 11 | `0x008c4028` | `0x80000220` | `0x80000220` |
| 12 | `0x008c40b0` | `0x80000000` | `0x80000000` |

## 退出和传输

正常180秒 timer，4路 stop 温度，source contention drops0：True。
USB 独立逐字节重放：True；244帧，
67949raw / 60141console bytes，drain：True。
接收器终态原样保留 `capture_failed`；Fastboot 唯一返回核验：True。
未单独采集 UEFI 内部 Prepare/Commit，也不将 USB 断开单独当作返回成功。

## 实现与复核

主代理集成 `g1-access-entry.inc`、默认关闭 `gpu_access_diag`、构建打包和真机生命周期；
此前供电子代理 Ramanujan 实现 `g1-power-runtime.h` 与 adapter。
主代理独立核对全部26条原始读数、双遍一致性和 USB 重放。该采集不重复 fuse/S2事务。

下一步使用本轮 APID 结果补齐 STATUS 来源绑定、受控 GFX 使能/退场及 MX/RPMbus。
chipID、PAS13、SMMU、CP/fence、Metal 合成均未验证。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_access_diag=1
```
