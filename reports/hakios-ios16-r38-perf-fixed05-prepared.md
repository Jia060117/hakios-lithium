# R38 perf-fixed05 / frequency14：CLKCTL按阶段精确匹配

frequency13 原图（SHA256299b1cda）全11项 valid0x7ff 已确定唯一失败项：CLKCTL读回0x40040071，旧谓词只接受0x71；其余10项匹配，包括最终MODE0xc0118000的LOCK和MUX4。差异是bit30/18，名称与读写语义仍未验证。来源及完整raw见 `reports/hakios-ios16-device-r38-perf-fixed04.md/json`。

主代理在 `ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h` 新增纯函数r38_perf_clkctl_snapshot_ok：初始/切换前只接受0x71，只有expectedMUX4的最终快照允许精确0x71或0x40040071。其余值（包括单独任一新增位、pwrcl的0x40440071、AUTO位变化）仍拒绝。保留LOCK、全部PLL配置、11项valid、所有供电/fuse/owner/停核门禁、时序和最多11次原PLL/MUX写。无CLKCTL、电压或动态调速器写入；开关仍是默认关闭cpu_perf_fixed，候选显式启用。

这是对同一块板/同bin与revision的**实测profile限定扩展**，不是给未知位编造状态名称或证明硬件已经稳定。Zeno独立来源核对没有取得两个位的定义，赞同该范围内精确实验接纳并记录未知；主代理复核其来源定位、差值算术及真实raw。来源报告 `evidence/cpu-r38-20261121/clkctl-source/report.md/json`。

Curie并行完成实际旧/新core+helper各44case的clang ASan/UBSan测试：新版29622checks、旧版29604checks，无sanitizer错误。照片状态旧版phase8拒绝、新版成功；初始/切换前异常仍拒绝，32个单bit变异及MODE失锁/USER/L损坏仍拒绝，默认关闭0调用。主代理独立核对44组nativeI/O轨迹及测试源与实际构建hash。新增成功分支只继续原有收尾供电核对（6个SPMI读取命令及811个mockR32），无额外PLL写入。未重跑历史大回归；子代理各自独立evidence目录，无生产写入冲突/阻塞重分配，墙钟未单独计时、测试起止见results.json。

实际support05 loader/Mu构建及完整包读回通过，FV无物理UFS provider。复用冻结power05内核/RAM卷/全部参数，未重新构建内核。命令（WSL Ubuntu jia，14GiB/12CPU资源约束）：

```text
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/build-hakios-r37-ram-support.py --revision perf-fixed05
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/package-hakios-cpu-r38-clock.py --perf-clkctl
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/evidence/cpu-r38-20261121/clkctl-tests/run-tests.py
```

新镜像位于 `artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-14/lithium-hakios-ios16-r37-ram-frequency-14.img`，SHA256 `3e697c0195d28f44369e529ce6f6a7b7f7c73aa315c3370397ca6e62c5634e23`，16334848字节。host runner校验通过，尚未上机；本阶段无手机查询/启动/复位/分区/刷写/ADB/QEMU，76/45计数保持。原索引及旧冻结/R20保持。

照片已保存，用户人工返回Fastboot后直接单次frequency14，无需重复审批：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --boot --phase frequency --attempt-id r37-frequency-14
```

若进入XNU，优先采集实际CPU频率有效样本和整数/memcpy，与原phys100/101约162ms/106MBps对照；307.2MHz仅初始低档目标，不能把PLL模型当作实测MHz。正常180秒timer和USBdrain/proof后10秒返回Fastboot；240–900秒只核验一次：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --verify-return --phase frequency --attempt-id r37-frequency-14
```

boot-args保持：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_freq_diag=1
```

前置异常仍保留snapshot/raw；本profile的稳定性、实际频率与四核仍未验证，phys0仍是resident服务核。精确回退补丁见 `evidence/cpu-r38-20261121/clkctl-main/clkctl-profile.patch`。不包含固件或其他二进制。
