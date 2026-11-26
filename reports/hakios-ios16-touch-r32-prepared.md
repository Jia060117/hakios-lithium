# R32：一次最小触摸芯片寄存器读取（主机阶段）

记录：2026-11-20T02:38:38.475708+08:00。本阶段只进行源码、主机测试、完整构建、打包及读回；未查询或启动手机。R31已消费，不重试；当前计数55/iOS16为24保持。

镜像：`artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/lithium-hakios-ios16-r32-touch-qup-transfer.img`，16302080字节；SHA256 `b260776a3fff5513134b2c594e364eb58fb95a495c9a7625e6866354345fe767`。

## 做了什么

新核心`ports/hakios-lithium/peripherals/touch-qup-transfer-core.h`与新适配层`.inc`，经boot-options.h/kernel.inc/contract.json接入。默认关闭`touch_qup_transfer=1`，与旧探测互斥，只允许Stage1clock、ufs0、touch_diag1、180秒RAM包。R31核心/适配层和旧Stage1/ID/snapshot/init/state/PID1全部保持原字节。五项生产变化与冻结R31精确比对；原Git索引及暂存内容不动，备份1126份源文件。

本次调用内先完成未改R31初始化，重新确认PAUSE及编程值。每次FIFO写前再检查PAUSE/错误/已有接收或服务状态；固定九个有效TX字节`81 70 82 01 a3 81 71 87 01`，三个32位字`01827081 877181a3 00000001`，再请求一次RUN。最大一次事务、总22次QUP写、一次RX读。RX先保留原字和字节，仅在完成及选定总线释放观察全部通过后标记accepted；00/ff仅分类为原始值，不冒充芯片型号。

INPUT_DONE和RX_NOT_EMPTY分别有原厂源码依据；两RX头字节跳过，payload为word>>16。完成不等于STOP，随后独立观察BUS_ACTIVE；额外要求BUS_MASTER清零、线路采样高、无残余FIFO指示是本地更严格接受条件，不是物理波形证明。FIFO计数寄存器单位、数值FSM意义、ERROR_EN0/服务掩码300下完成可见性仍未验证。新事务允许源码命名的动态STATUS字段变化，拒绝错误与未覆盖位；不改旧初始化的精确A/B策略。

成功或失败均隔离不复用；不清错/ACK服务、不结束RESET、不恢复、不重试，不改变GIC/GPIO复位/供电，无IRQ/HID事件。实际arm64栈调整：worker在进入R32前释放0x1980旧帧并tail-branch；新probe0xd0、destination核心0x1940、phase0x390/capture0x340/snapshot0x90，选定记录链8560字节，对应stack_init实际默认0x4000（16384字节）。主要结果改为共享once-latch保护的静态记录；主机value/destination API等价和真实适配层回归通过。不是所有异常/中断/logger最大栈证明。

共享外层100ms/4096访问覆盖初始化和事务（含GIC只读检查）；事务20ms/256poll，内层总200ms/8192访问。预算仅约束返回调用，不能防永久MMIO/map/barrier挂死或abort/SError/XPU。日志均带LITHIUM TOUCH前缀、有效位与unread，拆分长行；日志阶段不追加硬件读取。

## 构建和测试

按资源配置（WSL16GiB/12核，任务14GiB/CPU1200%、并发12）顺序运行：

```powershell
pwsh -File F:/IOS-MIX1/reports/local/run-r32-host-step.ps1 -Task prepare
# 随后config、deps、review、kernel、support、userspace、build-check、peripherals、package
```

完整HakiOS8796/iOS16.5/arm64、正常Mu/D22AP和413job流程通过；46项指令扫描通过。35套基础+17套后续共52套ASan/UBSan通过；115项打包检查及753子检查通过；最终116项测试源码hash闭包和真实Android/gzip/DTB/FAT/XNUP/APFS程序、权限及双卷位置读回通过。

