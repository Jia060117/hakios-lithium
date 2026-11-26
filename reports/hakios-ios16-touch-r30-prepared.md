# R30：QUP 首次 RUN/PAUSE 前置状态观察

记录：2026-11-20T00:35:03.087804+08:00。审计目录沿用20261119标签；实际完成日为2026-11-20。本轮仅主机准备，无手机查询或启动。

镜像：`artifacts/hakios/lithium-ios16/phase4-r30-touch-qup-state/lithium-hakios-ios16-r30-touch-qup-state.img`（16289792字节）。
SHA256：`fb7c575ec377dd8854786479cb1448f35974071c2ef19010b186eda1df1f92ae`。

## 上一轮结论与本轮目的

R29一次软复位及基础CONFIG207/ERREN3c/OPMASK300/MASTER1读回完成，但OP一直0xc0，旧自定义OP==0门禁阻止ID调用。因此不能判定触摸芯片不应答；尚未向它发送ID事务。
固定MiCode/Linux源码在新FIFO标签写入前先进入RUN、配置时钟并转PAUSE。R30新增独立、默认关闭的状态观察器；旧ID/初始化门禁不改，不把full位当作已清空或安全传输证明。

## 实现及责任人

- Poincare：`ports/hakios-lithium/peripherals/touch-qup-state-core.h:408` — New default-off core: eight two-pass snapshots, checked RESET/base/setup/first RUN/divider/PAUSE; no FIFO data/tag/ID。
- Main：`ports/hakios-lithium/peripherals/touch-qup-state.inc:97` — Board/context/affinity1/GIC pre-write checks, independent allowlists, sealed callbacks, valid/unread logs。
- Main：`ports/hakios-lithium/peripherals/boot-options.h:204` — Strict touch_qup_state=1 parser, RAM policy and mutually exclusive older active experiments。
- Main：`ports/hakios-lithium/peripherals/kernel.inc:43` — Single background worker dispatch; default R20 path remains inert。
- Copernicus：`maintenance/hakios-lithium/peripherals/touch-qup-state-adapter-test.c:405` — Actual adapter/core fault injection, all eight groups and 34104 scenarios。
- Archimedes：`maintenance/hakios-lithium/peripherals/touch-qup-state-adapter-boundary-test.c:168` — Independent targeted regressions for adapter poison boundary and explicit unread terminal GIC。
- Raman：`scripts/package-hakios-ios16-touch-r30.py:826` — Exact R29 ancestry, source/test/build/isolated Mu/readonly loader/APFS/image gates。
- Main：`scripts/hakios_touch_r30.py:34` — Exact five-file production delta and eight byte-identical old modules。

核心最大八组：pre、post_reset、base、setup、run、clock、pause、terminal；每组双轮17寄存器，共最多272个字段，未尝试/未成功读取逐项标unread。单次SW_RESET；精确18写序列，无第二次RUN、FIFO data-port、新tag、ID调用、W1C/总线清除或失败补救复位。
FIFO聚合计数为MX_WRITE(0x150)=9、MX_READ(0x208)=3（含协议开销），block计数MX_OUTPUT(0x100)/MX_INPUT(0x200)=0；不混用原厂命名混淆的别名。GPIO/供电/IRQ/HID未扩展。
每次写前GIC只读available-view检查，不等于独占或全局排除。终态不新增GIC读取，日志明确terminal_gic=unread。原厂基础ERREN3c启用错误源；setup改ERREN0并保留服务mask300，属显式轮询适配而非原厂全程静默。
**首次RUN可能恢复继承的硬件活动。无新FIFO标签/ID是软件事实，不能保证物理I²C总线无活动；所有权、复位效果、电气及0xc0阶段语义均未验证。**
核心200ms/每阶段20ms/8192返回访问/512共享poll/10us间隔；外层独立100ms/4096物理访问（含GIC）可能先停止。不能限制永久MMIO、map或barrier挂死、SError/XPU/abort。

## 主代理独立复核与修正

Archimedes独立审查复现两处非阻断缺口：active且adapter已poison时可能继续调用时钟；最终PAUSE之后未观察GIC却未明确终态范围。主代理在交付前收紧全部R30回调，active+poison的now返回失败哨兵而不调用外部时钟；结束日志明确GIC unread。旧公共helper字节不改。新增独立boundary测试，原报告及未上机的第一版包/构建/回归保留在pre-review-attempt。
外层void-delay超时使用UINT64_MAX时间哨兵将失败传给核心，此时核心elapsed_us不是硬件耗时测量；以adapter_reason=adapter_deadline等首因及真实读取时间为准，不能拿哨兵衍生值分析总线性能。
完整重建及全部回归针对修正后的最终字节重新执行。实际链接worker栈序言及core/capture序言已读回；仅为选定函数静态观察，不冒称全调用链/printf/中断最大栈证明。

