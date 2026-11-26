# Lithium A530 executable minimum bootstrap

本目录现在实现真实全局 GPU setup、安全切换和 CP marker 的完整调用链；main 不需要另写一大段 `a5xx_hw_init` 或提供 `CP_BASELINE`/`NONSECURE` 恒真资格位。主调继续负责真实 G1/PAS 执行窗口；Pauli 负责 G3 BO、IOVA、cache 与 route/owner provider。

这是可编译、可调用的硬件实现，**G4 尚未在设备执行**。不把主机回调模拟、源码参数、编译成功当作 SMMU/CP/Metal 通过。main/用户已报告 PAS03 实际 INIT13/MEM/AUTH/SHUTDOWN 均返回0且 G1 恢复；该窗口已经关闭，不能替代未来 G4 同次 AUTH journal。工作仅写两个 g4-live 目录；没有修改 shared、既有 cp/g2/g3、上游或设备。

## 主调的实际入口

先在普通内核线程中准备 Pauli provider 与四个 BO；使用持久驻留、全零初始化的 `g4lx_native` 和 `g4l_context`。`config.enabled=0` 没有任何回调或硬件访问。

```c
struct g4l_ops ops;
/* Matching stock DT profile, NOT a hardware chip-ID measurement. */
cfg->chip_id = G4L_LITHIUM_STOCK_DT_PROFILE; /* 0x05030004 */
enum g4l_status rc = g4lx_bind(native, cfg->enabled, cfg->window_cookie,
                               pauli_provider, &ops);
if (rc == G4L_OK) rc = g4l_prepare(cp, cfg, &ops, g4l_stock_files);
/* Main now establishes the real same-boot G1/PAS13/G3 window. */
if (rc == G4L_OK) rc = g4l_bootstrap(cp, actual_pas_auth_journal);
/* After leaving the timed live window; this reads retained RAM only: */
g4lx_log(native, cp);
```

`g4l_bootstrap` 实际执行以下阶段，不回调要求 main 补全 GPU setup：

1. 校验 main 的真实 AUTH 调用原始 journal 与同次身份，再采集 **30 寄存器×双遍**初态。
2. 执行 A530 最低全局初始化、完整 18 项 CP 保护范围和 12 个地址模式；逐项保存旧值、写入尝试与读回。
3. 写入 PM4/PFP/ring 的真实 G3 IOVA、关闭 RPTR shadow、清 ME_HALT（Linux 原始的 ME_CNTL=0 操作），发送并等待 ME_INIT。
4. 发送 A530 `CP_EVENT_WRITE(STAT_EVENT)`，等待实际 RPTR/idle。
5. 发送 **`CP_SET_SECURE_MODE(0)`**，等待同一 ring 实际 RPTR/idle；不写 TRUST，不执行 PAS resume 或重认证。
6. 发送 `NOP(1 DWORD)` 与 `MEM_WRITE(addr64, marker32)`，等待 fresh marker、RPTR/WPTR、STATUS、STATUS3 和 CP/RBBM IRQ。

默认没有 boot hook。集成可将 `g4-live.c`、`g4-stop.c` 和 `g4-xnu.c` 正常链接进活动内核，或在实际 worker TU 中只 include 一次 `implementation.inc`；两种方式不要同时使用。当前真实 worker TU 加该 include 的检查通过，core 私有 helper 使用 `g4li_` 前缀，stop 使用 `g4ls_`。

## same-boot baseline 与真实采集

`g4l_config.boot_cookie` 必须来自 main 当前 boot 的实际身份；`window_cookie` 标识此轮 G1/PAS/G3 存活窗口，不能用镜像 hash、历史运行 cookie 代替。`address_space/generation` 必须匹配 Pauli 的实际映射。

`g4l_pas_auth` 直接从 main **已经执行**的 AUTH 调用填写：

| 字段 | 精确来源 |
| --- | --- |
| `boot_cookie/window_cookie` | 与当前 `config`、G1 与 Pauli provider 相同的运行身份 |
| `before_ns/after_ns` | 真实 AUTH 前后 `mach_absolute_time` 转换的单调纳秒，与 G4 `now_ns` 使用同一时间域 |
| `function_id/pas_id` | 实际已执行的 SCM64 `0x42000205` / PAS13 |
| `callback_status` | 实际 SMC transport callback 状态；本层仅接受 0 |
| `x[0..3]` | 原始返回寄存器完整保留，只把 x0=0 解释为该服务返回成功，x1..3 不推测意义 |

