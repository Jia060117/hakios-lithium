# R37 frequency03：频率补测与返回核验

日期：2026-11-20，Asia/Shanghai。22:51:33.661462 临时启动一次；22:55:34 唯一返回检查核实同一设备已在 Fastboot。无手动恢复步骤、无重试；未另获用户事后未按硬件键的确认，USB 也未记录 UEFI Prepare/Commit 内部字段，自动返回归因为终点支持证据。

## 频率结果

| 物理 affinity | CNTFRQ (Hz) | MHz | 有效样本 | 结果 |
| --- | ---: | ---: | ---: | --- |
| `0x1` | 19200000 | 未测 | 0 | `near_cycle_overflow`，changed=0 |
| `0x100` | 19200000 | 未测 | 0 | `cycle_interrupt_unbounded`，changed=0 |
| `0x101` | 19200000 | **19.202** | 5 | min/avg/max 均19.202，恢复控制位读回相等 |

`0x101` 的输出边界19.126–19.280 MHz，考虑整数输出向下取整，外包络为 `[19.126,19.281)` MHz；与 frequency02 的19.193 MHz接近。它支持此核有效周期速率接近19.2MHz，仍不能证明实际时钟源为XO。未测两核的零统计不是0MHz。`0x100` 提前拒绝路径的 restored=1 只是保存副本相等，未发生接管或最终硬件恢复复读。

完整 boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_freq_diag=1 cpu_freq_pmu_borrow=1
```

原始频率行：

```text
LITHIUM CPU FREQ begin revision=37 prelaunch=1 raw_pmu_optin=1 fault_guard=0 el2_el3_trap_risk=1 samples=5 nominal_us=1000 spin_cap=262144 online=3 max_cpu=2 truncated=0 cntfrq_assumed_correct=1 external_reference=0 active_clock_or_pll_proof=0
LITHIUM CPU FREQ logical=0 actual=0 mpidr=0x80000001 currentel=0x4 pmuver=1 cntfrq=19200000 pmcr=0x51004020 enable=0x80000000 filter=0x0 interrupt=0x31
LITHIUM CPU FREQ logical=0 borrow_requested=1 borrow_changed=0 borrow_reason=near_cycle_overflow setup_requested=1 unowned_audited=1 setup_readback=1 controls_stable=1 restored=1 used_pmcr=0x51004020 used_filter=0x0 counter_width=64 overflow_width=32 divisor=1 overflow_before=0x0 overflow_after=0x0 guard_counts=268435456 cycle_irq_borrow_supported=0
LITHIUM CPU FREQ logical=0 valid=0 samples=0 invalid=0 timeout=0 wraps64=0 effective_mhz_milli_min=0 effective_mhz_milli_avg=0 effective_mhz_milli_max=0 calibrated_mhz_milli=0 bound_min=0 bound_max=0 cycles_min=0 cycles_avg=0 cycles_max=0 ticks_min=0 ticks_avg=0 ticks_max=0 bracket_max_ticks=0
LITHIUM CPU FREQ logical=1 actual=1 mpidr=0x80000100 currentel=0x4 pmuver=1 cntfrq=19200000 pmcr=0x51004020 enable=0x4 filter=0x0 interrupt=0x8000007f
LITHIUM CPU FREQ logical=1 borrow_requested=1 borrow_changed=0 borrow_reason=cycle_interrupt_unbounded setup_requested=1 unowned_audited=1 setup_readback=1 controls_stable=1 restored=1 used_pmcr=0x51004020 used_filter=0x0 counter_width=64 overflow_width=32 divisor=1 overflow_before=0x0 overflow_after=0x0 guard_counts=268435456 cycle_irq_borrow_supported=0
LITHIUM CPU FREQ logical=1 valid=0 samples=0 invalid=0 timeout=0 wraps64=0 effective_mhz_milli_min=0 effective_mhz_milli_avg=0 effective_mhz_milli_max=0 calibrated_mhz_milli=0 bound_min=0 bound_max=0 cycles_min=0 cycles_avg=0 cycles_max=0 ticks_min=0 ticks_avg=0 ticks_max=0 bracket_max_ticks=0
LITHIUM CPU FREQ logical=2 actual=2 mpidr=0x80000101 currentel=0x4 pmuver=1 cntfrq=19200000 pmcr=0x51004020 enable=0x0 filter=0x0 interrupt=0x0
LITHIUM CPU FREQ logical=2 borrow_requested=1 borrow_changed=1 borrow_reason=none setup_requested=1 unowned_audited=1 setup_readback=1 controls_stable=1 restored=1 used_pmcr=0x51004021 used_filter=0x0 counter_width=64 overflow_width=32 divisor=1 overflow_before=0x0 overflow_after=0x0 guard_counts=268435456 cycle_irq_borrow_supported=0
LITHIUM CPU FREQ logical=2 valid=1 samples=5 invalid=0 timeout=0 wraps64=0 effective_mhz_milli_min=19202 effective_mhz_milli_avg=19202 effective_mhz_milli_max=19202 calibrated_mhz_milli=19202 bound_min=19126 bound_max=19280 cycles_min=19347 cycles_avg=19353 cycles_max=19378 ticks_min=19344 ticks_avg=19350 ticks_max=19375 bracket_max_ticks=155
LITHIUM CPU FREQ end revision=37 original_binding_restored=1 original_unbound=1 previous_was_null=0 clock_voltage_writes=0 counter_resets=0 irq_included=1 preemption_disabled_per_freq_sample=1 hypervisor_or_secure_exclusion_unknown=1
```

## 结束、返回与日志

正常180秒timer，`return_cpu logical=0 affinity=0x1`、armed和`drain=1 target=12578`均已记录。54帧/12627 console字节独立重放一致；缺口、重复、残帧、device lost均0。接收器22:55:05.520217的`capture_failed / [Errno 32] Pipe error`原样保留，不重标为进程成功；Fastboot终点来自之后独立的devices/product检查。

22:55:34返回检查实际发现`54223ce9`及`MSM8996_A8`，无需用户手动恢复就继续下一项计时测试。该证据支持新返回包有效；未把前轮手动恢复当成成功，也未把RAM证据推广为UFS自动返回能力。RAM的Data sync lookup error2不代表UFS失败。

本包是隔离RAM PID1 hold，没有SpringBoard，不能验收桌面抽搐原因或调度止血。无分区、刷写、擦除、ADB、Android/Recovery、QEMU、PLL或电压更改；新功能默认关闭的既有规则保持，本轮未重建。

## 证据与并行复核

- 原始证据：`reports/local/r37-attempts/r37-frequency-03/`中的result/commands/console/frames/usb-audit/return-verification。
- Kant独立解读：`evidence/cpu-r37-20261120/frequency03-review/review.md/json`。主代理独立核对实际三核日志组、物理映射、频率字段及完整console SHA，结果一致；采用其提前拒绝restore标志的限定说明。
- 计数到本轮结束为66/35；timer02随后另一次启动计数67/36。所有旧冻结产物保留。

下一步：先完成已经启动的timer02跨核计时及其自动返回核验。任务1仍部分完成，任务3/4必须在真实桌面负载验证；定频设计仍等待前置证据，不写动态调速器。
