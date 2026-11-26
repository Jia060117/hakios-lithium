# GPU G2 PAS13 01 · 2026-11-26

PAS 认证并关闭：False；内存准备通过：False。
单次 RAM 接受时间 `2026-11-26T04:44:39.422473+08:00`，镜像 `8f036a3bac1a5b4f2b5481f2dbf51c2604f3ba4bfd45fb5740d94f6220aa96b7`。
CPU 保持614.4 MHz配置模型；未提交CP、未运行Metal。

## 实际调用和准备

```json
{
  "prepare": [
    {
      "line": 108,
      "error": "4"
    }
  ],
  "reservation": [
    {
      "line": 109,
      "requested": "1",
      "prepared": "0",
      "prep_error": "4",
      "cookie": "0xc85455b40",
      "code_pa": "0x92300000",
      "owned_bytes": "8192",
      "va_map_bytes": "16384",
      "metadata_pa": "0x0",
      "metadata_bytes": "0",
      "code_nc": "0",
      "metadata_nc": "0",
      "metadata_reserved": "0",
      "managed_retype": "0",
      "memory_release": "0"
    }
  ],
  "smc_before": [],
  "smc_after": [],
  "pas_end": [
    {
      "line": 110,
      "error": "0",
      "phase": "0",
      "used": "0",
      "calls": "0",
      "pas_attempts": "0",
      "pas13_known": "0",
      "pas13_supported": "0",
      "metadata_writes": "0",
      "code_zeros": "0",
      "code_writes": "0",
      "authenticated": "0",
      "shutdown_ok": "0",
      "hold_dependencies": "0",
      "memory_pinned": "0",
      "resume": "0",
      "cp_commands": "0",
      "qualification_bits_manufactured": "0"
    }
  ],
  "combined_end": [],
  "diagnostic": []
}
```

未进入时的零初始化 PAS end 不代表认证成功。元数据未通过只代表准备拒绝，不推断安全世界返回。

## 运行终点

正常timer/drain：True；stop温度记录4条；源争用丢字节[0]。
USB独立重放True，227帧 / 63199 raw / 55935 console字节。
接收器原始终态 `capture_failed` 保留；唯一同机Fastboot终点True。
生命周期通过True，不等同PAS或GPU加速通过。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=1 gpu_probe=1 gpu_wd=1 lithium_log_retry=1 gpu_power_clock=1 gpu_zap=1
```
