# Dynamic G3 — native IOKit descriptor → exclusive IOVA

交付范围：真实散页映射、IOVA/物理页排他、数据/页表cache维护、ASID失效和Qualcomm TLBSYNC。没有boot hook、设备操作、共享构建修改或默认成功claim。冻结的live.c/live.h/xnu-adapter.inc/implementation.inc保持原字节。

## 编译与IOKit胶合

将 `dynamic-core.c` 和 `dynamic-native.cpp` 分别作为**独立C / IOKit C++ TU**；编译搜索路径包含 `ports/hakios-lithium` 根。本目录无需 include 到大 worker TU。core私有命名空间复用实际 `gpu/smmu/smmu.c` 的walk/PTE编码；不导出重复`gs_*`符号，不宏替换`callbacks.begin/end`，不修改原核心。

`dynamic-native.hpp` 提供与 `gpu/iokit-a530/backend.hpp` **精确同型**的三个入口，实际TU有static_assert：

```cpp
backend.g3.map   = HakiosG3DynamicMap;
backend.g3.unmap = HakiosG3DynamicUnmap;
backend.g3.sync  = HakiosG3DynamicSync;
// 上述形式要求backend.context就是HakiosG3Dynamic*。
// G3/G4组合context可由main转发其g3成员；不覆盖G4需要的context。
```

仅这些入口不能发布一个完整ready的IOKit设备：不要据此设置CommandSubmission/HardwareCompletion或RequiredCapabilities。G4 readiness、authorize与实际CP状态仍由相同owner实现。

## 真实生命周期

1. 普通可抢占线程中 `HakiosG3DynamicCreate(config,&ctx)`。config必须明确给出generation、32MiB低32位IOVA aperture、PA宽度、SMR/CB/SID/ASID，不能默认用SID0/CB0。此调用**不读SMMU**；只分配实际96KiB连续表arena、验证其真实PA连续性、构造空leaf并DC CIVAC至PoC。没有32MiB连续数据池。
2. `HakiosG3DynamicLayout(ctx,&layout)`给main实际root PA/MAIR/TCR参数。main在同次inventory后，以真实driver claim、供电/提交门禁和DMA状态决定是否安装该**空root**。此交付不替main执行未确定的context/stream写入。
3. main完成实际空root安装并保留依赖后，`HakiosG3DynamicAdopt(ctx,&lease)`。它先进入实际lease，才映射0xb40000低64KiB，核对ID/精确SMR→CB、其他组无SID重叠/共享CB、CBAR/CBA2R、SCTLR/TCR/TCR2、完整64位TTBR0+ASID、TTBR1=0、MAIR和FSR；随后实际clean表、TLBIASID、TLBSYNC、poll。这个读回核对不是ownership凭证。
4. map/unmap/sync均在native IOLock及相同owner lease内串行；不允许IRQ/关中断/不可抢占/G1有界窗口内运行。Adopt后需要**持续持有**电源/时钟/route/table，不可沿用只读G1返回即掉电的生命周期。

PAS supported/INIT/auth结果不是只读SMMU资格。动态写context与CP的生命周期由main实际管理，不能把先前AUTH后已SHUTDOWN的journal当成仍认证/仍供电。

## map、cache、TLB和页的保留

- glue按现有契约先prepare并持有descriptor。native再明确retain一次，**不偷偷再prepare**；失败暴露后或成功映射后，直到成功unmap才release这一次retain。glue只在unmap成功后complete/release其prepare。prepared descriptor的后备存储在这期间不得reinit/redirect。
- 使用实际64位 `getPhysicalSegment(offset,&length,kIOMemoryMapperNone)`；不取系统mapper的DMA地址，不cast PA为IOVA。只接受精确长度、16KiB页对齐descriptor/段，逐真实段展开为4KiB PTE，拒绝零/短/不对齐段和PA宽度溢出。支持不连续16KiB页、完整>4GiB PA。
- 独占32MiB IOVA first-fit，最多128个活动mapping，64位单调token+immutable generation。物理页hash同时拒绝同descriptor内部重复页及其他live/uncertain mapping别名，拒绝覆盖table arena。没有固定物理池别名。
- 首次PTE store前失败：out全零、撤销软件预留。首个store后任意clean/barrier/TLBI/TLBSYNC失败：out=`MapUncertain`+恢复token，整个domain quarantine，IOVA/PA/descriptor/table/lease全保留；不重试、不回滚、不复用。
- TO_GPU在真实managed DRAM现有physmap VA上调用generic ARM64 `FlushPoC_DcacheRegion`（DC CIVAC→PoC），同时处理CPU dirty与GPU写入前缓存。TO_CPU只在真实退休/独立停止证明后对整4KiB页执行`dc ivac`+DSB，避免将stale CPU dirty写回GPU结果。cache line取CTR_EL0；不把barrier当cache同步。
- 不重设managed页cache属性，不创建新NC别名，不宣称所有既有CPU映射的cache属性一致。SMMU leaf和table walk为NormalNC；既有descriptor WC wire flag不作为coherency证明。可信客户端必须遵守CPU/GPU所有权交接，不能在GPU拥有期间并发写共享页。
- 每批PTE修改后真实clean dirty tables→DSB→所选ASID完整失效（含walk caches）→QCOM TLBSYNC写`0xffffffff`→最多256次ACTIVE轮询/20ms deadline→DSB。只写CB的0x610/0x7f0；不写SMR/S2CR/SCTLR/FAULT/RESUME。
- unmap要求lease确认该token从未暴露于唯一producer、真实fence退休，或独立DMA/prefetch已停且禁止新提交。清PTE/发布/TLB完成/TO_CPU维护全部成功后才释放IOVA/PA/native descriptor retain。任一失败全部保留。

## lease如何实际实现；不是新造恒真资格位

`enter/valid/leave/retired`是main实际driver owner的窄胶合，不是认证receipt、外部token服务或硬件Q位。本模块已实现所有物理段/PTE/cache/TLB操作；lease仅连接**实际已存在的资源生命周期和唯一G4 producer**，没有默认恒真实现：

- `enter`：取同一driver registry/domain锁，禁止其他producer进入，核对当前generation/root/route及真实保留的G1依赖；操作期间不释放。
- `valid`：检查这把锁下的实际driver资源仍归该domain、未掉电/被替换；不能独立返回常量1冒充firmware handoff。
- `retired`：查询同一G4提交journal与实际硬件fence/独立停止结果。仅在所有producer由该owner串行控制时，“该token从未提交”才是有效本地事实。没有fence、timeout、HALT、!ready或RPTR==WPTR都不能单独证明停止。
- `leave`：Adopt之后始终retainDependencies=1，包括失败。没有domain teardown/reset恢复实现；`DestroyOffline`仅允许从未Adopt的空root。运行中/失败/在飞页不被释放。

同次inventory和平台归属判断尚未完成时，main不调用Adopt即可；Create/host编译通过不意味着已获claim。具体最小handoff路线及本地来源见evidence `dynamic-ownership-next.md`。

## 验证

evidence的 `dynamic-tests/run.py --ports` 测试最终ports字节：ASan/UBSan、32MiB完整8192页散页/高PA walk、与原gs空表/全部连续leaf逐字节等价、碎片first-fit、重复PA、只读权限、失效generation、预/后store失败、quarantine和不复用、128个token限额、重复分配和回调重入。真实XNU8796现有osfmk C / IOKit C+CXX flags生成独立Mach-O arm64对象。完整命令、源/对象哈希和冻结只读文件比对在交付manifest；没有内核构建/链接或硬件执行。
