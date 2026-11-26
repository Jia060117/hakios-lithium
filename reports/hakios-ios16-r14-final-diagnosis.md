# R14 最终诊断（2026-11-18）

## 裸存储结果先列：R12 已有基线

100×4KiB，单位微秒；这组数据来自 R12，R14 没有重复裸设备测试。全部零错误、零数据不一致，scratch 已恢复。

| 测试 | 最小 | 平均 | 最大 |
|---|---:|---:|---:|
| read | 196 | 207 | 720 |
| write | 134 | 146 | 385 |
| flush | 246 | 312 | 552 |
| write_flush | 387 | 465 | 728 |

## 本轮终态与修正范围

R14 仅修正 R13 新诊断的逻辑 CPU 遍历边界，使用 `ml_get_max_cpu_number()` 的包含上限；保留完整 HakiOS 8796.121.2、20F66/D22AP/arm64、UFS、2880MiB、三核及原诊断负载。没有调整时钟、journal、flush，也没有重新刷写 userdata。

临时启动一次，主机接受时间 `2026-11-18T02:40:31.153084+08:00`；接收器结束 `2026-11-18T02:51:10.024795+08:00`。原始 launchd 启动，三个 XNU CPU 实际运行，正常 timer 返回且 USB `drain=1`。用户确认并提供 UEFI 结束摘要原图：`RETURN TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`、目标652743。未通过 SpringBoard 桌面显示。本轮原图不能证明运行中曾显示苹果标志。

原图的 `RAW BYTES 783988` 是 UEFI 保留日志计数；USB 控制台是652793字节。它们是独立日志路径，不能直接将差值认作 USB 丢失。

## 实际 boot-args

