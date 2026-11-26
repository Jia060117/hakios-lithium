# A530 G1 供电前置包 — 2026-11-23

**G1 前置采集代码已实际构建并冻结。** 本包补齐 133 MHz 最低活跃 GPU 档所需的 GFX/MX 数字供电配置与本机 GFX fuse，不打开 GPU、不读取 chip ID。CPU 保持用户选定的 614.4 MHz 配置模型，复用已验 G0 的隔离 RAM、三核与 180 秒正常结束/自动返回路径。**本阶段尚未查询或启动手机，G1 上电验收未完成。**

## 为什么先采集这些数据

G0 的两遍实机读数已经确认 CX/GX GDSCR 均为 `0x00222001`，PWR_ON 未置位，SW_COLLAPSE 置位，四个 GPU 时钟分支均报 off。打开电源域前必须先确认两条供电路径：

| 路径 | 指定设备/接口 | 本包新增的实机输入 |
|---|---|---|
| GFX | PMI8994 S2，SPMI SID3 / peripheral 0x1700；stock CPR3 | TYPE/SUBTYPE、0x40/0x41 数字设定、MODE、ENABLE、STEP，两遍 |
| MX | PM8994 S2，SPMI SID1 / peripheral 0x1700；RPM smpa/2 corner4，S12 为两相配置 | 同上；该读数不能代替 RPM vote/ack 或共享资源所有权 |
| GFX CPR fuses | QFPROM base 0x74000 | rows39/41/63/64/66/68，完整 64-bit 双遍 |

固定 Linux/MiCode 源码核对确认两路 S2 均为 0x1700，**0x1400 是 S1**。CPR3 的初始电压字段在 row63，之前 CPU 调试只采了其余五行；新的六行一次补齐。

从历史有效五行可解得 MMSS CPR revision1/bin0/limitation0/aging raw1，但缺 row63，计算器保持 `modelready=false, computed=null`。本机 133 MHz 电压不能从 stock 520–670mV 区间任选，也不能借用 CPU S11=905mV。实际读数返回后，以固定来源和 stock DT 算出 corner2 的 open-loop 模型，独立比较 GFX 数字设定。

## 实际实现

- [g1-spmi-read.h](/F:/IOS-MIX1/ports/hakios-lithium/gpu/g1-spmi-read.h)：仅允许 SID3/1 的 S2、指定七字段；按实际 PPID0x317/0x117 搜索唯一映射，重新核对 version/route/config，100 µs、最多100 poll，不重试。
- [g1-fuse.h](/F:/IOS-MIX1/ports/hakios-lithium/gpu/g1-fuse.h)：六行完整位提取、sign/magnitude 算术、Pro/非Pro 来源模型；无 MMIO 或写回调。stock trim、aging 和 floor/ceiling 由主机计算器单独处理。
- [g1-precheck.inc](/F:/IOS-MIX1/ports/hakios-lithium/gpu/g1-precheck.inc)：普通线程绑定 logical0/affinity1，单次 latch，精确地址/命令白名单，完整 PAGE_SIZE allocation/extent 配对释放；保留 raw/valid/error/status/attempted/completed/polls 和局部结果。
- [boot-options.h](/F:/IOS-MIX1/ports/hakios-lithium/peripherals/boot-options.h)：默认关闭 `gpu_power_diag=1`，要求显式 `gpu_stage=0 gpu_probe=1`，拒绝 higher stage/触摸混合/重复或畸形参数；[kernel.inc](/F:/IOS-MIX1/ports/hakios-lithium/peripherals/kernel.inc) 在 G0 快照后单独调度前置采集。

SPMI observer CMD 的 MMIO 写入仅发送 **EXT_READL**；没有 PMIC register WRITE/WDATA、配置/中断/复位/所有权写操作。每路七字段各两遍，最多 **28 个 READ 命令**。外层全局 returning-access 预算250,000µs、9,000访问上限；超时停止新增访问并释放已获完整映射。时钟停滞、外部 abort 或永久总线挂死不能由软件预算救回。三个 CPU 的 PMIC/cpufreq 驱动当前禁用与单次 worker 仅减少 XNU 内部并发；路由复核不证明 TZ/固件独占。

七字段是固定来源的 common layout，数字电压需按实际 TYPE/SUBTYPE 解释。没有用猜测的 0x08 STATUS 位；controller STATUS 是 SPMI 传输完成证据，**不是 regulator 的模拟供电 settled 状态**。未采的状态不填 0 充当实测。本包不会尝试补写未知的 regulator 使能/电压。

六行 fuse 两遍总12个64位读取。row39 的 MMSS revision 位48–50 与先前 CPU revision 位51–53不同，不混用；有效的全零 row 仍作为数据，无效或不一致 row 拒绝模型就绪。记录为 `LITHIUM GPU fuse` 与 `LITHIUM GPU supply`。

## 构建、冻结与验证

实际完整 HakiOS / XNU8796.121.2 / arm64 内核已链接，未删功能，保留真实触摸、R40 UFS seal、默认行为与正常 cleanup。新内核 SHA256：

```text
7c63f434fcae955c07299cf135025fb6b349bab854df3e7d8acf2c974ca090e8
```

