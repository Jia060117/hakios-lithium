# Lithium A530 G4 runtime

本目录是可执行的单 ring runtime core 和真实 XNU 适配 TU。复用相邻 `g4-live` 的 bootstrap、原四个 BO、MMIO/cache 回调、PM4 header encoder 和 hard-stop；没有另一套上电、固件装载、SMMU 初始化或 reset 实现。仅主机编译/测试完成，尚未在 CP/Metal 真机上验证。surface 不在本轮范围内。

## 文件与编译入口

| 文件 | 用途 |
| --- | --- |
| `g4-runtime.h/.c` | 无隐藏分配的生产 C core；adopt/submit/poll/last/done/stop/detach |
| `g4-runtime-xnu.hpp/.cpp` | 真实 IOKit 头文件、IOMalloc/IOFree、owner retain 和同步 executor；精确 `backend.g4` 签名 |
| `integration-check.cpp` | 可编译的 composite 转发模式；不注册服务，不自动安装 |
| `g4-runtime-test.c` | 链接生产 core 与原 `g4-stop.c` 的 focused tests；只在主机执行 |
| `run-focused.py` | ASan/UBSan、freestanding ARM64、实际 XNU C/IOKit C++ flags 编译、符号检查与来源台账 |

在 `F:\IOS-MIX1` 执行：

```powershell
wsl.exe -d Ubuntu -- python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/g4-runtime/run-focused.py
```

仅向 `evidence/gpu-metal-20261126/g4-runtime` 输出。读取已准备好的 XNU 构建头文件和实际 flags，不运行 make、不链接完整 kernel、不读取固件、不接触设备、不重跑 ABI/原 bootstrap 测试。

## 提交与完成

`g4r_adopt` 要求同一真实 `G4L_COMPLETE` context，包括实际 ME/STAT/secure/marker/baseline journal、四 BO 和相同 boot/window/address_space/generation。provider 额外持有控制 BO 和父依赖；原 bootstrap 引用也必须继续保持。adopt 实际读回 ring/firmware roots、NO_UPDATE RB_CNTL、IRQ mask 和相同的空闲 RPTR/WPTR，随后才初始化专用 fence 槽。

每次提交发出：

```text
每个 IB: CP_INDIRECT_BUFFER_PFE(addr64, words)
CP_SET_RENDER_MODE(0,0,0,0,0)
EVENT_WRITE(PC_CCU_FLUSH_COLOR_TS, fenceBO+128, value=0)
EVENT_WRITE(PC_CCU_FLUSH_DEPTH_TS, fenceBO+256, value=0)
CP_WAIT_FOR_IDLE
EVENT_WRITE(CACHE_FLUSH_TS, fenceBO+512, value=issuedFence)
```

尾部 22 DWORD，单 IB 总 26 DWORD。`TIMESTAMP[30]=0` 选择包中的 fence 值，`IRQ[31]=0` 使用 polling。来源为已固定 Linux v6.12 `a5xx_submit` 和 Mesa 25.0.7 A5xx/XML；`sources.json` 保存准确行号、hash 和组合边界。CCU color/depth 加 WFI、最终 UCHE event 是此 runtime 的保守组合，不声称整段序列已获真机资格。

ring 为现有 32 KiB / 8192 DWORD，使用绝对读写计数，保留至少一槽，按真正 RPTR 增量回收容量。回绕时用两个原 ring cache sync 范围发布，不额外插入占位包。容量不足和 pending journal 满在 `pin_job`/ring 写入之前返回 Busy。每个 accepted job 只有一次 WPTR store 尝试；回读不符、cache 错误、deadline/guard/fault 失败均停止新提交并保留可能暴露的资源，不重发该 store。

`last` 只报告实际成功发布的非零序号；`done`/`poll` 经 FROM_DEVICE cache sync 和现有 native acquire sample 读取 GPU 写入的累计 fence，前后检查硬件错误。只有这个样本可以推进 `completed` 和释放对应 inflight 逻辑 pin。分配序号、CPU bookkeeping、RPTR/WPTR 相等、超时、HALT 或停止状态均不能表示成功退休。私有控制页初始化后禁止任何 CPU writer；本 core 不宣称能够辨识另一个内核组件恶意改写同一物理 fence 页。最终 GPU cache flush 也不能代替目标 data BO 的 FROM_DEVICE cache handoff；后者仍由 G3/CPU_PREP 执行。

