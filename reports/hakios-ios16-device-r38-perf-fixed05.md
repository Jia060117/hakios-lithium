# R38 perf-fixed05 / frequency14 真机结果

**本轮首次通过受限定频路径进入XNU，三个核运行，两个原慢核有明确微基准提速，并正常定时返回同一设备的Fastboot。** 实际CPU MHz未测得，307.2MHz只是nominal目标；本轮不是UFS桌面或四核验收。

## 性能和时钟证据

同一冻结内核SHA256 `eb5b407451e41be6b2b14ff4c6b7d24d034b1ba30470e396de0d989c35a83a4a`，primary对照为frequency09（clock-power05）。两轮逐核MPIDR确认logical0/1/2→phys0x1/0x100/0x101，5样本/0error、整数checksum相同。当前含各项min/avg/max的原始行及精确比值见benchmark-comparison.json和main-evidence-anchors.json，非凭logical序号猜映射。

|物理affinity|整数平均毫秒 旧→新|整数提速|memcpy平均MB/s 旧→新|带宽倍率|
|---|---:|---:|---:|---:|
|0x1|2.449 → 2.449|1.00×|3391 → 3528|1.04×|
|0x100|161.900 → 9.802|16.52×|112 → 2080|18.57×|
|0x101|161.885 → 9.805|16.51×|111 → 2099|18.91×|

perf两核整数现在约是原快核的4倍耗时，原先约66倍。warm-cache memcpy微基准不等于桌面速度或外存带宽。对照包并非整个固件相同：内核相同且参数只多cpu_perf_fixed1；新loader/Mu承担显式初始化，不能把比较描述为整个包逐字节相同。

内核读到perf簇的19寄存器两遍各valid/stable，MODE0xc0118000有LOCK，L32/ALPHA0/USER109/MUX4，AUTO0。配置模型19.2MHz×32=614.4MHz primary、MAIN/2=307.2MHz；FREQ三个核valid0/samples0，**没有有效PMU MHz测量**，不能把输出0当0MHz或把模型当实测。CNTFRQ19.2MHz是定时器频率。CLKCTL在CPUonline后为0x40440071，与UEFI前置最终照片0x40040071相差bit22；只记录阶段差异，不借此扩大UEFI值集合或给未知位命名。

S11十二读两遍稳定，digital setpoint仍905000uV，没有模拟电压测量或电压设定写。kernel CLOCK end的fixed_frequency_applied0/mmio_writes0仅指这个只读采集器；定频发生在更早UEFI阶段，不构成矛盾。BENCH末尾frequency_registers_changed0也是测试模块局部行为。

## 启动、日志与返回

2026-11-21 13:01:54.865874（Asia/Shanghai）单次接受镜像 `3e697c0195d28f44369e529ce6f6a7b7f7c73aa315c3370397ca6e62c5634e23`（16334848字节），5项只读预检通过、USB先启动。实际Darwin/XNU8796启动、GenericARMPE3of3running和三条online/MPIDR均捕获；RAM APFS/PID1hold已进入。没有启动SpringBoard，未验证触摸或桌面调度。RUN A在secondary显示match0仅是该行当前线程所在核，不表示引导核错误；正常返回明确绑回logical0/affinity1。

捕获正常 `loader_return_after expired` 和normal_timer1、returnCPU0/affinity1、armed RAM、USBdrain1 target37323。保存150frames/42172rawbytes/37372consolebytes，独立parser+冻结receiver重放逐字节一致，缺口/重复/残帧/device-lost及源争用丢0，已输出内核流覆盖至timer/drain。未覆盖UEFI Prepare/Commit内部或所有daemon原始输出。RAM DATA SYNC error2 phase lookup是隔离路径无Data挂载，不是UFS失败。

USB接收器随后于13:05:27.272059报 `[Errno 32] Pipe error`，原始失败状态保留，不重标为接收进程成功。13:05:56.013052，在接受后241.146秒唯一devices/product核实序列号54223ce9、产品MSM8996_A8回Fastboot。该终点及正常timer/drain支持本轮自动返回路径；未请求用户手动恢复，未另取得硬件键事后确认，不冒称捕获固件内部全链路。当前无须再次重启或查询。

## 范围、复核和后续

无本轮新构建、分区/刷写/擦除、ADB、Android/Recovery、QEMU或重试，计数HakiOS77/iOS1646。默认关闭开关和R20/旧冻结/原索引保留。当前只有三个XNU核；phys0驻留UEFI/UFS服务未释放，不能通过cpus4宣称第四核可用。正常高频、长期负载、真实UFS桌面仍未验收。

Zeno并行只读解读已保存日志，主代理处理全部设备生命周期并独立重放USB、核对原始MPIDR/benchmarks/原内核哈希及比值。首稿以power03为对照且timer/drain范围表述不清，主代理要求改为同内核frequency09和区分“观察到日志”与“终点审计”；修正稿已复核。子代理没有生产文件写入、设备操作或测试，墙钟未单独计时，报告时间在review.json。主代理证据：`reports/local/r37-attempts/r37-frequency-14/main-runtime-review.json`。

下一阶段保留这条已验证的低档修复，按实际stock电压/频点依据验证更高稳定档，并单独接入UFS桌面路线。四核还需处理phys0驻留服务的所有权。未将本次307.2MHz配置模型当作完整“四核正常频率”目标完成。

完整boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_freq_diag=1
```

原始运行、USB独立审计、返回核验和benchmark表在 `reports/local/r37-attempts/r37-frequency-14`；代码主机准备见 `reports/hakios-ios16-r38-perf-fixed05-prepared.md/json`。本阶段精确文档回退补丁为runtime-record.patch，不含固件或其他二进制。
