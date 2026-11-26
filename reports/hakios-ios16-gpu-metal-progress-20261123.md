# Lithium A530 Metal 路线推进记录 · 2026-11-23

CPU 沿用用户选定的 **614.4 MHz 配置模型**，显示沿用 R20。当前目标是 A530 内核驱动 → HakiosGPU 兼容 ABI → hakios-metal A5xx → HakiosMetalCA → CoreAnimation；Freedreno 用作 A5xx 的寄存器、布局和编译器参考。上游工作树保持只读。

## 本轮真机结果

`gpu-g1-access-01` 于 18:19:49 单次临时 RAM 启动，镜像 SHA256：

```text
402375d3e5762753d022735bd8d70a57371a6ef8087a9316f03cb4fbbf05e794
```

绑定 logical0 / affinity1，13 个字段双遍读取，26 项均有效且一致：

- GFX：APID52 / PPID317，写入归属 EE0（APPS），channel CONFIG=0 / STATUS=1。
- MX：APID127 / PPID117，写入归属 EE4。MX 必须另走 RPM，不能直接套用 GFX 的写法。
- CX/GX 与 GPU 分支时钟仍保持关闭。

该运行没有发送 PMIC 事务，也没有修改 GPU 供电或时钟，没有访问 GPU aperture/SMMU。正常 180 秒 timer、四路温度 stop、源日志丢失 0、USB drain=1。18:25:20 唯一 devices/product 返回核验找到同一 serial/product 的 Fastboot；USB 原始帧独立逐字节重放通过。接收器的 Pipe error 状态原样保留，不以断开代替返回证据。完整数据见 `hakios-ios16-gpu-g1-access-01.md/json`。

计数更新为 HakiOS 99 / iOS16 68；`record-hakios-gpu-access.py` 是仅处理保存文件的幂等记录脚本，不操作手机。

## 已落地的软件

| 模块 | 主要文件 | 已完成范围 | 尚缺的硬件环节 |
|---|---|---|---|
| ABI | `ports/hakios-lithium/gpu/abi/spec.md`、`hakios_gpu_abi.h` | 固定上游的 selector、结构和 IOKit transport 布局；实际 Darwin ABI 检查 | A530 userclient 的真实 BO/submit/fence |
| 常量/PM4 | `hakios-metal-a5xx/include/hakios-metal/a5xx.hpp`、`pm4_a5xx.hpp` | Mesa XML 生成常量、A5xx 包格式核对 | 真实 ring 执行 |
| copy/fill | `transfer_a5xx.cpp`、`fake_a5xx.cpp` | 使用 A5xx CP_MEM_TO_MEM / CP_MEM_WRITE；严格 fake 检查与 sanitizer/TSan 专项 | GPU 读写和逐字节回读 |
| 纹理布局 | `texture_a5xx.cpp` | 初始线性格式，对照真实 Mesa fdl5 的 host 布局 | GPU 描述符读取/采样 |
| 编译/计算 | `compiler.cpp`、`compute_a5xx.cpp` | 实际 A530 AIR→ir3 编译和 buffer-only PM4 编码 | shader 执行、缓存与 fence |
| AIR 桥 | `air_dispatch_a5xx.hpp/.cpp` | 实际 CompiledKernel 元数据、常量零 padding、强引用 BO 生命周期、失败回收 | 与真实 queue/context 的连接 |
| 绘制 | `render_a5xx.cpp` | restore/sysmem/颜色目标/viewport/scissor；不支持的 draw 明确拒绝 | 链接后的 A5xx VS/FS 状态及实际像素 |
| SMMU | `gpu/smmu/smmu.h/.c` | 4KiB LPAE、32MiB 全池生成、动态子区间、cache/TLB 回调及 fault 解码 | SID/context 归属、真实安装和 TLB 完成 |
| CP | `gpu/cp/cp.h/.c` | 固件头、ME_INIT、ring/fence、超时与 DMA 引用保留模型 | A530 MMIO backend 和真实 fence |
| Zap | `gpu/g2-zap.c`、`g2-zap-mu.c` | 匹配 stock MDT 的解析、PAS13 调用序列和双层返回码 | 合法内存保留及实际认证 |
| 电源/时钟 | `gpu/g1-power-*`、`g1-clock-core.c` | 窄 SPMI transport、真实权限采集、133MHz 固定序列 core | STATUS 来源绑定、恢复关闭、RPM ACK / bus、实际芯片识别 |