序号范围 `1..0x7fffffff`，到上限拒绝新提交，必须停止并以新 service/generation 重建。fence 0 只表示本 generation 无工作。每 IB 最多 `0xfffff` DWORD 是本实现的保守准入政策，不宣称已探测的硬件上限。

## 主代理实际 owner 的五个 provider 回调

`g4r_provider` 不是恒 false 的硬件替身；安装真实回调后 core 即按上述路径执行。未提供回调直接拒绝 adopt。主代理持有实际 G1/G2/G3 owner，并完成：

1. `guard(identity)`：检查仍在同一 boot/window/aspace/generation、独占 producer、真正 G1/PAS/SMMU lease 和 owner 执行环境。不能仅检查历史 PAS 报告或 CPU 布尔字段。
2. `claim(bootstrap, identity)`：原子取得 PM4/PFP/ring/fence **额外逻辑 pin** 与父 power/PAS/root/stream lease；禁止旧 bootstrap API 或其他 producer 再发布。失败不能留下隐藏 pin。
3. `pin_job(identity,bos,ibs,&token)`：用真实 map journal 验证每个 token、完整 extent、IOVA、权限、descriptor、generation/address_space，包含 nested fetch 和 resident references；固定 backing pages/CPU map/PTE，冻结或验证 command 的不变性，并确认实际 cache handoff 仍有效。不得根据用户 presumed IOVA、PA cast 或固定池猜测。返回唯一非零 job token，失败必须零 token/无 pin，或返回可恢复的 token 表示不确定。
4. `release_job(..., RENDER_RETIRED|STOP_ABORTED)`：只撤销该 job 的 inflight 逻辑引用，不能隐式 unmap、complete 或销毁控制 BO。不得失败/重入。`STOP_ABORTED` 不等于完成。
5. `drop_control`：独立 stop proof 成立且 inflight 清理后，撤销 runtime 的额外控制引用；原 bootstrap 引用另行释放。

普通用户不能得到控制 BO 的别名。core 对 BO/IB 做边界和控制 IOVA 重叠检查，但不是完整不可信 A5xx 指令沙箱。provider/现有 authorize 必须保证 IB 可信或已验证，禁止改 ring/固件 roots/保护模式或写退休地址。CPU 页固定不等于自动撤销用户可写映射；实际 owner 必须落实它承诺的不可变性，不能将 metadata 副本当成执行页冻结。

## 与真实 G3 dynamic 的接法

沿用主代理的 descriptor→真实 `HakiosA530Mapping` registry。map 前预留 registry 空间，记录 Map/MapUncertain 的实际输出；runtime job token 是独立 inflight journal id，不能复用 GEM handle 当 pin 证明。原 G3 lease 负责真实 PTE/TLB 和 descriptor lifetime；core 不接触 PA、不生成 IOVA。

在进入 G3 内部 mutex **之前**，在同一 executor/producer gate 内取得 runtime 退休或 stop proof，并记录为 G3 `lease.retired(token)` 可消费的证据。`lease.retired` 不能递归调用 `done`/G3 Sync。runtime 的控制 fence FROM_DEVICE 采用 bootstrap 原 `ops.sync` 的私有控制 BO cache 路径；它不能先要求自己正在读取的 fence 已退休，否则死锁。

原生 IOKit 已在每次提交前做 data BO TO_GPU；provider 仍要核实同一 lease 下没有重新暴露/写入。直接使用 C core 的调用方必须在 `pin_job` 完成真实 data handoff。退休后，G3 unmap/cache 仍须在 gated producer 下执行真实操作；`release_job` 只释放 job 引用，不代替 G3 行为。

## XNU/backend 集成

独立编译 `g4-runtime.c`、`g4-runtime-xnu.cpp`，链接现有 `g4-live.c/g4-stop.c/g4-xnu.c` **各一份**。已有 `implementation.inc` 的 monolithic worker 不得再重复编译同一份 bootstrap/stop。不要安装测试文件或 `integration-check.cpp` 中的示例 owner。

