# A530 G0 真机结果 — 2026-11-23

**G0 只读状态阶段通过。** 同一 RAM 会话读取了全部 9 个 GCC/GPUCC 字段，每个两遍有效且一致，18 次读取耗时 18 µs；正常 180 秒结束、USB 排空通过，03:56:56 唯一返回核验确认同一设备已处于 Fastboot。

**GPU 的 CX/GX GDSC 当前均报告关闭。** 这是下一阶段上电流程的真实起点；G0 没有读取 chip ID、加载固件、提交 CP 命令或 GPU 加速。

## 会话与边界

- 用户批准一次 G0 RAM 测试后，03:52:07.672005 +08:00 接受冻结镜像 `61bc8b8220f54e8a2b0772a694690901b89e48a65b2d52463c8f962ab02c7933`。
- 五项预检通过，USB 接收器先于单次临时 boot 启动；之后只读取该次保存日志，再做一次 Fastboot devices/product 返回核验。
- 真实 XNU 日志为 `3 of 3 processors are running`。G0 worker 实际 logical 0 / affinity 0x1 / max logical 2；结束返回也为 logical 0 / affinity 0x1。
- CPU 使用用户选定的 614.4 MHz **配置模型**，PMU 实测 MHz 未建立；包含短 CPU sanity 基准，未启用满载 soak。
- `ufs=0`，最终 Mu 无物理 UFS provider；PID1 在 RAM hold，没有启动 SpringBoard，不访问 Data/userdata。
- 无 GPU 寄存器写、时钟/电源写、SMMU/中断配置、固件装载；无刷写、分区命令、ADB、QEMU 或重试。本次沿用基线的 UEFI CPU PLL 初始化。
- 启动计数更新为 **HakiOS 97 / iOS16 66**；原索引、614.4 桌面/R20/low01 和此前冻结包保持。

## 两遍原始读数

以下所有字段 `valid=1, stable=1`，两遍值完全相同。源日志行 111–128；完整分析包含各字段独立行号。

| 字段 | 地址 | 两遍 raw | 文档字段解释 |
|---|---|---|---|
| CX_GDSCR | 0x008c4034 | 0x00222001 | PWR_ON=0、SW_COLLAPSE=1、HW_CONTROL=0、SW_OVERRIDE=0 |
| GX_GDSCR | 0x008c4024 | 0x00222001 | 同上，GX 报告 collapsed |
| GPU_AHB_CBCR | 0x008c403c | 0x80008000 | enable=0、off=1 |
| GPU_AON_ISENSE_CBCR | 0x008c4044 | 0x80000000 | enable=0、off=1 |
| GPU_GX_GFX3D_CBCR | 0x008c4028 | 0x80000220 | enable=0、off=1 |
| GPU_GX_RBBMTIMER_CBCR | 0x008c40b0 | 0x80000000 | enable=0、off=1 |
| GFX3D_CMD_RCGR | 0x008c4000 | 0x80000000 | 原始值保留；未读 CFG/PLL，不推算实际 GPU Hz |
| GCC_MMSS_BIMC_GFX_CBCR | 0x00309010 | 0x80004221 | enable=1、off=1；使能请求与反馈不能等同 |
| GCC_BIMC_GFX_CBCR | 0x00346018 | 0x00000001 | enable=1、off=0；不足以证明整条 GPU 总线可用 |

GDSCR 字段直接来自固定 MiCode `gdsc.c:28–35`，CBCR enable/off 来自 `clock-local2.c:67–68`。高位其他字段不猜测。G0 未读 GDSC 独立 HW_STATUS、clamp、GFX regulator、RPM vote/ack 或 PLL，因此这些读数不建立 rail 数字设定/模拟电压、供电 settled、所有权、实际频率和 GPU MMIO 安全访问证明。

三个映射以完整 **16KiB allocation** 建立并分别释放，日志保留对应 allocation/extent。快照结束行：

```text
status=snapshot_complete valid0=0x1ff valid1=0x1ff stable_mask=0x1ff
unstable_mask=0x0 reads=18 loads=18 read_failures=0 completed_passes=2
errors=0x0 elapsed_us=18 gpu_writes=0 power_verified=0 chip_id=UNREAD
```

## 温度、退出与日志

| CPU affinity | 本次最高原生读数 | 结束读数 |
|---|---:|---:|
| 0x0（UEFI resident） | 51.2°C | 48.6°C |
| 0x1（XNU 引导核） | 51.8°C | 46.1°C |
| 0x100 | 48.0°C | 45.1°C |
| 0x101 | 48.6°C | 45.1°C |

这是四路 CPU TSENS 原生值，非 GPU 温度或校准精度验收。本次没有持续满载，不据此选择新频率。

尾部包含正常 timer、四路 `phase=stop`、`return_cpu affinity=0x1`、source writer contention dropped bytes=0，以及 **`drain=1 target=57875`**。RAM 的 `DATA SYNC error=2 phase=lookup` 是没有真实 Data 路径，不是 UFS 失败。

独立逐字节重放原始 USB：**235 帧 / 65,444 原始字节 / 57,924 console 字节**，起始 offset 0，末 offset 57,924，缺口/重复/残帧/device-lost 均 0，与冻结 receiver 重解码完全一致，包含内核起点至正常 timer/drain。接收器在 03:55:40 记录 **capture_failed / Pipe error**，该进程状态原样保留；独立排空与返回证据不依赖把它改成 success。

03:56:56 在原窗口内只核验一次，得到 serial `54223ce9` / product `MSM8996_A8`，确认同一手机 Fastboot。没有请求或执行手动恢复，不再查询。固件内部 Prepare/Commit 文本未单独捕获，未目视手机终屏；USB 断开本身不作为自动返回证明。

## 独立复核与下一阶段

主代理执行真机 lifecycle、保存流分析和独立 USB 重放。Bacon 子代理仅从保存日志与固定源码核对原始字段、地址、位定义和解释；不操作设备。复核结果附在 evidence/core/runtime-g0-01；本报告的原始数值和位计算由主代理直接复核。

下一步是 **G1 供电与上电后 chip ID**，仍保持 614.4 MHz CPU 基线：

1. 从已固定的 stock CPR3 数据与本机 fuse 确认 GFX corner 2 / 133 MHz 所需数字配置；独立核对 GFX 的 PMI8994 S2 与 MX 的 PM8994 RPM smpa/2 corner 4，不能套 CPU S11。
2. 建立可观测的 regulator settled、RPM/bus acknowledgement、CX/GX/clamp 和时钟前置，再实现受控上电/逆序退场；在此前不访问 0x00b00000。
3. 上电前置满足后，单独批准的 G1 包才读取 `RBBM_CHIP_ID`；目标 0x05030004。再单独验证 G2 的 PAS13/内存池与 CP 安全切换。G0 通过不扩大成 G1/G2 写操作批准。

匹配本地 8 个固件已准备，但本次均未装载。软件 fb_threads=2 对照仍单独冻结未上机。

[逐字段分析](/F:/IOS-MIX1/evidence/gpu-g0-20261123/main/runtime-g0-01/analysis.json) · [USB 独立审计](/F:/IOS-MIX1/evidence/gpu-g0-20261123/main/runtime-g0-01/usb-audit.json) · [原始 console](/F:/IOS-MIX1/evidence/gpu-g0-20261123/package/runs/gpu-g0-01/console.log) · [G1/G2 来源准备](/F:/IOS-MIX1/evidence/gpu-g0-20261123/power-zap/G1-G2-readiness.md)
