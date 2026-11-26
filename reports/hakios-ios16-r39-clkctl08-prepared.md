R39 CLKCTL08 / 614.4MHz复测包

frequency26原图证实phase8 final谓词拒绝50050071；PLL LOCK/11写/MUX5已到位。该完整raw仅作为本机经验profile，不声称未知字段语义。新core只在expected_mux5允许此精确字，初始71、MUX4、LOCK/其他11项快照/供电/温控/CPU-OFF检查保持。

实际完整HakiOS rate08内核、独立RAM loader/Mu support04与镜像28构建和逐层读回；无物理UFS模块。focused真实core/helper测试通过，main核对源码与build hash，旧硬件I/O序列兼容。新路径成功后只继续原有供电/测温收尾，不添加PLL或电压写。回退补丁及原始副本见evidence/cpu-r39-20261121/clkctl08-main。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_perf_hz=614400000 cpu_inherited_supply_r39=1 cpu_rate_soak=1 cpu_rate_soak_parallel=1 cpu_rate_soak_perf_only=1 lithium_log_retry=1 cpu_freq_diag=1
```

内核 7e7c47ccb30ee0cc0a58d3d8eaf2d70ef769d2ac6b8d066e4a69557e26505a3d
镜像 f7ae5d2420c8f59f3098e1b54b1d98f1c767d316e873af314b97f604a56fe60f

真机状态单独见frequency28运行报告，准备报告不代替运行验收。