核心测试：
```text
PASS success: all 256 raw payload bytes including 00/ff, arbitrary overhead, 500 accesses, exact writes, per-FIFO guards, checked second RUN, delayed completion/STOP, absolute origins, retained flags/no cleanup
PASS invalid API: NULL and each missing callback, no callbacks
PASS exhaustive callback poison: 7134 faults across 2960 baseline callbacks; false/false+late/late/rollback on actual initializer and transfer paths; zero callbacks after poison
PASS own validity: 122 partial/late/rollback sample reads; later partial pass clears earlier evidence; raw RX retained but ID uncommitted through STOP failure
PASS fresh sequencing: 197 mutation/partial cases, full programming guard, STATE last before EACH FIFO and second RUN, all service/done/RX/full/unknown bits rejected pre-RUN
PASS runtime evidence: 4565 error/unknown/OP cases, all 4096 dynamic FSM field combinations, 256 arbitrary RX overhead pairs, missing/extra RX, raw occupancy only
PASS bounds: frozen-time completion/STOP and shared 256-poll cap, 71 persistent outer-cap positions, 8192 core access cap, exact 20ms/200ms limits, poisoned helpers and illegal writes/read whitelist
PASS touch-qup-transfer-core enabled: 12375 scenarios; host model only, physical transaction/ACK/STOP/identity/ownership untested
```
实际适配层测试：
```text
physical faults: 1950 independently replayed callback positions x late/rollback
R32 actual adapter: 10177 scenarios, 0 failed checks, 7492247 assertions; evidence_runs=5198 mode=actual-production
```
PID1复用测试：
```text
PASS touch-diagnostic-init scenarios=380 exact-main=1 baseline-equivalence=1 storage-on-hold=0 reboot-on-hold=0 session-lines=2 mock-termination=1
PASS patch applicability/reversal, exact-main host mocks, one arm64 object; inputs unchanged
```
完整证据：`evidence/touch-r32-20261120/main/`（构建、最终回归/读回、栈反汇编）；`protocol/`、`core/`、`adapter/`、`package/`、`independent-review/`与`stack-review-final/`保存各代理结果。最终引用文件/行号见main/final-source-lines.json。

历史R9几何0021总回归hash门禁仍未解决，不计为通过。具体构建退出/链接处理以本轮kernel.log及报告为准；未删除功能。

## 并行执行记录和主代理复核

- **Wegener**：Pinned source audit, TX/RX/STOP/status and occupancy limits。主代理复核：Rehashed six files and read protocol windows; retained physical unknowns。
- **Banach**：New default-off transaction core and sanitizer/fault tests。主代理复核：Reviewed callbacks, per-write PAUSE guards, partial evidence and deferred ID commit; independently reran final tests。
- **Laplace**：Actual adapter/core numeric oracle and fault tests。主代理复核：Reviewed22-write numeric sequence, one-shot/context/poison/return caps and log validity; independently reran full suite。
- **Epicurus**：R32 package ancestry, readback validator and115 synthetic tests。主代理复核：Read source checks and preserved pins, independently reran115 tests and final image/APFS readback。
- **Hypatia**：Independent production audit and actual parser/adapter/core probes。主代理复核：Checked concrete repros; fixed early ID commit, prepoison external calls and premap deadline, then verified final source hashes。
- **Goodall**：Independent destination/tail-dispatch revision audit and API equivalence smoke。主代理复核：Checked final diff and destination/value outputs, isolation and guarded storage; main checked actual arm64 tail branch/prologues。
- **Main**：Parser/dispatch/contract/integration/build/package/checkpoints。主代理复核：All integration, complete build, instruction scans, final readback and original index verification performed locally。


各代理写入范围分离：协议证据、核心/核心测试、适配层测试、打包/打包测试、独立审查；共享parser/dispatch/contract/构建由主代理负责。无共享文件合并冲突。发现的早提交ID、已中止后回调及日志后映射超预算均已退回修正；早期失败记录保留，最终重新跑全量主机测试。耗时和确切执行命令见各`evidence/touch-r32-20261120/`子目录日志/JSON；不把模型推理时间当作执行耗时。

## 下次上机的唯一变量与风险

准确boot-args：
```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_transfer=1
```

相对R31只替换末尾实验参数为`touch_qup_transfer=1`。下一阶段先说明会执行原QUP初始化、一次0x38/A3读及第二RUN，再取得对应确认；新的独立预检记录、一次临时boot、不刷写分区/不自动重试。RAM隔离不改变长期UFS路线。需保留`transfer result/progress/counters/rx/initializer/init_poll/field/write/end`、CPU affinity及正常timer/drain；失败时看first_failure有效字段与写入序号，不把unread当零。`observed`只表示选定寄存器完成/释放及原始字节观察，不等于物理触摸可用。NACK、未知位、超时必须按实际证据分支，不预猜供电/线路/芯片错误。

未验证项目详见JSON：真实ID/应答、轮询完成可见性、物理总线/所有权、计数语义、SCL频率、IRQ/HID、timer/USB新包终态。下阶段先做这一事务；成功后才推进报告读取与触点/HID投递。
