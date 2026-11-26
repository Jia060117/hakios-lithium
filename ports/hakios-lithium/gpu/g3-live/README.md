# G3 live：默认只读发现与关闭的静态 stage1 草案

这是可供 main 集成的真实 XNU BSD worker 后端；本次仅主机编译/测试，没有接入共享入口、运行设备或重构内核。`LG3_ENABLE_STAGE1_WRITES` 默认 **0**。没有新造 Q 资格位，没有默认成功的 ownership/cache/TLB 回调。

## 最快接入：只读入口

1. 在 `peripherals/kernel.inc` 中、`g1-combined/adapter.inc` **之前** include `../gpu/g3-live/implementation.inc`。该文件包含 `live.c` 与 native adapter，所有私有函数均有 `lg3_`/`lg3x_` 前缀。
2. 将本目录 `table-core.c` 作为**独立 C TU** 加入正常内核构建；它原样包含现有 `gpu/smmu/smmu.c`。若 main 已链接原核心对象，则不重复添加。不要在混合 worker TU 内直接 include 原核心，避免其通用静态函数名冲突。
3. main 自己添加默认 false 的显式只读选项（建议 `gpu_smmu_diag`），本目录不触碰共享 parser。可在下一包中单独启用该选项，或将它放在同次 live 窗口的 PAS 只读查询之后；**PAS supported/auth/INIT 成功都不是 SMMU 只读门禁**。不要改动已经冻结/正在运行的包。
4. 精确调用位置：`g1-combined/combined-mmio.inc::lgcb_observe_vbif()` 中，实际 bus-post、CX/GX/clock readback 和 VBIF 成功之后、G1 依赖释放之前：

```c
/* main owns the new default-false parser field and this insertion. */
if (!rc && lithium_peripheral_requested.gpu_smmu_diag)
    (void)lg3x_after_g1_live(1u, 1u, lg2l_g1_live_guard, c, NULL);
```

这里的 `rc` 是 VBIF 访问结果，不是 PAS 查询结果。即使 PAS `IS_SUPPORTED13` 返回 x0=0/x1=0，只要同次真实 G1 guard 仍通过且外层预算尚有余量，就可以进入该只读函数。entry 及每次读前后都会重新检查现有 `lg2l_g1_live_guard`；源码不检查 PAS 状态、不发 SMC，也不新设资格位。G1供电/时钟/bus/VBIF成功不证明 TZ 已授予 context；只保留公开窗口/显式 CB 的原值与有效性，不因此取得写权限。

`cb_read_mask=1` 表示**仅观察源码描述的 user CB0**，不表示 CB0 空闲或归自己所有；不会跟随 S2CR 自动读取未知/受保护的 CB。可以先传0只看 IDs/global/SMR/S2CR，再依据真实结果另行选择明确可访问的 CB mask。`NULL` 写请求保证只读，且请保持编译写开关为0。

在普通 worker 的 G1 调用**返回之后**调用 `lg3x_log()` 输出保留在 RAM 的完整原值与 valid bits。日志函数不读设备，不在20ms live预算内 printf。所有 IO VA 在 entry 返回前释放；释放映射不更改 SMMU 路由。该只读路径不占用32MiB池、不分配页表、不发 SMC、不需要先通过 PAS，不将稳定路由误当成 ownership。

## 来源、寄存器范围和界限

- Linux v6.12 固定 `adc218676eef25575469234709c2d87185ca223a` 的 `msm8996.dtsi:2309–2324`：`0xb40000/0x10000`、GPU GDSC、GPU_AHB 与 MMSS_BIMC_GFX 依赖。stock MiCode `1492c8e...` 的 GPU DTS 给出同基址、更宽的 `0x20000`、user/secure SID0/2 和 user GPU-offset `0x48000`。本后端只映射两者交集的低64KiB，不读 stock 的额外64KiB，不碰 micro-MMU/secure实现寄存器。
- 先双读 `ID0=0x20,ID1=0x24,ID2=0x28,ID7=0x3c`。仅接受 SMMUv2 major2、4KiB寄存器页、`ID1.NUMPAGENDXB=2` 的保守64KiB profile；CB0 offset=`0x8000` 与 stock user-offset/GPU基址关系及 Linux header CB page寻址一致。不同几何只保留8次 ID 原读数并停止，绝不推测更多 aperture。
- 全局只读 `sCR0, sGFSR, sGFSYNR0..2`；全部硬件报告且不超过128组的 SMR/S2CR；只对调用者明确选择的、ID1范围内 CB 读 CBAR/CBA2R/CBFRSYNRA，以及 SCTLR/ACTLR/TCR2/TTBR0/TTBR1/TCR/CONTEXTIDR/MAIR0/1/FSR/FAR/FSYNR0/1。64位字段保留低/高两个32位原值。没有 ACK、清 FSR、RESUME、ATOS 或复位。
- 最多417个字段、834次实际只读（1024次硬上限），两遍都保留有效位；任何 unstable/error/超时即失败，未读0不作为实测。默认仅CB0时，读数个数由实际 NUMSMRG 决定，不能预先当作硬件已知常量。
- 每次 MMIO 前后都核对实际 G1 guard、logical0/affinity1、非IRQ、可抢占且中断开启；整个 discovery 20,000us。映射也在真实 guard 后建立。此预算只限制能返回的操作，不能保护永久总线挂死、同步 abort 或 TZ 拒绝。

## 高 PA、页大小和静态32MiB草案

