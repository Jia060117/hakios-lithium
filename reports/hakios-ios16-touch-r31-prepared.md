# R31：原轮询证据与按阶段的精确状态组合

记录：2026-11-20T01:33:06.097698+08:00。本报告为主机准备阶段，不包含R31手机操作。用户已确认手机Fastboot可测试；主代理在主机验证完成后说明实际范围，再进行单次预检和临时boot。

镜像：`artifacts/hakios/lithium-ios16/phase4-r31-touch-qup-phase/lithium-hakios-ios16-r31-touch-qup-phase.img`，16293888字节。SHA256：`a6bd21ef428e620f4cf9fe9f5d954c23c00c12d3d19efe523a70b67b2e2d0f53`。

## 变化与依据

R30在第一次RUN写入后因STATUS整体空闲值检查停止；后来terminal0x0c110000的变化是两个内部FSM字段，但失败poll原值未独立保留。R31保留全部R30文件，新增`touch-qup-phase-core.h`及`touch-qup-phase.inc`，显式`touch_qup_phase=1`默认关闭，要求隔离RAM/ufs0/Stage1clock/180秒，并与旧实验互斥。

原厂固定commit1492c8e4373b7428bb0accc4f4881e97b36508b9的`drivers/platform/msm/i2c-msm-prof-dbg.c:196–214`定义DAT[12:10]、CLK[15:13]、O_FSM[18:16]、I_FSM[21:19]。0x0c110000的O/I是1/2；源码没有这些数值的通用合法性表。主代理独立检查blob及3259目录树hash，具体来源/链接见protocol报告。

**显式实验性策略**：PRE/RESET/post_reset/base/setup及RESET-targetpoll只接受A=0x0c000000；RUN-target/PAUSE-targetpoll和RUN/CLOCK/PAUSE成功guard只接受完整A或完整B=0x0c110000。不是字段掩码或任意范围：DAT/CLK仍0，O/I只配对(0,0)/(1,2)，所有其他位固定（错误/活动/未知/保留位0，SDA/SCL采样高）；混合FSM组合或任一其他位继续停止。来源支持字段及转换顺序，**不证明O1/I2/PAUSE通用安全或物理总线静默**。

4个固定slot分别记录SW_RESET、普通RESET、首次RUN、PAUSE的最后实际poll；每组五字段VERSION/ERROR/STATUS/OP/STATE带accepted valid位。失败slot/index在terminal前保持，部分/迟读unread。时间来自既有返回检查，不新增MMIO或time回调；未读完成时间明确unread。MAX时间哨兵是失败信号，不可当真实耗时。

旧17寄存器双轮/8阶段、18写顺序、最多一次SW_RESET/首次RUN、总200ms/每阶段20ms/8192/512poll和外层100ms/4096访问保留。无FIFO data-port/tag/ID、第二RUN、错误确认、总线清除、恢复复位、GIC写、GPIO/rails、IRQ/HID。terminal GIC明确unread。首次RUN仍可能恢复继承硬件活动。软件预算不能约束永久MMIO/map/barrier、abort/SError/XPU。

## 主机验证

- 完整HakiOS8796.121.2/arm64、Mu/D22AP和413job构建，82源码边界与46指令扫描通过。
- 32+17=49套回归通过；新core50762场景、实际adapter35040场景/5371812断言；实际PID1 378场景。C测试使用ASan/UBSan，所有既有套件保留。
- 130打包测试/820subtests独立复跑；111合并测试源哈希及实际APFS程序/属主/权限、payload、整包读回通过。
- linked worker/core/capture序言已读回；仅为选定函数静态观察，不称完整callchain/printf/中断最大栈证明。
- 历史R9几何hash总门禁未解决。上游成功链接后kext符号集exit2保留，无删功能绕过新编译错误。
- 主机命令：`pwsh -File reports/local/run-r31-host-step.ps1 -Task <prepare|config|deps|review|kernel|support|userspace|build-check|peripherals|package>`。WSL16GiB/12CPU、task14GiB/1200%、JOBS12，大任务顺序。

## 并行执行和主代理复核

- Euler：Pinned STATUS field/order audit and primary Git tree/blob validation。主代理复核：Independently rehashed 7 local sources/blobs and 3259 canonical trees; read decoder196-214; numeric legality not proven。
- Hume：Separate default-off core, exact failed-poll preservation and exhaustive phase-policy tests。主代理复核：Reviewed last-pass/failure-slot publication, accepted-read time provenance, poison and exact A/B checks; full50762 cases independently rerun。
- Euclid：Actual adapter/core fixture and fault-injection suites。主代理复核：Reviewed independent18write/17read policy and every poll field; initial format mismatch aligned to actual unread semantics, no excluded groups; full35040 cases rerun。
- Sartre：R31 packager and130 synthetic validator tests。主代理复核：Reviewed immutableR30 ancestry/currentsource closure/APFS readback; interimstrict evidence retained, finalexactpair metadata and130tests independently rerun。
- Singer：Independent production audit, small syntax/context probes。主代理复核：Read final report/source line windows, no source blockers; actual linked prologue readback separately performed by main。

共享解析器/日志/adapter/contract/构建由主代理负责，子代理写入范围分离，无共享文件冲突。初版strict策略及fixture格式失败证据保留；最终策略完成独立审查和全量重跑，不排除失败组。耗时依保留的各命令记录，不编造未记载的端到端计时。

## 保留、范围及下一步

R20-display仍为唯一显示基线：1080×2040 BGR24/stride3240，360×680points/scale3；R19不复测。旧R15/R20/R25–R30冻结字节及原Git索引/暂存内容保持，备份904文件，导出正反向精确补丁。

本包仍是独立RAM诊断（长期系统仍在UFS），固定无physicalUFSprovider Mu和只读loader/FAT，resident=Boot/md0/flashboot.apfs/stage0；ramdisk=redwood/md1/root.apfs/init，两卷各4MiB。无root/Data参数、无分区或持久日志访问扩展。

下一设备阶段只读身份/解锁/电量/下载容量预检，然后一次临时boot与USB采集，运行180秒，不自动重试。范围为一次QUP软复位/设置、带精确实验状态检查的首次RUN、名义100kHz divider和PAUSE；无FIFOtag/ID读取。需核对poll_failure/poll/poll_field、八phase、write/end及实际affinity/timer/drain，不守屏。若停住按原值/validity识别，不猜芯片或总线损坏；到PAUSE也不等于触摸通过。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_phase=1
```