以上软件存在并有专项结果，不等于对应 G/M 硬件阶段通过。尤其没有真实 shader、draw 或界面 GPU 合成验收。

## 本轮并行执行记录

| 负责人 | 独立范围 | 本轮交付 / 状态 | 主代理复核 |
|---|---|---|---|
| Aristotle | GFX STATUS 与受控 enable/restore | 进行中；仅 `g1-status-*` 与独立证据 | 待接入；不绑定旧 synthetic STATUS |
| Tesla | RPM/GLINK 与 MX/bus | 进行中；独立 `gpu/rpm/` | 本轮实机已确认 MX owner4，禁止直接写 |
| Godel | CompiledKernel→A5xx AIR 桥 | 12 host 场景、13 真实 compileAir 集成场景、Darwin 对象通过；后续独立 program-state 模块 | 已读实现及产物，3 个 source hash 与交付一致；共享 compiler/schema 未被代理修改 |
| Chandrasekhar | SMMUv2 host core | 19 场景 / 12,495,517 checks，Darwin 对象与逐表独立遍历通过；后续 zap 内存保留模块 | 61 项交付 hash 逐项核对通过，硬件状态仍为 false |
| 主代理 | 集成、构建、打包、全部设备操作、退出与报告 | 完成 access01；AIR/render 加入 Darwin 静态库及探针实际链接 | 单次生命周期、全部26读、USB重放、返回终点、原索引分别复核 |

本轮未发生共享文件并写冲突。没有重跑全部历史回归，也未重建已冻结的 Mesa 编译器 archives；仅对修改模块执行专项检查和实际链接。

## 实际构建与复现

完整 HakiOS access 内核：`scripts/build-hakios-gpu-metal.py`，实际产物 `hakios-kernel.gpu-g1-precheck-03`，SHA256：

```text
e83ab14a8a234a967022be2b9e3f2df2808b82d432a7b976804f3e86ec9dc74c
```

RAM 包、真实嵌套 FV 的无物理 UFS 与 Fastboot DXE 精确检查复用已冻结的 G0 支持组件；未改旧包。

Darwin A5xx 库与探针：

```sh
make -f A5xx.mk probe -C ports/hakios-lithium/hakios-metal-a5xx
```

使用既有 LF-normalized iPhoneOS16.5 SDK 和 ld64.lld-18；仅 Darwin arm64，不是 arm64e。当前库包含 AIR bridge 和 render 编码器，产物记录在 `evidence/gpu-metal-20261123/main/darwin-products-current.json`。探针未装入手机或运行。

保存日志的分析与记录：

```sh
python -B scripts/analyze-hakios-gpu-access.py
python -B scripts/record-hakios-gpu-access.py
```

各专项的命令及结果：

- AIR：`evidence/gpu-metal-20261123/air-dispatch/README.md`、`verification.json`。
- SMMU：`evidence/gpu-metal-20261123/smmu/continuation-report.md`、`results.json`。
- 编译器：`scripts/build-hakios-a5xx-compiler.py`、`scripts/test-hakios-a5xx-compiler.py`。
- 纹理：`scripts/test-hakios-a5xx-texture.py`。
- ABI、CP、power、clock、zap：各模块的 `run_tests.py` / `*-run-host-tests.py` 与独立证据目录。

## 接下来按依赖推进

1. GFX 短暂 enable/原值 restore：只允许既有数字电压设定，不改电压、不写 MX、不打开 GPU consumer。精确 STATUS 来源不足时只报告寄存器和延时结果，不伪称 settled。
2. 完成 MX 的 RPM 投票、bus vote ACK 和真实时钟/GDSC backend，具备这些条件后才读 GPU 寄存器。
3. Zap 内存保留/PAS13 与 SMMU context 归属；有证据后连接 CP。
4. 通过真实 userclient fence，再运行 A5xx copy/fill、计算、绘制探针，最终接 CA。

需要人工恢复的异常会保留证据并停止该条设备链，主机上不依赖它的模块继续推进；正常返回的阶段沿用现有授权，不反复请求批准。614/R20/low01/旧冻结产物保持，原索引不变，无刷写、分区操作、ADB 或 QEMU。