INFO 可用性查询、INIT/MEM、未执行的零初始化 end、未来 timestamp、错误 boot/window 均不能满足此检查。本结构是本地调用 journal，不是密码签名；main 的 LIVE guard 仍必须保证 AUTH 后未 shutdown、G1 未掉电、G3 未卸载。

随后 `g4l_capture_baseline` 在 **本 context 内部**即时执行原始读取，外部不能提交一张 `complete=True` 快照取代它。`g4l_baseline` 保留 boot/window/aspace/generation、开始/结束时间、AUTH 原值、每个寄存器双遍原值、valid 掩码及失败 callback 状态。以下 30 项的顺序也固定在生产 core，方便主调独立重放：

```text
RESET, STATUS, STATUS3, IRQ_STATUS, AHB_ERROR, CP_IRQ, CP_PROTECT_STATUS,
ME_CNTL, RB_RPTR, RB_WPTR, RB_BASE_LO/HI, RB_CNTL,
PM4_BASE_LO/HI, PFP_BASE_LO/HI, IB1_SIZE, IB2_SIZE, CONTEXT_SWITCH_CNTL,
VBIF_VERSION, VBIF_HALT0/1, GPMU_CM3_SYSRESET, GPMU_GENERAL0,
TSB_CTL, TSB_BASE_LO/HI, TSB_SIZE, TSB_ADDR_MODE
```

最低接受条件是双遍稳定、idle、无 stall/fault、零 ring/firmware/IB 根和零 context-switch 控制；VBIF_VERSION 必须是实际非零非全 1 值，不能以软件默认 0 伪造可访问性。STATUS 的 HI_BUSY(bit0) 与 IRQ 的 GPU_IDLE(bit0) 可变化，其余原值均保存并严格检查。其他 CTRL 未知位不猜掩码；ME_CNTL 只保存原字，不从旧型号 XML 猜 A530 HALT 位。

**GPMU 必须实际读到 SYSRESET=1**，来源是 stock 明确的 stop 操作（Linux 用 0 启动）。不满足就保持原始失败退出；本层不会先停调速器来制造 baseline。这个 profile 在现有 G1 真机上能否满足仍未验证。

零根/idle 快照仅用来识别本实现接受的冷启动形态，不是排尽 DMA 的硬件证明；独占设备/路由由真实 provider 保障。任何非零旧 ring 根都保留诊断并拒绝接管，不重置索引，不借用旧运行内存。baseline 不会被当作后续释放证明。

首次采集后的寄存器 setup 也会再读旧值，再执行所需写入并立即读回；已正确的普通配置会记录为 `attempted=0`，不做冗余写。ME_CNTL 的清 halt 操作仍显式写一次。每个阶段都有独立观测 journal；最终安全切换标志仅在真实安全 packet 发布并排空后由 core 设置。

## 最低全局初始化的覆盖范围

唯一参考为本地固定 Linux v6.12 commit `adc218676eef25575469234709c2d87185ca223a` 的 `a5xx_gpu.c/a5xx_catalog.c`，以及匹配 stock 源、Mesa25.0.7 XML；文件 SHA、逐组来源行号和常量交叉检查位于 evidence 的 `sources.json`。

实现了必要的：VBIF QoS=3；AHB error/split 控制；A530 hang masks/detection；UCHE cache ways、trap/write-through、1MiB GMEM 范围与 mode；A530 CP queue thresholds/retention；PC ECO 及 TWO_PASS_USE_WFI；hbb15 对应的 TPL1/RB 模式；VPC flat-shading workaround；18 项 CP protection；Linux 非安全 TSB 布局；12 项全局 64bit address modes；firmware/ring setup。

固定 catalog 的 A530 配置 profiles 是 **`0x05030002/0x05030004`**，quirks 为 TWO_PASS_USE_WFI 与 FAULT_DETECT_MASK。本层只接受这两个来源明确且共用该配置的 profile，context mask=3；不把 A540/A512 的 LMLOADKILL 移来。旧 stock A530v1 使用另外两份 firmware，不能用当前 a530_pm4/pfp 假充支持。

