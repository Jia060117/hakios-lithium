# R37：三核频率、计时和 SpringBoard 调度诊断

2026-11-20。主机端构建与离线验证完成，尚未启动 R37。用户报告三核能进桌面、触摸可用，Clock 图标指针一快一慢；单核匀速。把这条记录为目视现象，尚无同次调度或频率数据可以解释它。

## 1. 每核频率

| XNU logical / phys | 时钟簇 | R35 CNTFRQ | R35 整数 / memcpy | R37 实际 CPU MHz |
| --- | --- | ---: | ---: | --- |
| 0 / `0x1` | `pwrcl` | 19.2 MHz | 2491 µs / 3534 MB/s | 未验证 |
| 1 / `0x100` | `perfcl` | 19.2 MHz | 162535 µs / 110 MB/s | 未验证 |
| 2 / `0x101` | `perfcl` | 19.2 MHz | 162603 µs / 110 MB/s | 未验证 |

R35 同次保存日志：`reports/local/20261120-r35-ui-perf-combined-attempt2/console.log`，第 119、122–124、145–149、1007–1009 行。物理 `0x0` 在本配置由 UEFI/UFS 驻留线程使用，不是第四个可供 XNU 测量的核。

`cpu_freq_diag=1` 在 launchd 前把同一工作线程绑到各在线核，用 `CNTVCT_EL0` 前后夹取 `PMCCNTR_EL0`，按 `CNTFRQ_EL0` 换算五组约 1 ms 忙窗口的有效 MHz。保留控制位、计数差、取样括号误差界和 min/avg/max；`calibrated_mhz_milli` 除以 1000 得 MHz，仅采用 `valid=1` 的行。考虑 PMCR LC/D 与 EL1 过滤，不用整数测试反推频率。这个结果以架构计时器标称频率为基准，也不能单独证明 PLL 来源或桌面运行时频率。

当前 XNU 的通用 KPC 为空实现，构建锁定其哈希，实际 MONOTONIC 为 0。对未启用且使能、中断和导出掩码无占用的 PMU，诊断可以短暂使能周期计数并恢复配置和读回；不清计数器或溢出状态。`cpu_freq_pmu_setup=0` 可禁止配置写入。EL2/EL3 访问控制和固件 PMU 所有权未验证；启用后原始寄存器访问没有 trap 恢复，若在探测开始后中止，不得当成零频率或重试同配置。默认无寄存器探测。

## 2. 计时器

`timer_migrate_diag=1` 打印每核 MPIDR、CurrentEL、CNTFRQ，对六个有向迁移各采八次。记录迁移前后的 `mach_absolute_time`、虚拟计数、物理计数，当场区分倒退和正向差值，输出 min/avg/max、延迟直方图、代表性原始快照。样本 `v0 <= p - offset <= v1` 可提供 `p-v1 <= offset <= p-v0` 的区间估计，前提是计数源一致、访问有效且无回绕；迁移耗时自身不能用来推断偏移。EL1 不直接读 `CNTVOFF_EL2`；CNTPCT 访问是否受更高异常级限制未验证，也没有外部独立时间基准。结果全部待上机，旧日志中相同 CNTFRQ 不能排除偏移、计时中断延迟或异常跳变。

## 3. SpringBoard 与卡顿对齐

`ui_sched_diag=1` 在 XNU 的两个实际线程切换路径累计 SpringBoard/backboardd 的 TID 按核执行时间与切换次数。普通线程每约 5 秒输出 `LITHIUM SCHED` 窗口，`main_tag=1` 来自实际 XNU 主线程标记。`residency_us` 只结算已经切出的区间，包含该区间内的中断时间，不等于用户态 CPU 时间，也不含等待；正在运行的区间要等下次切出。线程名称可辅助识别；`timer_expiries_on_cpu` 只计 XNU 等待超时回调在哪个核执行，不能当作 Clock 动画回调所在核。没有独立动画线程证据时标未验证。输出有容量和数量上限，带遗漏计数。

到主屏后，保持在 Clock 图标所在页，观察约 30 秒并记下抽搐发生的大致主机时间。使用同一台电脑给实时接收的 `LITHIUM SCHED window` 行记到达时间，并依据其 `at_abs` / `uptime_us` 排窗口；USB 缓冲可能使到达时间滞后。回传抽搐前后各两个窗口、SpringBoard `main_tag=1`/相关 TID 行和同时段帧率。先查慢核驻留是否随抽搐增加，再用第 4 步做对照。只有肉眼现象、缺少时间对应或观测到窗口内没有足够样本时，结论仍为未验证。

USB 接收器开始写 `console.log` 后，在另一个 PowerShell 窗口对该文件的新增行加主机时间。替换 `$consoleLog` 为当轮接收文件的路径；它只读取主机文件，用 Ctrl+C 结束。

```powershell
Get-Content -LiteralPath $consoleLog -Tail 0 -Wait | ForEach-Object {
    if ($_ -match 'LITHIUM SCHED|LITHIUM CPU POLICY|MiniIOMobileFramebuffer: .*frames') {
        '{0:o} {1}' -f (Get-Date), $_
    }
}
```

## 4. 慢核临时止血