## 实际验证

- 完整HakiOS8796.121.2/arm64、Mu/D22AP/413job；82来源边界及46指令扫描通过。
- 29基础套件+17后续套件=46套主机回归通过（C测试ASan/UBSan），新增核心9766、实际adapter34104场景/4228222断言，独立boundary与实际PID1的376场景通过。
- 108项打包器模拟验证独立复跑；106合并测试源哈希、整包逐层和实际APFS内程序/属主/权限读回通过。
- 历史R9几何hash总门禁仍未解决，不冒称全部历史回归通过。正常上游wrapper的kext符号集exit2位于实际成功链接之后；无删功能绕过新编译错误。
- 命令：`pwsh -File reports/local/run-r30-host-step.ps1 -Task <prepare|config|deps|review|kernel|support|userspace|build-check|peripherals|package>`；WSL16GiB/12CPU，任务14GiB/1200%，JOBS12，大任务顺序执行。
- 独立读回：`wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/evidence/touch-r30-20261119/main/final_offline.py`。

## 并行执行记录

- Chandrasekhar：Fixed MiCode/Linux QUP state/count/clock protocol audit。主代理复核：Read pinned sources and manifest independently; correct 0x150/0x208 FIFO aggregate vs 0x100/0x200 block counts; no physical silence proof。
- Poincare：New portable core, default-off/enabled sanitizer tests。主代理复核：Reviewed all 18 writes, guard phases, poison semantics and first-failure valid bits; independently reran 9766 scenarios on final header。
- Copernicus：Actual production adapter/core fault tests。主代理复核：Reviewed independent numeric whitelist and eight groups; independently reran 34104 scenarios/4228222 assertions without excluding groups。
- Raman：Separate R30 packager and 108 synthetic tests。主代理复核：Reviewed source/build/protected pins and actual APFS gates; independently reran 108 tests, then actual package and final layer/source closure。
- Archimedes：Independent production/source review and targeted adapter regressions。主代理复核：Two nonblocking gaps reproduced, accepted and fixed by main; new boundary suite and final full regression passed. Original review retained。

分工写入范围互不重叠；主代理独占共享接口、合同、构建和集成。没有卡住的代理或共享写冲突；独立审查提出两项后续并由主代理修正，非通过排除/放松断言处理失败。各子代理命令和结果保留于对应evidence目录；耗时以保留运行记录为准，不编造缺失的端到端计时。

## 隔离与保留

R20-display仍为唯一显示基线：1080×2040 BGR24/stride3240、360×680points/scale3。R19不复测。R15/R20/R25/R26/R27/R28/R29冻结字节与原索引保持。
本诊断包复用固定不含物理UFS provider的Mu和只读loader/FAT：resident=Boot/md0/flashboot.apfs/stage0；ramdisk=redwood/md1/root.apfs/init；两卷各4MiB。ufs=0且无root/Data持久存储参数。长期系统仍走UFS；隔离诊断不是改用RAM系统路线。
本轮没有手机查询、临时boot、接收器、分区/ADB/QEMU/刷写动作；次数维持HakiOS53/iOS16为22。当前物理状态未经本轮核验，不能沿用R29之前Fastboot状态当实时事实。

## 后续上机范围和判读

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_state=1
```

下一阶段需用户确认已回Fastboot，并在说明本包一次软复位及条件式首次RUN→CLK→PAUSE的范围后做一次身份/电量只读预检、单次临时boot和USB采集；不自动重试，不扩大分区/Android/Recovery/刷写范围。无需守屏，180秒结束并保留摘要。
重点取LITHIUM TOUCH state result、八阶段field/snapshot、write及end、CPU实际affinity、timer/drain。若只停在pre/post_reset/base/setup，按首失败项定位，不补跑后续访问；若到PAUSE，比较OP/count/state/config/error/status的阶段变化。到PAUSE不等于ID/真实触摸通过，更不自动准许FIFO事务。
若0xc0持续，保留完整状态证据，继续核对I2C阶段语义/复位效果；若出现新错误或活动位，停止并归档，不能当作本轮ID的NACK。若无USB尾部，区分传输与内核终态，不认定任一硬件原因。

## 未验证汇总

- Physical SW_RESET effect, secure ownership, electrical readiness and first-RUN bus behavior
- OP full bits6/7 I2C/RESET phase meaning and count-read side effects
- Actual divider/SCL frequency, PAUSE/internal parser and inherited FIFO state
- MMIO/map/barrier that never returns, abort/SError/XPU not contained by software budgets
- GIC terminal/global exclusion, actual controller ID, IRQ/HID and physical input
- R30 hardware timer/USB drain/end screen; no device has run this package
- Unchanged PID1 initial boot_arg-read failure can miss opt-in