**`g4l_config.chip_id` 保留字段名，但含义是 DT/catalog 配置输入，不是硬件 ID 测量。Lithium 固定选择 `G4L_LITHIUM_STOCK_DT_PROFILE=0x05030004`。** 匹配本机 stock `lithium-stock.dts:11311` 的 `qcom,chipid=<0x5030004>` 与 MiCode `msm8996pro.dtsi:1021` 一致。固定 Linux `adreno_device.c:132–193` 从 DT compatible/旧 `qcom,chipid` 取值并查 catalog；stock `adreno.c:675–680` 同样读取该属性再 `_get_gpu_core`。这些来源未建立可读的 A530 完整 chip-ID 寄存器，因而本层不要求也不增加这种 MMIO 读取。VBIF_VERSION 仍是独立真实原值，不用它拼装型号。native 日志明确输出 `source=dt-catalog hardware_chip_id_measured=0`。精确 hash/来源行号见 `sources.json`。

以下是有意排除且不要求 main 在此 probe 前重实现的 Linux 子系统：devfreq/perf counter 设置、HWCG、GPMU firmware/LM/电压/调速、SP/TP power collapse、preemption 和 RPTR shadow。G1 保持已经建立的供电/时钟窗口，GPMU 实际保持 reset。stock 也在安全切换后才启动 LM/GPMU，因此将此 probe 限定在该启动前窗口。完整 Linux 驱动等价、GPU 渲染或 Metal 可用性仍不在主机验证结论内。

IRQ_MASK 使用 0 以保持轮询；不触及 GIC，不 W1C 清错后继续。只容许已记录的 GPU_IDLE 状态位，其余 RBBM/CP IRQ 非零保留失败。bootstrap 没有 TRUST 直接读写、软件 reset、频率/电压操作或硬件重试。独立显式 recovery 的 reset 白名单见下文，不会扩大 bootstrap 的可写寄存器集合。

## 唯一 PM4 来源与序列

所有 opcode 只定义在 `g4-pm4.h`，验证脚本逐个与固定 XML 比对，不混入旧 host CP encoder：

| 包 | 精确序列 |
| --- | --- |
| ME_INIT | type7 0x48、payload8；`2f,3,20000000,0,0,0b,0,0` |
| STAT_EVENT | type7 0x46、payload1；`16`（A530 用法额外由 Linux hw_init 明确支持） |
| SET_SECURE_MODE | type7 0x66、payload1；`0` |
| NOP | type7 0x10、payload1；`0` |
| MEM_WRITE | type7 0x3d、payload3；真实 fence IOVA low/high、marker32 |

无回绕的新 ring，四次发布的 WPTR 依次是 **9、11、13、19 DWORD**。实际 RPTR/WPTR 必须与此 context 严格一致，不能接纳外部偷偷推进的指针。secure_completed 只表示源支持的安全 packet 已实际排空且无 fault，不捏造不存在的 secure-status 寄存器测量；后续在 G3 映射中的 fresh marker 是附加执行证据。它仍不是 Linux CACHE_FLUSH_TS/IRQ 渲染 fence，也不证明 GPU 没有预取。

## Pauli G3 provider：精确合同

五个函数签名在 `g4-xnu.h`；v2 在 guard 枚举新增 STOP_WINDOW，其余签名不变。主调只需要绑定这些函数和真实 AUTH journal。core 不要求 provider 伪造 GPU setup 或安全切换结果：

