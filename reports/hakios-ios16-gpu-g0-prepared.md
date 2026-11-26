# A530 G0 主机准备与下一步 — 2026-11-23

CPU 按用户选择固定为 **614.4 MHz 配置模型**，结束频率搜索，GPU 路线为 **A530 内核后端 → Freedreno Gallium/GLES**，先争取填充/blit，再接用户态。G0 已实际构建、打包并通过主机检查；**尚无本阶段真机操作或 GPU 阶段验收**。

## 当前交付

| 项目 | 结果 |
|---|---|
| G0 只读核心 | 9 个来源明确的 GCC/GPUCC 字段，两遍，共最多 18 次读取；50,000 us 软件预算，无写回调 |
| 实际 XNU 适配器 | 普通线程绑定 logical 0 / affinity 0x1；完整 VM 页可释放映射，精确地址白名单；保留每项 raw/valid/stable |
| 完整 HakiOS | XNU 8796.121.2 / iOS 16.5 / arm64，实际编译链接，不删旧功能 |
| RAM 包 | 最终 Android/FAT/XNUP/APFS/BootShim 读回；解压最终嵌套 FV，确认无物理 UFS 模块且 Fastboot DXE 完全一致 |
| 固件准备 | 匹配本地 V9.5.2.0.NAHCNFA，8 个文件，共 64,504 字节，仅放忽略区 |
| 软件绘制候选 | 已冻存 fb_threads=2；相对已验 614.4 桌面仅一字节变更，未上机 |
| 本阶段设备操作 | 0；启动计数仍为 HakiOS 96 / iOS16 65 |

G0 镜像：[lithium-hakios-ios16-gpu-g0-01.img](/F:/IOS-MIX1/evidence/gpu-g0-20261123/package/g0-01/lithium-hakios-ios16-gpu-g0-01.img)，**16,367,616 字节**。

```text
image SHA256 61bc8b8220f54e8a2b0772a694690901b89e48a65b2d52463c8f962ab02c7933
kernel SHA256 14635e6dcd3db6ab26226afbdf53f27a6a266d8931e335e91fc229d2b26ea1ff
```

## G0 实现与边界

[g0-core.h](/F:/IOS-MIX1/ports/hakios-lithium/gpu/g0-core.h) 定义精确读取表和预算；[g0-adapter.inc](/F:/IOS-MIX1/ports/hakios-lithium/gpu/g0-adapter.inc) 实际连接 MMIO、时钟、映射和 CPU 绑定；[peripherals/kernel.inc](/F:/IOS-MIX1/ports/hakios-lithium/peripherals/kernel.inc) 在旧单核 GPU 占位检查之前接入独立 G0 分支。未启用 gpu_probe 时不分配 G0 映射；更高阶段仍不接入写操作。G0 与触摸诊断混合申请会拒绝。

| 字段 | 物理地址 |
|---|---|
| CX_GDSCR | 0x008c4034 |
| GX_GDSCR | 0x008c4024 |
| GPU_AHB_CBCR | 0x008c403c |
| GPU_AON_ISENSE_CBCR | 0x008c4044 |
| GPU_GX_GFX3D_CBCR | 0x008c4028 |
| GPU_GX_RBBMTIMER_CBCR | 0x008c40b0 |
| GFX3D_CMD_RCGR | 0x008c4000 |
| GCC_MMSS_BIMC_GFX_CBCR | 0x00309010 |
| GCC_BIMC_GFX_CBCR | 0x00346018 |

**不读取 0x00b00000 GPU 区域，不读取 SMMU，不加载固件，不附加中断，不修改 GPU 电源、时钟或寄存器。** 只读取已返回的 GDSCR/CBCR/RCGR 值；两遍相同不等于供电稳定、总线所有权或实际频率已验证。有效位为 0 时打印 UNREAD，不以默认零充当实测。

映射沿用已修正的 `ml_io_map_unmappable` / `ml_io_unmap` 配对，以真实 PAGE_SIZE 对齐完整 allocation/extent；不释放 4K 子页指针。实际链接 probe 局部帧 224 字节，结果保留在静态记录中。软件预算只限制会返回的读取，不能打断总线永久挂死、SError 或外部 abort。正常 180 秒结束后走既有排空/停核/UEFI 返回；错误或证明不完整保留摘要，不自动重试。

首轮真实编译发现 BSD 翻译单元看不到两个私有映射声明及 VM 属性定义。修复为引用 `vm/memory_types.h` 并声明与固定 `osfmk/arm/machine_routines.h:711/721` 完全相同的函数原型；映射行为、断言与旧功能保持。第二轮完整链接成功。上游 kext symbol-set 检查的已知 exit 2 由原构建脚本分类，成功的实际内核链接独立留证。

## 主机验证

| 工作 | 负责人 | 结果与主代理复核 |
|---|---|---|
| G0 核心及来源 | Bacon | 2,696 场景 / 470,478 检查，ASan/UBSan；主代理核对冻结 header/hash、地址/来源并以实际适配器另测 |
| XNU 适配器/集成/构建 | 主代理 | 实际适配器 14 场景 / 795 检查，覆盖 4K/16K、部分映射失败、受限地址、CPU 拒绝、超时及完整释放；完整内核实际链接 |
| RAM 包/启动脚本 | Kepler | 22 个专项及 6 个 Windows FV 解析测试；主代理实际生产包生成、再次读回、Windows `--check` 通过 |
| 本地固件/G1/G2 | Pasteur | 38 个固定提交源文件、48 个来源锚点、8 个固件哈希和 ELF/FW2 结构；主代理独立核对文件哈希、关键 stock OPP/rail/PAS 段落 |
| fb_threads=2 候选 | Kant | 解压后单一字节 1→2；主代理核对组件/delta，完整参考镜像哈希保持；性能未验证 |

