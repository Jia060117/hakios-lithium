# R38 power04：S11读取候选

镜像 `91d4b99d57e93896ba960ae4b9c1054e5dfc3e74584c66e8c2a021c50ef52ebb`；内核 `7ade6131bc0ed80f6c20e47efdc94b5dbd84b12d258ab97d2987eb819394b29f`。

在已验证power03基础上仅新增默认关闭的 `cpu_spmi_diag=1`。RAM/ufs0/三核/180秒及自动Fastboot流程不变。SID1/PPID0x132；读TYPE、SUBTYPE、RANGE、VSET、MODE、STEP，两遍。最多12个EXT_READL observer CMD写，不写供电寄存器、PLL、CONFIG、IRQ，不清错或重试。遇未知版本、非唯一映射、CONFIG非0或任何事务错误停止。

Russell实现纯core及480cases/77849checks（ASan/UBSan与O2分别通过）；main接入XNU并完成真实增量编译和各层读回，逐项核对编译input hash及冻结header。Aristotle对实际fuse独立解码；主代理重算核对12行raw64和585000uV策略。主机测试不等于硬件证据。

新增文件：`ports/hakios-lithium/diagnostics/cpu-spmi-read-r38.h`、`cpu-supply-spmi-r38.inc`；共享调用：`cpu-sanity.inc`。完整allocation/extent经ml_io_map_unmappable建立并配对释放，保留power03修正。

MMIO external abort或总线永久停顿不由软件轮询超时恢复。本包尚未上机；不声称频率或四核修复完成。

精确参数：
```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_freq_diag=1
```
