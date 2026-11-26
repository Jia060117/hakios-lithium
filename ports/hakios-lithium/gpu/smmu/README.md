# G3/G4 SMMUv2 stage1 4KiB host core

已实现离线、可复用 C11 页表与池分配 core；不依赖 A6 GPU、IOKit 或刚交付 ABI。`smmu.h` / `smmu.c` 可供主代理复核合并。本次 continuation 保留已通过的 core 与测试，补齐零回调全池表生成及逐表导出、预生成数据同步和真实对象检查。**host 验证不算 G3/G4 真机通过；没有硬件挂接或 A530 submit 验收。** SID0 是后续目标 stream，本包没有选择或绑定 SID/context。

## 页表与分配接口

- AArch64 SMMUv2 stage1、TTBR0 低地址区、4KiB granule。每表512个64bit descriptor；按 IOVA width 选择 L0/L1/L2 起始级，最终只使用 L3 page，没有 block、contiguous、DBM、NSTable/NS quirk 或 TTBR1。
- `gs_init()` 构造上级 table descriptor，叶子初始全 invalid。cb 可为 NULL，未绑定完整回调时禁止 publish。指定 32MiB IOVA aperture，可仅4KiB对齐；24页独立 pinned table arena 足以容纳最坏跨级边界的22页。不把数据池当页表池。
- `gs_build_pool()` 一次生成全部8192个leaf，保持 OFFLINE，零回调；预留全部物理页。`gs_table_view()` 导出每张已用4KiB表的只读CPU指针、PA、字节数；`gs_layout()` 补充 tables_used/table_bytes/mapped_pages。导出的表引用绑定传入 table_pa，**不能把二进制搬到任意PA直接使用**。
- 32MiB pinned contiguous 物理池，共8192页。`gs_map()` 指定 IOVA、物理 pool_page、页数；`gs_map_pool()` 映射全池；`gs_alloc()` 独立寻找连续 IOVA 与物理空洞。`gs_unmap()` 撤销后释放正确物理页位。物理页禁止别名映射。当前可供动态 GEM 使用固定池子区间，**不是任意外部 PA/scatter-gather 或完整 GEM userclient**。
- PA width 必须由调用者提供：32/36/40/42/44/48；IOVA width 25..48。检查数据池、表池、整个32MiB窗口边界及重叠，保留 PA[47:12]，拒绝截断、越界、零页数和不对齐。
- S1 descriptor：type3、AF=1、nG=1、XN[54:53]=3；READ必须显式给出，READONLY设AP[7]，非PRIVILEGED设AP[6]。GS_CACHE使用AttrIndx1/inner shareable；默认AttrIndx0/outer shareable。MAIR=`0xf404ff44`。只写权限不可用，因为S1 AP无法表示write-only；不映射MMIO或可执行页。
- `gs_lookup()` 是独立软件查询入口，返回PA+页内偏移及原始descriptor。`gs_layout()` 返回root PA、MAIR、start level/T0SZ/IPS信息，**不写TTBR/TCR/MAIR**。walk-cache属性和硬件寄存器配置仍由真实adapter确定。

## 主代理接入入口

下例中 domain 必须初始清零，domain/arena 由调用者在堆或静态存储分配。cfg 的 IOVA、pool PA、table PA、PA能力都由下一阶段提供；本包中的模型地址不作为本机分配依据。

```c
/* 普通内存写入；不需要提供假成功的 cache/TLB 回调。 */
enum gs_status rc = gs_build_pool(domain, arena, cfg, GS_READ | GS_WRITE);
if (rc != GS_OK) return rc;
struct gs_layout layout;
rc = gs_layout(domain, &layout);
if (rc != GS_OK) return rc;
for (unsigned t = 0; t < layout.tables_used; ++t) {
    struct gs_table_view view;
    rc = gs_table_view(domain, t, &view);
    if (rc != GS_OK) return rc;
    /* view.cpu / view.pa / view.bytes 可交给主代理记录或逐层读回。 */
}
/* 此处仍为 OFFLINE；没有硬件安装或 SID0/context ownership 证明。 */
```

取得 context/stream 归属、完整 pinned allocation、真实cache/TLB实现与DMA准入控制后，调用 `gs_bind_callbacks(domain, callbacks)` 复制完整回调，再按adapter的挂接顺序调用 `gs_publish()`。publish不安装root，任何真实安装只能在表已同步、DMA门禁和ownership均成立后由adapter控制。已全池映射的image不能再直接 `gs_alloc()`；G4动态子分配可使用原来的 `gs_init()` 空叶表路径，或在成功publish后先受控撤销相应区间。

`tablegen.c` 是可运行的host导出示例。测试runner生成ASan/UBSan版本 `evidence/gpu-metal-20261123/smmu/build/tablegen`，参数依次为 `IOVA_BASE POOL_PA TABLE_PA IOVA_BITS PA_BITS PERMISSIONS OUT_BIN`。所有地址和能力必须显式传入，输出是小端页表连续字节及stdout JSON。`11-tablegen-low` 和 `12-tablegen-cross-l0` 两个fixture只用合成地址；runner独立解析导出的二进制，检查每个leaf、表图及窗口外invalid槽。core不依赖此host CLI或stdio。

## 回调事务

所有调用由适配器序列化，domain/arena/pool必须长期 pinned。`gs_domain` 包含16KiB持久撤销journal，应放在堆或静态存储；core 无大栈数组、动态分配或内核库依赖。