冻结 [G1 precheck 镜像](/F:/IOS-MIX1/evidence/gpu-g1-20261123/package/g1-precheck-02/lithium-hakios-ios16-gpu-g1-precheck-02.img)：**16,371,712 字节**，SHA256：

```text
24f53f36277df2ccdd4cb2bc5e4226ea5edda403435820ff55a7a586d5b4bc6a
```

G0 的 Mu/loader/FAT/Fastboot DXE、两个4MiB RAM APFS 卷和 DTBs 逐字节复用。实际 Android/SHA1/gzip/FAT/XNUP/APFS/BootShim 读回，最终压缩嵌套 FV 确认无物理 UFS provider、Fastboot DXE 完全一致；Windows runner `--check` 通过。唯一参数增量为 `gpu_power_diag=1`；新内核专用标记和 SHA 绑定，旧 G0 内核不能冒充前置包。

| 模块 | 子代理/主代理 | 验证与复核 |
|---|---|---|
| SPMI exact S2 read core、数字设定解码 | Pasteur | 实际核心 ASan/UBSan 41,419 场景 / 186,408 检查；主代理核对 SID/base、common layout、Late-DONE 分支、测试/构建源哈希 |
| fuse decoder、stock133 calculator | Raman | 实际 C ASan/UBSan 6,006 场景 / 73,234 检查，AArch64 freestanding；317计算器检查；主代理核对 row63 与固定 stock 查表/trim/aging，不使用假 row |
| 实际适配器专项与来源审查 | Bacon | 修正版实际 adapter ASan/UBSan **158场景 / 1,183,442检查**，覆盖4K/16K释放、READ命令、错误/超时/拒绝/一次执行；主代理核对测试与真实内核源hash/linked读写指令 |
| RAM package 与一次 runner | Kepler | 首版9个增量专项，修正版7个针对性检查通过，不重复 G0 的22项；主代理生成生产包并逐层读回/Windows准入 |
| 默认开关、共享接入、完整构建 | 主代理 | 13 个实际 parser ASan/UBSan正负用例，实际内核链接；linked precheck 局部帧384字节，数据静态保留 |

所有测试与模型均属主机验证。**未测 GFX/MX 原始值、fuse row63、133MHz GPU 运行、chip ID、CP/fence 或 GPU 加速。** 本阶段设备操作0，计数仍97/66；原索引及 R20/614.4桌面/low01/G0冻结保持。

首版测试实际暴露最后一次 RDATA 返回超过250ms仍被记录为成功的边界。主代理补上32-bit读、READCMD写与64-bit fuse读的访问后预算检查，保留实际attempted计数/错误，超预算数据不设valid；修正版158场景通过并绑定新02内核。旧01内核/镜像/结果保持归档，尚未上机；本次只申请02，不覆盖或启动旧包。软件无法打断永不返回的MMIO，mapping/logging/cleanup本身不声称都落在250ms内。

实际命令：

```text
scripts/build-hakios-gpu-g1.py --revision 02（WSL内14GiB/CPU1200%/JOBS12，完整HakiOS增量链接）
wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-gpu-g1.py --attempt 02
python -B reports/local/run-gpu-g1.py --check --attempt-id gpu-g1-precheck-02
```

## 一次实机采集与后续

获准后运行已冻存包，不重建、不混入fb2或触摸/GPU命令变量：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 gpu_power_diag=1 lithium_log_retry=1
```

`run-gpu-g1.py --boot --attempt-id gpu-g1-precheck-02` 执行五项预检、先开USB接收、单次临时 boot。接受后240–900秒只做一次 `--verify-return`，独立重放保存帧，核对全部 raw/valid/stability、命令数、错误、timer/drain/source0 与同机 Fastboot。正常约3分钟结束、摘要10秒后返回；不要求用户守屏。

真实输入返回后：

1. 对支持的实际 TYPE/SUBTYPE 解码 S2 数字设定/ENABLE，而非模拟电压或 settled 权限。
2. 六行实际64-bit双遍一致后，用固定 stock133计算器得本机 GFX corner2 模型；若 rail 数字设定与目标要求不一致，准备来源明确的下一步调整方案。
3. 继续补齐 RPM/bus vote/ack、GDSC/clamp/PLL与真正 regulator 状态；满足时才允许G1受控上电，读取0x05030004并逆序退场。不因 precheck通过就解除GPU aperture禁读。

回退采用配套文本补丁与 before快照，不含本地固件/Apple/Qualcomm/Xiaomi二进制。

设备操作前暂停源自用户 GPU计划§0：**“任何设备操作（boot/查询/重启/分区/刷写）都要事先得到用户批准”**。当前申请为已冻存的 **G1供电/fuse前置RAM采集一次**，GPU上电与G2仍另行说明、确认。

证据：[实际构建](/F:/IOS-MIX1/evidence/gpu-g1-20261123/main/kernel-result-g1-precheck-02.json) · [stock133模型](/F:/IOS-MIX1/evidence/gpu-g1-20261123/fuses/stock133-model.json) · [供电解码](/F:/IOS-MIX1/evidence/gpu-g1-20261123/spmi/decode_digital_setpoint.py) · [实际适配器复核](/F:/IOS-MIX1/evidence/gpu-g1-20261123/adapter/review-02.md)