CPU 的 `ID_AA64MMFR0_EL1.PARange` 在 native entry 读取并保存 raw/valid。ID2 IAS/OAS/UBS 来自同次快照，支持编码0..5（32/36/40/42/44/48）；不接受未知编码/52位扩展。实际本机数值本次**未测**。物理范围保守限制在 `min(CPU, IAS, OAS)`；32位 IOVA 还要求 UBS>=32、S1TS 和 PTFS_4K。不会截断4GiB以上 PA。

SMMU 页表固定 **4KiB**，与 XNU 的16KiB VM页独立。Qualcomm v2 明确禁用不可靠16KiB表支持，MSM8996禁用TTBR1。复用 `gs_build_pool()` 生成8192个只读/可写、XN、NormalNC leaf，配置低32位 TTBR0 IOVA窗口；不新增页表算法。GS domain保持 OFFLINE，避免将硬件设置冒充 GS 动态 map/publish 完成；此版只允许不可变静态表，不接G4动态分配。

`lg3x_prepare_static(enabled,iova_base)` 是独立、一次性、默认不调用的 CPU-only 准备：普通线程、G1 **之前**分配32MiB连续 wired 数据池和独立96KiB连续表 arena，均留在 VM 正常 WB 映射，不重设 managed 页属性、不加 NC 别名。调用真实 `FlushPoC_DcacheRegion(va,size)` 的 generic ARM64 `DC CIVAC`→PoC，并 `dsb sy/isb`；避免可能只剩DSB的 coherent-IO physical wrapper及其 legacy32位 outer-cache PA 参数。`APPLE_ARM64_ARCH_FAMILY` 配置显式拒绝编译；本机实际flags编译通过。准备后不准再有 CPU 数据/页表写入。G1内重新清理96KiB页表，TCR walk使用 NC/outer-shareable；不从ID0.CTTW或数据NC属性推断硬件coherent。

stage1 写草案需要同时：重新编译显式开启 `LG3_ENABLE_STAGE1_WRITES=1`、非空 `lg3x_request.enabled`、真实 pool准备、同一次新发现、**明确 SMR index/CB index/SID/ASID**，以及平台实际实现的独占 routing/ASID claim 和 DMA quiesce 回调。**本包没有提供该 ownership provider。** SCTLR=0、FSR=0、稳定 SMR 只是检查，不是 free/ownership证明。

仅接受已存在的 exact-match、TRANS→所选CB路由，且没有其他有效组共享该CB或匹配同SID；不分配空SMR，不将 BYPASS/FAULT 当空闲。所选CB须非 stage2-only、已明确允许读取。stage1程序完全不写 SMR/S2CR/global sCR0/ACTLR，不复制 firmware 探测写。

草案顺序：真实 claim/quiesce → 重新核对 route/CBAR/CBA2R/SCTLR → 清表/屏障 → CBAR、CBA2R(VA64)、TCR2、TCR(EPD1/4K/NC-walk)、TTBR0完整64位与显式ASID、TTBR1=0、MAIR0/1，逐项读回 → TLBIASID → Qualcomm TLBSYNC写`0xffffffff` → 至多256次 ACTIVE 只读轮询 → 最后写 SCTLR.M 并读回。fault policy为HUPCF+CFRE、无stall、无CFIE，未注册IRQ。整个 programming另限20ms且仍受外层G1总预算约束。

任何尝试 MMIO store 之前即记录 `hold_dependencies=1`；成功也保留，失败不自动回滚/重置/释放/重试。今后 main 若启用此草案，必须把 `lg3x_must_hold_dependencies()` 接到现有 G1 dependency-release 与 cleanup 两处门禁；本版只读接入不需改门禁。`installed` 只表示寄存器读回与TLBSYNC完成，不等于 DMA/CP 运行、页表硬件walk或用户态GPU通过。

## 具体未解决项

1. 真机 ID/SMR/S2CR/CB快照尚无；运行时几何若不属于上述profile，只能根据实际ID和新的固定来源扩充，不能扩大读范围碰碰运气。
2. 没有同boot的真实非安全context/stream/ASID归属交接和 DMA quiesce实现。读取只能揭示当前路由，不可能独自证明软件/firmware永久不再写。因此下一次可接入的是只读发现，写开关必须继续为0。
3. 本地pin包含 `arm-smmu.h`、QCOM实现和io-pgtable，但未包含主 `arm-smmu.c`。寄存器数值/TCR编码/TLBSYNC/HUPCF有本地来源；完整通用context安装/厂商handoff流程仍缺这份对应pin源码及实际租约实现。不能把编译通过当作完整写序列上机许可。
4. IRQ、translation probe、fault恢复、GPU命令提交、关闭/归还domain和G4数据回读均未实现/未验。不新增这些硬件动作。

## 主机验证

`evidence/gpu-metal-20261126/g3-live/run-tests.py`：两种编译开关的 ASan/UBSan 小专项，覆盖只读、错误/预算、几何拒绝、非零SID/CB选择、高PA完整8192页、重叠路由拒绝、真实claim缺失、部分写/忽略写/TLBSYNC失败与保留依赖。另用实际BSD `.CFLAGS`编译完整当前 `hakios.input.c` + 本后端两种模式，单独以实际osfmk flags编译复用表核心。全部只生成对象、不链接/执行内核、不操作设备。结果、源码SHA与固定来源行号在同名evidence目录。

## 集成冻结

本交接冻结 `lg3x_after_g1_live(unsigned,uint32_t,int (*)(void *),void *,const struct lg3x_request *)` 与 `lg3x_log(void)`；首次只读传 `NULL` request，保持编译写开关0。实际 core/adapter/独立表核心字节见 evidence 的 `frozen-sources.json`。后续接入由 main 修改共享调用/构建列表；本子任务不再改动被冻结实现。