```text
init      → OFFLINE（只构造普通内存）
build_pool→ OFFLINE（普通内存写入完整32MiB叶表，零回调）
bind      → OFFLINE（只复制回调，零回调）
publish   → begin → 预生成数据TO_DEVICE（若有）→ clean所有表
          → write barrier → invalidate整个context → tlb_sync → end
map       → 完整预检 → begin → data TO_DEVICE → 写leaf
          → clean变更L3表 → write barrier → invalidate整个context → tlb_sync → 占用物理页 → end
unmap     → 完整预检/保存物理页 → begin（适配器先停该范围DMA）→ leaf=0
          → clean变更L3表 → write barrier → invalidate整个context → tlb_sync
          → data TO_CPU → 释放物理页 → end
```

`invalidate_context` **必须覆盖完整已获准context及walk cache**，范围参数仅作提示；具体 ASID/CB/SID 由外部验证与绑定，core没有默认context0/SID0。`tlb_sync` 只有在真实完成并执行后续屏障后才能返回成功。`data_sync` 由适配器实现CPU/GPU数据维护，不能因descriptor选NC就假设CPU缓存无需维护。publish仅同步页表，不会安装硬件root。

begin失败或map的data TO_DEVICE失败发生在leaf写入前，可重试。预生成image首次publish的数据同步失败也可重试：保持OFFLINE，未调用table_clean/barrier/TLB回调，叶表和预留位图保持。map/unmap修改leaf之后，或publish进入页表同步之后，任何同步失败都进入 `GS_QUARANTINED`：不回滚到“看似成功”的旧状态，不释放物理页/表/arena，后续map/alloc/unmap返回POISONED。end报告未提交；调用者须继续持有**整个pool与arena**，直至另行取得domain停止/ownership恢复证明。无自动重置接口。UNMAP失败可能已清除leaf但仍有旧TLB，尤其不可复用物理页。

回调期间状态BUSY，阻止同步重入。无IRQ锁实现；主代理需给真实adapter加串行化。为保持pool内容可见，TO_DEVICE要处理CPU dirty lines；TO_CPU在quiesce/TLB完成后处理GPU写入。回调若只是主机mock，`GS_READY` 仅代表mock事务完成，不证明硬件ready。

## 来源与留白

固定官方 [Linux v6.12 commit](https://github.com/torvalds/linux/tree/adc218676eef25575469234709c2d87185ca223a)。下载的3份源码、完整hash与URL在 `evidence/gpu-metal-20261123/smmu/sources/linux/manifest.json`；tag签名没有在本机验证。具体行号在 `line-evidence.json`：

| 源码 | 行号 | 用途 |
|---|---|---|
| `io-pgtable-arm.c` | 67–130、433–528 | descriptor/MAIR、S1 AP/cache/XN、map屏障 |
| `io-pgtable-arm.c` | 270–283、633–689 | 非coherent表同步、walk/leaf失效顺序 |
| `io-pgtable-arm.c` | 906–914、973–1009 | 起始级、PA width/IPS与MAIR |
| `arm-smmu-qcom.c` | 179–229 | CB0是Linux分配器策略，非本机free证明；msm8996禁用TTBR1 |
| `arm-smmu-qcom.c` | 314–333、358–363 | firmware S2CR差异/保留context/TZ限制 |
| `arm-smmu.h` | 138–139 | CBFRSYNRA SID掩码，仅观测 |
| `arm-smmu.h` | 197–234 | FSR/FSYNR定义，补充fault decode所需头文件 |

`gs_fault_decode()` 只解码传入快照：TF/PF/AFF/EF/ASF/TLBMCF/TLBLKF/UUT、SS/MULTI、format、write/PNU/instruction/NS/PTWF/AFR、level/context、SID及未知FSR位。保留FSYNR1和FAR原值。**没有读寄存器、IRQ注册/ack/resume、TZ调用或ownership判断**。

IRQ路由、TZ/CB/SMR/S2CR ownership、SID真实连接、硬件PA能力、cache coherency、TLB完成与stall恢复全部未验。下一胶合步骤先取得这些前提，再将root/layout与回调接入受控adapter；本包不含MMIO适配器或写寄存器的路径。

## 验证

```powershell
python F:/IOS-MIX1/ports/hakios-lithium/gpu/smmu/run_tests.py
```

Linux对应用python3。测试提取pinned Linux descriptor及fault数值作为独立golden facts；C core链接C++ host fixture，ASan/UBSan禁止错误恢复。覆盖全8192页、所有PA宽度×每个IOVA宽度25..48的完整离线生成、高位table PA、L3/L2/L1/L0跨界、资源耗尽与拒绝时无leaf修改、cache/TLB失败隔离、动态分配撤销复用、非同号IOVA/PA释放与逐bit fault decode。另有真实Darwin arm64 C11/C++17 Mach-O目标编译、实际对象格式/符号解析及core 512字节栈帧上限检查；不运行Darwin/设备。

精确命令、结果、产物hash见 `evidence/gpu-metal-20261123/smmu/results.json` / `SHA256SUMS.txt`，接续报告见同目录 `continuation-report.md`。`before-continuation/` 保存中断前源码和报告，`continuation-baseline.json` 记录原35份artifact与报告hash全匹配。旧 `abi-unchanged.json` 属于前次验证，本次只核对SMMU构建输入与pinned来源，不重做通用ABI审计。写入只在两个smmu目录。