这些测试验证主机逻辑、构建与包内容，**不表示 GPU 已上电、chip ID 已读取、CP/fence 或 GPU 加速通过**。原索引及已验桌面镜像保持；回退使用本报告配套文本补丁，不导出固件/Apple 二进制。

构建与验证入口：

```text
scripts/build-hakios-gpu-g0.py（WSL 内按 14G / 1200% / JOBS=12 运行）
clang -std=c11 -O2 -g -fsanitize=address,undefined -Wall -Wextra -Werror scripts/tests/test-gpu-g0-adapter.c
wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-gpu-g0.py --attempt 01
wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-gpu-g0.py --verify-package --attempt 01
python -B reports/local/run-gpu-g0.py --check --attempt-id gpu-g0-01
```

实际日志与 receipt 位于 [main](/F:/IOS-MIX1/evidence/gpu-g0-20261123/main)、[core](/F:/IOS-MIX1/evidence/gpu-g0-20261123/core)、[package](/F:/IOS-MIX1/evidence/gpu-g0-20261123/package)。隔离 RAM 支持与双 4MiB APFS 卷复用旧冻结，不重构未变的固件或用户态。

## 对原计划的来源修正

1. **最低活跃 GPU 档为 133 MHz**，不是约 214 MHz。两组 stock speed-bin 都给 133 MHz、GFX corner 2、MX corner 4；214 MHz 是 stock 初始档。27 MHz 尾档用于退场，不能充当活跃 OPP。
2. **MX 和 GFX 是两个供电路径。** MX 为 PM8994 S2/S12、RPM smpa/2；GFX 是 CPR3 + 直接 PMI8994 S2，stock 的 RPM smpb/2 禁用。不能把 CPU S11=905mV 证据或 MX corner 当作 GFX 已供电。
3. stock GFX 使用 CPR3，不是旧 CPR2。corner 2 的 520–670mV 表区间需要本机 fuse/offset/aging 计算，不能选区间中的猜测值。
4. zap ELF 的实际可加载 code span 为 8KiB，另需 6,684 字节 SCM metadata。43MiB no-map 总池或 Linux 示例子区均不是当前内存可用证明。
5. Linux 的无 zap 写回退仅在特定 `-ENODEV` 分支；认证失败不会授权该回退。当前 MIX 的 PAS13、签名接受及非安全寄存器权限仍未验证。
6. 614.4 已验桌面的驱动 band1 六样本中位 **17.004ms**，原计划 26–30ms 来自旧档；它不是屏幕物理 FPS，也不能替代同次温度/CPU/wall 对照。

详证：[G1-G2-readiness.md](/F:/IOS-MIX1/evidence/gpu-g0-20261123/power-zap/G1-G2-readiness.md)、[firmware-manifest.json](/F:/IOS-MIX1/evidence/gpu-g0-20261123/power-zap/firmware-manifest.json)。所有固件仅在 `artifacts/local-firmware/lithium-a530/` 的 Git 忽略区，未装入 G0 包、未进入源码补丁。

## 下一次上机与后续阶段

G0 获批准后只启动冻结镜像一次：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 lithium_log_retry=1
```

`run-gpu-g0.py --boot --attempt-id gpu-g0-01` 执行五项预检、先启动 USB 接收器，再单次临时 boot。正常情况下约三分钟到结束，保留十秒摘要后返回 Fastboot；接受后的 240–900 秒内只执行一次 `--verify-return`，核对同一 serial/product。随后对保存的原始 USB 帧与 console 独立重放，核查 18 项 raw/valid/stable、CPU affinity、四路温度、timer/drain/source drop 和终点。不让用户守屏，不自动重复启动。

| 阶段 | 当前状态 | 下一项实机证据 |
|---|---|---|
| G0 | 主机就绪，待批准 | 时钟/GDSC 两遍有效且一致，正常排空/返回 |
| G1 | 供电/时钟来源准备完成，后端未实施 | 本机 fuse/GFX 数字设定与 settled 状态、MX/RPM/bus 确认后才允许上电、chip ID |
| G2 | 匹配固件与 PAS 生命周期已整理 | 明确池所有权与 PAS 可用性，然后单独认证/安全切换；不盲目无 zap 写入 |
| G3/G4 | 尚未实现 | SMMUv2 context、DMA 可见性、CP 和重复 fence、受控 quiesce |
| G5/G6 | 尚未实现 | 像素一致的 GPU 填充/blit 与收益，再接 Freedreno GLES |
| G7 | 延后 | GPMU/DCVS 与 Metal 在实际前置通过后评估 |

并行的 fb_threads=2 包已冻存 SHA256 `901863173f494309fb4e813e2a80d6f8fd1a9989625ec391935dcc762138e3a4`，它是独立 UFS 软件绘制对照，正常桌面启动可能写 Data，需单独确认；G0 不混入这次变量。

本次停在设备操作前，依据用户 GPU 计划 §0 的明确要求：**“任何设备操作（boot/查询/重启/分区/刷写）都要事先得到用户批准”**。请求批准的是已冻存的单次 G0 RAM 测试，而非泛化的后续 GPU 写操作。
