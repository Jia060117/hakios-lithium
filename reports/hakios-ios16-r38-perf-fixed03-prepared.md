# R38 perf-fixed03 / frequency12：primary PLL ACTIVE 状态修正

## 已定位的停止原因

frequency11 用户照片有效读回 `first=last=0xc0118000`，旧配置掩码 `0x7ffffff8`，期望 `0x00118000`，失配 `0x40000000`；一轮成功读取，elapsed51µs。LOCK(bit31) 已置位，唯一额外的受掩码影响位是 ACTIVE(bit30)。旧代码先作配置比较，在 LOCK 判定前报 READBACK_ERROR。10 次初始 PLL 写完成，第 11 次 MUX 写未执行，XNU 未启动。`core_not_programmed` 不代表 PLL 没有报告锁定。原始图和转录在 `reports/local/r37-attempts/r37-frequency-11/screen-terminal-evidence.json`，实际 CPU MHz 尚未测得。

## 源码修正与适用证据

主代理修改 `ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h`：新增 ACTIVE 位名称，把配置比较掩码从 `0x7ffffff8` 改为 `0x3ffffff8`，只额外排除 bit30。MODE 的 bit29:3 检查、独立 LOCK 条件、其余寄存器读回、50µs稳定等待、原有有界轮询、供电/fuse/停核/owner条件保持。PLL/MUX 写入值和顺序仍最多 11 次；无电压、ACD、pwrcl 或动态调速器写入。开关仍是默认关闭的 `cpu_perf_fixed`，候选显式启用。

这是针对 primary 的直接证据链：pinned Linux common `clk-cpu-8996.c` 的 `perfcl_pll` 绑定 `prim_pll_regs` 与 `clk_alpha_pll_hwfsm_ops`（143–153行），同 commit 的 `clk-alpha-pll.c` 定义 ACTIVE(bit30)/LOCK(bit31)（33–34行）且该 ops 的 enable/is_enabled 使用 ACTIVE。并非从备用 PLL 的字段类推。MiCode Pro 分支保留 primary MODE/LOCK 映射；generic MSM8996 驱动适用于 Pro 的结论为源码及实机状态相互支持，未取得 Pro 的硬件寄存器手册，不宣称全 revision 保证。

Zeno 完成独立源码核对，见 `evidence/cpu-r38-20261121/pll-bit30-source/report.md/json`。主代理重新核对本地 primary 定义、独立下载同 commit alpha 源码并比对哈希，见 `evidence/cpu-r38-20261121/pll-bit30-main/main-source-recheck.json`。Curie 负责独立的实际生产 core/helper 主机回归，结果和主代理复核见后附验证记录。

## 构建与冻结

只重建实际 loader/Mu，复用冻结 power05 内核与原 RAM 卷；不是一次新内核编译。两个脚本增设独立 revision/选项，旧产物保持。命令（在既有 WSL Ubuntu 的 jia 环境，内存14GiB/CPU1200%/JOBS12资源限制内）：

```text
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/build-hakios-r37-ram-support.py --revision perf-fixed03
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/package-hakios-cpu-r38-clock.py --perf-active
```

独立 support03 的实际构建通过；FV 无物理 UFS provider。包 `lithium-hakios-ios16-r37-ram-frequency-12.img`，SHA256 `e5dd2cd4d7fd35a18ca47ad6cc2626effa559c9036ca88ce33debe09ffbe0e43`，16334848字节，位于 `artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-12/`。Android header、XNUP、FAT loader、Mu FD、APFS、搬运和交接层全部读回。与 frequency11 的内核、payload、APFS 及完整启动参数相同；主机 runner 校验通过，但没有启动。证据：`evidence/cpu-r38-20261121/pll-bit30-main/package-check.json`。

## 下一次上机

需手机从当前保留屏手动回到 Fastboot 后，直接一次 frequency12 临时启动，无需重建或再审批。命令：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --boot --phase frequency --attempt-id r37-frequency-12
```

进入内核后按180秒正常 timer、USB排空和返回proof/10秒倒计时自动回 Fastboot；240–900秒窗口内只执行一次核验：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --verify-return --phase frequency --attempt-id r37-frequency-12
```

boot-args 精确如下（与 frequency11 完全相同）：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_freq_diag=1
```

先核实 PLL LOCK、MUX4 及 XNU 上线，再对比 phys0x100/0x101 的整数/memcpy与原约162ms/106MBps；若 PMU未产生有效测频，保留未测，不能将PLL算术模型当作实测 MHz。本候选仅是307.2MHz初始低档验证；不是满速验收。四核 XNU 目标也未完成，phys0仍驻留服务。前置错误仍保留屏幕，不自动忽略异常或重试。当前阶段无手机查询/启动/重启、分区、刷写、ADB或QEMU。

## 针对性验证与并行执行记录

6 个真实生产头文件/helper 的 host 二进制用 clang18 ASan/UBSan 编译运行通过：既有 core466checks；既有 helper103case/23169checks；新增 core14case/953checks、helper12case/6689checks；两个冻结旧头负向对照分别复现10写、phase6、bit30失配。新逻辑在照片值 `0xc0118000` 的延迟及立即读回模型中均完成11写/MUX4；`0x40118000` 只有ACTIVE没有LOCK时仍超时且无MUX写，bit24或控制位漂移、USER/L损坏及LOCK在快照丢失仍拒绝。默认关闭时没有硬件回调。全部是Mock，不当作实机频率证据。

主代理核对了测试源码、完整命令/输出与来源哈希；实际测试与 support03 编译源一致，没有重复跑历史总回归。复核草稿时发现 helper 测试把8个格式参数读为9个，已交回 Curie 改为8个，记录的成功运行发生在修正后；生产打印格式不变。验证报告 `evidence/cpu-r38-20261121/pll-bit30-tests/results.json`，命令 `wsl -d Ubuntu -u jia --exec /home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/evidence/cpu-r38-20261121/pll-bit30-tests/run-tests.py`。

Zeno 与 Curie 并行，各自仅写独立 evidence 目录，主代理负责共享生产文件及构建打包，无生产写入冲突。Zeno 的源码结论经主代理重新抓取同commit alpha源码及哈希核对；Curie 的结果经主代理核对测试源/输出/实际build输入哈希。子代理总耗时未单独计时，测试进程起止在 results.json；无阻塞子任务或固件二进制提交。R20与旧冻结、原索引保持。回退补丁见 `evidence/cpu-r38-20261121/pll-bit30-main/perf-active-correction.patch`。本候选尚未上机。
