# R38 perf-fixed04 / frequency13：最终快照证据

## 问题与改动

frequency12 原图已确认：先前LOCK轮询0xc0118000/mismatch0通过；第11次写MUX4并立即读回4，5µs后进入phase8 FINAL；完整snapshot返回后最终谓词READBACK_ERROR。当前只能确认11个比较项至少一项不符，不能从末尾fault_pa0x06480044推断CLKCTL出错。先前日志只保留等锁MODE而未保留最终值，这个信息缺口由本修订补齐。

主代理仅在 `ports/hakios-lithium/return-fastboot/cpu-perf-uefi-r38.inc` 原序列结束的END行后增加4行输出，读取已存 `c->core.final`：阶段、valid位图、期望MUX/ALT，以及11项原始值。没有新MMIO/计时调用；`cpu-perf-fixed-r38.h`、所有谓词/掩码/锁定位要求、50µs/5µs等待、PLL/MUX写序列、供电停核检查保持。开关仍为默认关闭的cpu_perf_fixed，候选显式启用。

有效位图按枚举下标：0 MODE，1 L，2 ALPHA，3 USER，4 CONFIG，5 CONFIG_HI，6 TEST，7 TEST_HI，8 ALT_MODE，9 MUX，10 CLKCTL。仅对应bit置位的raw可以解释为成功采样；完整位图0x7ff。未置位的字段可能仍含切换前快照残值；不能当作本次最终读取，raw0也不表示寄存器实测0。

Zeno独立核对了全部11项要求及MiCode/Linux来源，未发现能够证明某项需要放宽的证据；主代理核对实际core相同哈希、读取顺序及失败枚举（READ_ERROR=2，READBACK_ERROR=4）。报告：`evidence/cpu-r38-20261121/pll-final-source/report.md/json`。Curie负责实际helper的针对性主机测试，见后附结果；主代理负责构建/整包读回/集成和runtime照片归档。子代理各自只写独立evidence目录，无共享生产写入。

## 主机产物

实际loader/Mu support04构建通过，FV无物理UFSprovider，旧产物保留。命令（既有WSL资源约束，JOBS12）：

```text
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/build-hakios-r37-ram-support.py --revision perf-fixed04
/home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/package-hakios-cpu-r38-clock.py --perf-final
```

包：`artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-13/lithium-hakios-ios16-r37-ram-frequency-13.img`；SHA256 `fbfd63818fbcdadd03bc7662fe3d3f9c2a184a412518e0d5d957c77ef9beb179`；16334848字节。逐层读回通过，与frequency12的内核、XNUP payload、RAM APFS、所有参数及CPU核心算法一致。仅新loader/其Mu绑定随日志增加变化。主机runner校验通过，尚未发出手机命令。

## 下一次测试

本轮没有手机查询/启动/重启/分区/刷写/ADB/QEMU，不重试frequency12。用户保存照片后人工返回Fastboot，再单次临时boot frequency13：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --boot --phase frequency --attempt-id r37-frequency-13
```

若完成正常内核180秒timer，排空USB和proof后10秒返回Fastboot；接受后240–900秒只核验一次：

```text
python -B reports/local/run-cpu-r37-ram-measure.py --verify-return --phase frequency --attempt-id r37-frequency-13
```

若前置仍失败，拍snapshot/raw三行及END，便可从同次原始值明确哪项不符。不得把先前LOCK当最终LOCK、立即MUX读回当稳定有效频率。307.2MHz是初始低档验证目标，实际频率及四核仍未完成。

boot-args（与frequency12相同）：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_freq_diag=1
```

## 主机测试与复核

Curie完成2个实际旧/新helper的clang18 ASan/UBSan二进制，每个25case：正常成功、11字段各自post-MUX漂移、11字段各自读取失败形成部分valid位图、无LOCK及默认关闭。旧版278069checks、新版278901checks通过，无sanitizer诊断。新输出逐字段与存储快照一致，valid0不输出raw，部分valid保留且未置位字段不得作最终实测解释。

25组对照逐行核实native MMIO调用地址/顺序/写值/次数、原有日志及语义结果均一致，仅序列结束后额外打印1或4行。主代理独立比较25组已保存trace，核对测试源码哈希与实际support04构建输入。没有重跑未改core的历史大套回归；本轮无新内核构建。测试初稿对ALT预期值的固定假设导致一次断言失败，失败证据保留，改为从实际initialALT派生预期后通过；生产代码未因此更改。结果及失败记录在 `evidence/cpu-r38-20261121/pll-final-tests/`。

测试命令：`wsl -d Ubuntu -u jia --exec /home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/evidence/cpu-r38-20261121/pll-final-tests/run-tests.py`。Zeno只读来源与Curie测试并行，主代理复核全部交付，未阻塞于超时子任务；子代理墙钟耗时未单独计时，测试起止见results.json。回退补丁：`evidence/cpu-r38-20261121/pll-final-main/final-snapshot-receipt.patch`。不含固件或其他二进制。