| 回调 | 责任 |
| --- | --- |
| `guard(cookie,HOST_PREPARE,window)` | 普通线程、独占 CPU/BO、不可变化的对象、同次身份。原生层另外检查 logical0/MPIDR affinity1、非 IRQ、开中断、可抢占。 |
| `guard(cookie,LIVE_WINDOW,window)` | main 的实际 G1/PAS 生命周期与 Pauli 的实际 installed context/stream/ASID、mapping/TLB-sync/route 所有权持续成立。无 CP_BASELINE/NONSECURE。 |
| `guard(cookie,STOP_WINDOW,window)` | 同次仍上电、MMIO 时钟可用、CPU/设备独占恢复租约、所有提交入口已封锁、全部 BO 仍 pin。不能依赖 PAS/SMMU 健康；它们可能正是故障原因。这是操作租约检查，绝不是由外部恒真 bit 宣告 DMA 已停。stop 自行采集原始停止证据；release 前再查本租约。 |
| `acquire(cookie,role,min_bytes,4096,access,out)` | 原子 pin backing pages/CPU VA/真实 IOVA/aspace/generation/token；成功输出完整独占页尾。PM4 ≥19568B、PFP ≥15872B、ring ≥32768B、fence ≥4096B；前三项 GPU_READ，fence GPU_READ\|GPU_WRITE。 |
| `sync(cookie,bo,offset,bytes,direction)` | TO_DEVICE clean 到 PoC 并处理 GPU 写前 dirty lines；FROM_DEVICE 正确失效/完成 GPU 写回。native 的 dsb 不能替代 cache maintenance。只有独立证明 coherent/NC 时才允许 no-op。 |
| `release(cookie,role,bo)` | 本层 pin/ref 的不可失败 release；不可重入。若父 pool/table 为永久保留资源，不能借逻辑 release 物理释放。 |
| `dma_stopped(cookie,window)` | 其他恢复实现独立完成真实停止/排空并禁止新提交才返回1；未知0、错误负值。HALT、fence、RPTR==WPTR、超时、baseline 全不能代替该证据。v2 本 context 的完整 hard-stop 原始 journal 验证通过时，不需要把此回调伪造成1。 |

Provider 失败必须 out 全零且无隐藏 pin；带非零输出的失败、重叠/错误代际/token 重用的成功分配会进入 allocation_uncertain，本层连显式 release 都拒绝，留给 owner 审计。不能把 ABI presumed 地址、PA 或 CPU VA cast 当 IOVA。

若复用 G3 static pool，Pauli 应建立独占子分配，核对实际页表/PA、SMMU权限与 installed route；整个 pool、table、claim 同时保留。XNU16KiB页内不得提前释放其他4KiB BO，不给 WB managed 页另建 NC alias。G3 “clean 后无 CPU 写”的旧准备断言必须与本层 firmware/ring/fence 填写协调；所有后续 CPU 写应由真实 sync 重新发布。

## 超时、DMA 与诊断

初态采集、setup/ME、STAT、安全切换、marker 各有 deadline，单阶段最多20ms、poll最多4096；时钟不前进仍由迭代上限停止。每个实际 MMIO store 前检查预算，缓存同步/marker sample 后也复核 deadline；迟到 marker 不判通过。单次互连 MMIO load 本身若挂住，软件计时无法中断它，本层不声称总线 fault recovery。

任何 MMIO store 尝试前即设置 exposure；包括回调报错但硬件可能已接受。成功、超时、错读回、secure packet 未结束均保留 firmware/ring/fence/G3 映射及 G1/PAS 依赖。**不要在 exposure 后直接 PAS SHUTDOWN、unmap、掉电或释放页**。`g4l_release_after_stop` 本身不请求 stop/reset；只有独立 stopped-DMA 证据或本 context 的完整 hard-stop 原始 journal 验证通过才 release。

## 显式 hard-stop 与退出顺序

仅在 main 明确请求 CP 恢复时调用；下一轮只读 SMMU 库存不能触发它：

```c
struct g4l_stop_ops stop_ops;
enum g4l_status stopped = g4lx_bind_stop(native, &stop_ops); /* no MMIO */
if (stopped == G4L_OK) stopped = g4l_hard_stop(cp, &stop_ops, request_stop);
if (stopped == G4L_OK) stopped = g4l_release_after_stop(cp);
/* Main separately retires G3, shuts down PAS, restores G1, checks all
 * existing timer/drain/source-loss/Fastboot gates. Log original cp->error
 * separately from stopped: successful cleanup never changes a failed probe. */
```

`g4-stop.c` 组合固定 Linux `a5xx_recover` 的 SW_RESET 脉冲与 `a5xx_pm_suspend` 的四路 VBIF drain/A530 FIFO reset，并额外保持最终 halt。stock `_soft_reset` 明确说明 reset 全部逻辑、恢复默认寄存器并 flush pending VBIF transactions。**七次 store**，以下均为 GPU DWORD 索引：`43=1 → 43=0 → 3080=f → 3080=0 → 45=003c0000 → 45=0 → 3080=f`；第一次 halt 后轮询 `3081&f==f`，FIFO reset 后实读 ACK 清零，最后重新 halt 并取得 fresh ACK=f。reset 断言后的 dummy read 保留原值，不增加来源未承诺的“必须读回1”；撤销 reset 必须读回0。