```text
debug=0x8 rd=md0 ufs_shared_pa=0x0000000040854000 hakios_kver=2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty bootcpu=2 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=600 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

`bootcpu=2` 下本轮映射仍是 XNU逻辑0→物理0x100、1→0x101、2→0x1，与此前报告一致。不能把参数值2直接解释成 XNU逻辑CPU2。MPIDR0由UEFI存储worker占用。单核对照包已冻结且双包检查通过，但没有启动，因此没有实测 SMP A/B 结论。

## 明确的新异常：各核性能差异

同一整数循环100万次；memcpy每样本32×1MiB，各5个样本，错误0。表中耗时为最小/平均/最大微秒；MB/s 是原日志数值。

| XNU CPU | MPIDR | 整数循环 us | memcpy us | memcpy平均 MB/s |
|---|---|---:|---:|---:|
| 0 | 0x80000100 | 161621/162097/163098 | 310286/311069/312718 | 107 |
| 1 | 0x80000101 | 161916/162071/162364 | 309394/313204/316559 | 107 |
| 2 | 0x80000001 | 2441/2451/2466 | 9365/9641/10357 | 3480 |

CPU0/1的整数测试约为CPU2的66.1倍耗时，memcpy约32倍。CPU0/1 MIDR=`0x512f2051`；CPU2 MIDR=`0x512f2011`。这已超出“老芯片所以慢”的解释，但仍不能仅凭循环反推准确MHz、认定某个PLL设置错误或认定所有LS等待都由它导致。

CNTFRQ均为19200000Hz；XNU timebase=625/12ns。MAIR=`0xc08044f00bb44ff`，TCR=`0x22a51a251a`，采样RAM/栈PAR属性为0xff，无转换fault。它们不代表实际SCTLR缓存开关已逐核验证，也没有独立振荡器参考。后续先核实各核缓存控制与真实时钟来源/分频状态。

已只读查看上游 `drivers/power/hakios.cpufreq.c`：该实现明确针对POCO X5 Pro/redwood的EPSS地址。它不能作为MSM8996的现成调频设置直接启用。实际适配应查已验证的本机固件/源码与硬件接口。

## launchd前的固定HFS测试

20×4KiB文件，首个rename创建目标，其余替换；完成20/20，读取校验、清理均成功，整组5205102us。

| 测试 | 次数 | 最小 us | 平均 us | 最大 us | 错误 |
|---|---:|---:|---:|---:|---:|
| write | 20 | 5000 | 5487 | 5886 | 0 |
| fsync | 20 | 8328 | 210815 | 4051389 | 0 |
| rename_replace | 20 | 12225 | 24968 | 28265 | 0 |
| read | 20 | 1163 | 1213 | 1629 | 0 |

没有重现R11那次20.549秒rename。这是新临时目录、不同持久Data状态的工作负载，不能宣称修正CPU诊断边界就优化了文件系统。原日志中的 `revision=13` 和临时目录前缀是保留的工作负载标签；实际启动镜像是R14。

## 三个慢fsync：主要时间在UFS桥外

| op ID | PID/TID | 墙钟 us | 线程CPU us | UFS桥 us | 桥外 us | buf_biowait次数 |
|---|---|---:|---:|---:|---:|---:|
| 1380 | 1/0xb5 | 4050137 | 4050171 | 933 | 4049203 | 31845 |
| 1903 | 11/0x114 | 1448867 | 916886 | 225 | 1448642 | 32453 |
| 1962 | 11/0x114 | 1756867 | 1755806 | 255 | 1756611 | 32800 |

三个操作均成功返回；每个同线程仅1读、1写、0flush，固件耗时分别194/195/197us，桥接睡眠、记录到的唤醒和超时均为0。1380的全窗口也只有1读1写；1903和1962的全窗口另含其他线程226/468次读，不能全部归给该操作。

1380的4.05秒几乎全部记为线程CPU，UFS桥仅933us；cnode/truncate/global/journal锁阶段都是微秒级。`buf.biowait`却调用31845次、累计918456us；另两次是32453次/211315us和32800次/392679us。重点应转向其调用者、buffer身份、B_DONE与循环处理成本，并区分诊断锁/计时本身的开销。1380的CPU计数略高于墙钟34us，来自起止测量顺序/开销，不能解释为CPU利用率超过100%。

23次慢操作采样都见 `state=0x4`（TH_RUN，运行或可运行），没有安全停驻栈，因而显式跳过、`frames=0`；没有可供符号化的HFS内核栈。采样并未发现长锁睡眠/已武装超时等待，不能据此全局排除丢唤醒。慢op没有记录所在核及迁移，尚不能把某一次fsync直接归到某个慢核。

源码核对还发现诊断语义问题：`hfs_systemfile_lock()` 返回已获得的锁位掩码，非errno；因此该阶段的 `errors=2` 不是两次锁失败。下一修订修正计数解释，不改变锁行为。所有stage是inclusive计时，不能相加解释总耗时。

UFS完成路径为固件MPIDR0轮询mailbox；XNU先自旋至2ms，再按1ms睡眠检查。本轮三个慢fsync没有进入该睡眠分支，没有证据将这几秒归因于mailbox轮询超时。

## 用户空间和日志边界

SpringBoard共有18个样本，其中17个在constructor_ready，提交数0，初始化后的CPU累计不变，同步Mach/XPC栈不变；lsd仍有进展，末尾bundle begin/end=111/111、distinct=111、fail=1，容器调用318次，整体scan/gather/enumerate尚未返回。尚不能认定循环死锁；容器fail计数也不能直接替代逐对象errno诊断。最后free1163MiB、available2494MiB。

两轮用户栈均完整记录6个目标进程：SpringBoard、containermanagerd、backboardd、runningboardd、cfprefsd和lsd。屏幕上的 `denied attempt to escape CPU monitor` 对应lsd尝试调整CPU监控限制被拒绝，单凭它不能认定panic或启动停止。

USB原始762073字节、3415帧，重新解码与652793字节控制台逐字节一致；传输缺口、重复、残帧、device-lost均0，覆盖内核横幅至timer及成功排空。随后Pipe error属于返回后的连接结束。**源端仍丢1697字节，FS快照dropped=80/end_lost=7，HFS记录槽full=481，因此不是所有原始日志和操作无损。** 有界诊断也可能拖慢慢核，尚无关闭诊断后的性能对照。

## 下一修订的判定点

1. 逐核确认实际SCTLR/缓存状态、可用的时钟计数与MSM8996时钟来源/分频读数；先读证据，再决定唯一时钟或缓存修正，不套用redwood寄存器。
2. 对高频buf_biowait只做有界聚合，保留caller地址、buffer身份/标志、已完成比例、实际等待次数及执行核，补足这次所有busy栈跳过的证据缺口；修正systemfile_lock返回语义。
3. 在根因候选有依据后只改一项，保留本轮格式和原始UFS基线，完整构建/检查/冻结再汇报。当前不增加等待时长、不先关闭journal、不重刷userdata，也不机械追加同镜像/单核重跑。

准确MHz、逐op执行核、buf调用者以及诊断开销是下一步主要不确定性；桌面、触摸、GPU、全部6GB仍未通过。

完整日志、原始USB帧、原图、bootargs、前150行/后150行及诊断摘录保存在 `reports/local/20261118-024030-881959-ios16-r14`。
镜像SHA256：`6201aae3c699f601488c2fd59540f59a5453844ace89384ce919fbc5393c67fc`。