main 持久 owner 调用 `HakiosG4RuntimeCreate(executor,&runtime)`，在其真正 bootstrap 成功后调用 `HakiosG4RuntimeAdopt(runtime,&bootstrap,&provider,1)`，再取得 `HakiosG4RuntimeEndpoint`。composite 的四个 `backend.g4` 转发到 endpoint 的独立 context；保留原 backend.context 用于 G3/authorize/parameter。ready 先检查 G1/G2/G3 的当前 lease，再调用 endpoint.ready；后者会实际 poll/检查，而非仅看 COMPLETE。

executor 必须同步完成 job，所有返回路径都不能留下栈参数在队列中。若 composite 已在 owner 线程，`runSync` 应 inline 执行，不能等待自身队列或重复锁自己。不要从 IOKit service 锁内反向调用同一 service；release/guard 回调同样不得重入。使用现有 `g4lx_bind` 的 owner-thread/logical0/native MMIO cache 约束，无需新映射 GPU aperture。

现有 `iokit-a530/native.cpp` 将任何 backend.submit 错误保守记为 uncertain/faulted，包含干净 Busy。本 core 保留 Busy 的“尚未 pin/发布”语义，但不能据此假定现有 native service 会继续接受请求。主代理若需要平滑背压，应在其串行 owner 桥上、仍未暴露的新 job 上做有界容量等待；不要把 Busy/超时转成假 fence 或假成功。未做背压时按既有 service 故障/停机路径处理即可。

既有 native GEM cache 类型、CPU_PREP 的已交接 prerequisites 继续适用，见相邻 iokit-a530 的 `next/MINIMUM-INTEGRATION.md`，本轮未修改它们，也未重新审计 ABI。

## 生命周期与 stop

正常 render retirement 可释放每 job 的 inflight 引用，但不能释放仍可能被 CP 预取的四个控制 BO。任何 post-touch 失败都保留依赖。provider 若返回重复 token，core 记录 `pin_uncertain/uncertain_token`，停止后也不猜测重复引用数；所有 pin 保留供实际 owner 对账。

显式停机顺序：

1. gate 新 opens/submit 和所有其他 producer；保留 service/context、G1/G2/G3 和全部 BO。
2. `HakiosG4RuntimeStop(runtime,&existingStopOps,1)`，内部只组合 `g4r_begin_stop -> g4l_hard_stop -> g4r_finish_stop`。也可由 C owner 分三步调用；stop proof 不成立就保持 pin，绝不自动重试 reset。
3. STOPPED 阶段 `dmaStopped(generation)==1` 才允许 native reap/G3 处理未完成 job 的实际 unmap；它们记为 aborted，`completed` 不增加。先将 native clients/BO reap 完整结束。
4. 然后 `HakiosG4RuntimeDetach` 撤额外控制引用；`g4l_release_after_stop` 释放原四 BO。**Detach 以后 endpoint.dmaStopped 不再提供证明**，因此不能把第 4 步提前到 native reap 之前。
5. 所有 endpoint/executor 用户已 drain 后 `HakiosG4RuntimeDestroy` 释放 CPU context 和 retained owner。backend/service 自己持有的 owner 引用照常管理。adopted G3 root/PAS/G1 的后续 teardown 仍属于实际 owner；此处不虚构现有 dynamic API 尚无的 root destroy。

Create 会 retain owner；若 owner 自身持有 runtime，必须在显式 teardown 时 Destroy 打破这个引用环，不能只等 owner destructor。新 generation 不能覆盖或清零旧仍保留的 context。

## 证据边界

最新 `evidence/gpu-metal-20261126/g4-runtime/results.json` 保存实际命令、cwd、返回码、source/dependency hash、对象 hash 与测试计数；`sources.json` 保存 PM4/cache 来源。host fixture 明确模拟“已有成功 bootstrap”，只测试新增 runtime 和与原 stop 的组合，不把该 fixture 当成真机资格。CPU/device 内存分离的测试模型只编进主机测试二进制；XNU TU 中没有这条路径。
