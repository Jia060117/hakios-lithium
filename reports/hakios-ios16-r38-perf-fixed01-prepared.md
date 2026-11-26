# R38 perf-fixed01：首次最低固定频率候选

目标：只将perf簇从XO切到source支持的307.2MHz main/2路径。未实现动态调速器、四核XNU或正常最高档。默认cpu_perf_fixed关闭。

UEFI seed成功后、XNU CPU_ON前执行。要求当前affinity0，PSCI逐核证实1/100/101 OFF、12行实际fuse两遍严格匹配、S11六字段1c/09/0/a5/40/82、BHS模式及FSMidle、SAW AVS0、pwrPLL既有锁定及L40、perf完整初态。每次PLL写前重读供电/CPU状态和non-ACD选路。

完整fuse/DT最低档含aging的CPU策略585mV；保留S11已观测数字设定905mV，不写PMIC电压。VSET是数字设定并非ADC；不将ACD未选中写成整个ACD关闭。PLL614.4MHz /2给CPU307.2MHz，源与最终参数逐项读回。未知/错值/锁超时停止，不回滚重试，失败保留屏幕；正常XNU运行后180秒排空并自动返回Fastboot。

Russell实现helper及ASan/UBSan、O2各103cases/22369checks，包含真实无rd的payload参数与完整raw64pin核对；main实际loader/Mu编译、API2原有466checks、所有Device页映射和调用次序/逐写白名单复核、整包分层读回。原内核/用户态复用；旧冻结与索引保持。

镜像 `268605b90e1b571300c53ea556d085c9fbec8118ea0ccea97aaff2991d51cea1`。本候选尚未上机，CPU_ON可能重配置PLL，须以XNUAPCC读回和benchmark确认，不以UEFIprogrammed当实测MHz。

精确boot-args：
```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_freq_diag=1
```