完成后再采集19寄存器双遍，要求 DMA 根、ring 索引、IB size/context 为0，无 busy/stall，GPMU reset1，最终 HALT/ACK=f；ME_CNTL 只保留实际原值、不猜 HALT 位。随后 FROM_DEVICE 同步 fence 页并再查 deadline。总预算≤20ms、全部 ACK 轮询合计≤4096，任何实际尝试失败后不可重试。原 bootstrap error 与 stop error 分开保存，七写 before/after、三阶段 ACK、双遍 cold 原值均可日志回读。

成功 journal 可让本层 pin/ref 正常 release，不会因外部 `dma_stopped=0` 永远 hold。**真实父资源退出仍必须逐层完成**：封锁新提交并持有最终 halt → G4 stop 证据 → Pauli G3 retirement/实际寄存器重核/页表与 TLB/cache 完成 → main 同次 PAS SHUTDOWN → G1 恢复/掉电 → 现有返回门禁。不可把 G4 ref release 当作 G3 pool/table/claim 已清退；reset 对 SMMU 的实际影响也须重核，不能沿用 reset 前快照。具体流程见 evidence 的 `HARD-STOP-PROPOSAL.md`。

该组合有源码依据且已实现为真实 XNU MMIO，但仍未上机；硬件不能响应、ACK 不到、非零根/错误/超时均保持失败与资源，不承诺软件能打断一条已挂起的 MMIO，也不放宽 Fastboot 门禁。

## 数量与最小资源

旧 v1 的“11”是源/测试/文档文件数；v2 增加 stop.c/.h 后共13个。bootstrap 是68全局配置项（含18保护范围、12地址模式）+8 firmware/ring/ME setup 项+4 WPTR 发布槽=80个计划槽；已经正确的普通值跳过写，实际 store 数由 journal 给出。baseline 是30×2读；hard-stop 另有7写和19×2读，ACK poll 另计。这些不是“11个 GPU 寄存器”。

| BO | 固件头跳过后的有效数据/用途 | 4KiB 映射最小值 | 独立 XNU16KiB 分配 |
| --- | --- | --- | --- |
| PM4 | 19568B | 20480B | 32768B |
| PFP | 15872B | 16384B | 16384B |
| ring | 32768B | 32768B | 32768B |
| fence | 4096B | 4096B | 16384B |
| 合计 | | **72KiB** | **96KiB** |

另有4×16KiB Device MMIO 映射（64KiB VA），不算 DMA DRAM；PAS reservation 是 main 自有资源。G3 现有32MiB pool 与24×4KiB 页表 arena 是 provider 实现选择，不是 G4 intrinsic minimum。G3 如引用“11写”，其含义是9配置写加 TLBIASID/TLBSYNC，由 Pauli 管理，不能与 G4 数量混用。

`g4lx_log` 在实时窗口后打印 retained RAM 中的 baseline 双遍原值、partial validity、逐写 before/after、四阶段状态及 native 最后真实访问。native `io_executed` 区分未执行与真实值，即使 post-access guard 失败也保留最后值。未采集值不会当作0。

## 固件与验证

厂商 PM4/PFP 字节只存在原忽略区和已生成的 workspace header：`/home/jia/ios-mix1-build/hakios-lithium-ios16/local-inputs/gpu-cp/g4-pm4-pfp-local.inc`。本轮只复用该 header，不改 workspace。`make-workspace-firmware.py` 可在另行获准的准备流程中核对两份固定 stock size/SHA 并生成它；不把字节写 source/evidence，不下载固件。首 DWORD 按 Linux/stock 丢弃，不能当 magic/长度。

```sh
python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/g4-live/run-focused.py
```

该单元只运行少量生产 callback 边界场景，不重跑旧 CP/G2/G3 大型测试：ASan/UBSan；真实 AArch64 ELF object；真实活动 XNU flags/headers 的 core/native arm64 Mach-O objects；当前完整 worker TU 加 `implementation.inc` 与调用点的语法检查；真实 firmware header 仅 syntax-only。完整 worker 可能引用 vendor inputs，故不在 evidence 产生它的 object。

后续 Metal 真机目标继续由主代理整合渲染/用户态路径；本次明确完成其可执行的 CP bootstrap 前提。设备、PAS 与 G3 真实状态仍待 main 的同次硬件证据；本层没有通过 wrapper 或模拟结果代替这些结论。