`ports/hakios-lithium/diagnostics/cpu-slowcores-r37.inc:31`：默认关闭的 `slowcores_idle=1`。在正常线程上下文、确认三个 CPU 在线及逻辑/物理映射后，经 XNU `sched_processor_enable(..., FALSE)` 取消 logical1/2 的普通调度推荐，让常规未绑核工作优先留在 logical0。实际读取 `recommended=` 和 `pset_mask=`，并保留原绑定。没有参数或值为 0 时，没有推荐掩码写入。在线、IPI、定时器和绑核工作仍可运行；直接交接、绑核及系统安全覆盖可能绕过普通选核。它不保证慢核零用户代码，也可能把其他普通内核工作挤回快核。

## 5. 根因只读调查

Linux MSM8996 时钟驱动和 MiCode 的 Mi MIX 共用 `scorpio-n-oss` 源码都将 `0x100/0x101` 映射到性能簇。按源码模型，主 mux 和次 mux 都选 0 时可走标称 19.2 MHz 的 XO。HakiOS 当前两处 PSCI `CPU_ON` 调用传入目标核、入口、上下文，没有配置 APCC PLL、ACD 或供电；**本机安全固件启动从核后选择了哪个源仍未验证**。即使 PMU 接近 19.2 MHz，也需核对有效 mux、自动切换、主/备 PLL 的锁定与分频状态，才能归因为 XO。设备树 `clock-frequency` 只是静态元数据。

Linux 源码与 MiCode Pro 配置、APCC/ACD/CBF、CPR、PM8994 S11 供电及 128 项频率/档位表详见 `evidence/cpu-r37-20261120/clocks/r37-clock-root-cause.md` 和 `frequency-voltage-source-tables.csv`。档位是 CPR corner 编号，DT 约束不是本机实际电压；设备 efuse bin/revision、实测电压与安全固件状态均未验证。ACD 间接寄存器读取需要写 selector，本轮未加入。未设计或实现固定频率或动态调速器。先收齐 1–5 的真机证据，再按匹配的 Pro 硬件计划评估电压、频率切换和失败恢复。

## 真机顺序和精确参数

只用 `artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/`。同名非 `-final` 目录是修复任务 ID 与跨核标志之前的留档，不上机。以下是参考包的完整内嵌参数：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 touch=1 touch_log=1 touch_poll=1 applog_ui_quiet=1 ui_lite=1 touch_smp=1
```

| 包 | 相对上一包唯一变化（累加） | 需要看到 |
| --- | --- | --- |
| `reference` | 以上参考参数 | 三核在线、主屏、触摸和指针基线 |
| `frequency` | 加 `cpu_freq_diag=1` | 三核 `LITHIUM CPU FREQ` 的 PMU、有效值和恢复行 |
| `timer` | 加 `timer_migrate_diag=1` | `LITHIUM TIMER cpu/migration/snapshot`，尤其倒退和大正向差值 |
| `scheduler` | 加 `ui_sched_diag=1` | 指针抽搐前后 `LITHIUM SCHED`、CPU 放置与帧间隔 |
| `idle` | 加 `slowcores_idle=1` | logical1/2 `recommended=0`、仍在线；同场景下按核驻留与指针响应 |

五套完整参数、镜像路径和 SHA256 在同目录 `profiles.json`。使用已有 Fastboot 临时启动流程，参数已经内嵌；UFS Data 正常运行可能写入，`ui_lite=1` 延续既有的持久偏好。各轮已加参数留在下一轮，若诊断失败，先回退失败项，不带着不明确的停点直接测下一包。止血对照可直接改回 `scheduler` 包，取消 `slowcores_idle`。这轮没有发出手机命令。

## 构建、验证及归档

- 生产入口：`cpu-sanity.inc:85`；频率与计时：`cpu-frequency-r37.inc:271`、`:473`；调度记录与输出：`cpu-sched-r37.inc:53`、`cpu-sched-report-r37.inc:66`；XNU 集成：`patches/0026-lithium-cpu-r37.patch`。
- Popper 实现频率/迁移，主代理复核测量边界、锁定 KPC 后端和修正实际 OSFMK 头文件冲突。Clang ASan/UBSan 模拟检查 2107（允许配置）/2103（禁配置）通过；去掉 BSD 头只修复实际内核编译冲突，不改变模拟路径。
- James 核对实际 non-AMP clutch 调度器与推荐接口。主代理复核源码，修复慢核状态标志的跨核发布并复跑 34 个策略检查。Locke 覆盖热路径和报告器，主代理修正 BSD/Mach ID 混用及输出配额并复跑 9 个核心、4 个报告器专项检查；发现问题的初次测试记录保留。
- Chandrasekhar 完成板级时钟来源调查；主代理复核三核历史日志、时钟驱动与 Pro DT 的关键锚点。证据保留在 `evidence/cpu-r37-20261120/`，未提交参考源码或固件。
- 实际增量编译链接、最终整包逐层读回通过。上游 wrapper 在链接成功后按原规则分类已知 kext symbol-set 检查退出；没有把首次真实编译失败当作通过。内核 SHA256 `cdd261e6be574dce61c4781b0b034a4d8412ff039c44b16a9f0cedbaba26cec4`。构建：`pwsh -File reports/local/run-cpu-r37-build.ps1 -Task prepare`、同入口 `-Task kernel`；打包：`wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-cpu-r37.py`。
