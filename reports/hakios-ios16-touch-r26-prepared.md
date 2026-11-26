# R26：触摸分支时钟准备与 RAM 诊断保持

2026-11-19，Asia/Shanghai。用户要求“进入下一修订”后完成主机开发、测试、完整 HakiOS 构建和独立镜像冻结；本轮手机查询、启动、重启、刷写、分区操作及 QEMU 均为 0。R20-display 是唯一已验收显示基线；R19 归档，R25 已消费，不重试。

**结果：一个新的 R26 实验性诊断包已准备完成，尚未上机。** 它补齐 R25 对原厂 HID0 分频编码的识别，并让隔离 RAM 的 PID1 保持等待，交由既有内核 timer 收尾。它没有实现物理 I2C 收发、触摸 IRQ 或 HID 投递，不作为可操作触摸或桌面验收包。

## 冻结产物

| 项目 | 值 |
| --- | --- |
| 镜像 | `artifacts/hakios/lithium-ios16/phase4-r26-touch-clock/lithium-hakios-ios16-r26-touch-clock.img` |
| 大小 | 16,269,312 字节 |
| SHA256 | `ff79c3ea55fb3022d997d119859b4b772fcbe233fc4364a8dc58a20c89612351` |
| 新内核 SHA256 | `9170507ef742114f085286d01fe1b7f4a3b5f1f7551504c802ae62b226ef7f72` |
| 实际新 init SHA256 | `49280373ae22a38f7dd6c69912ea1b78b1f0c6a8d180d56cfa23c5e3d591e32f` |
| 固定隔离 Mu | `a2d3afc1815552973ff3562e1d94af2322c4c1fa81609fec52977a0a6150dde7` |
| 固定只读 loader | `efaeb95e73680922da42d8719b3160be1beb6cc7fcbdeb2a42e91a08072013f5` |

精确内嵌 payload 参数：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1
```

loader 另有 debug、rd=md0 和版本前缀；上述不是实机最终命令行。含最长保守前缀和 NUL 的预算为335字节，容量608字节。`bootcpu=1 cpus=1` 的目标 affinity0x1、R20 的1080×2040 BGR24/stride3240及360×680 points/scale3均保留；R26运行核号仍待同次日志确认。

## 为什么这样修改

R25 同次日志只读到 `CFG=0`、`CMD=0x80000000`，在仅接受 `(CFG & 0x371f)==1` 的门禁停止，4读、0写、72us；没有执行分支使能或 QUP 读取。该轮结论仍原样保留。

固定原厂源码 `clock-local2.c:543–558` 的交接识别将 HID0 与 HID1 视为等价：分频字段不同时，只有其中之一大于1才拒绝。Linux固定源码的计算也让0和1均保持parent频率。原厂QUP6 I2C表在 `clock-gcc-8996.c:997–1014` 指向XO、规范HID1、HID setter。因此CFG0有可识别的XO /1编码依据，不需要为了通过检查写CFG/UPDATE/ROOT_EN。源码证据与提交/哈希保留于 `evidence/touch-r26-20261119/clocks/findings.md`、`source-manifest.json`、`candidate-review.json`。

这只说明编码识别有依据，不证明XO实际频率、当前电压、NoC、安全世界所有权或控制器静默。R25 的ROOT_OFF=1仍表示该观察时刻根时钟关闭；R26在分支需求后必须观察它清零，不能直接写状态位消除。

## 实现和默认行为

1. `ports/hakios-lithium/peripherals/boot-options.h:54` 加入严格解析的 `touch_clock_init=0/1`，仅允许与显式 `touch_stage=1` 搭配；重复、格式错误或其他stage均拒绝。缺省为关。`kernel.inc:50` 分派独立入口。
2. `touch-stage1.inc:180` 的新入口接受受审字段0或1；原 `probe()` 保留严格HID1路径。仍仅允许AHB vote bit15、apps CBCR bit0、GPIO87/88 mux/OE四处掩码写，不写CMD/CFG、M/N/D、PLL、复位、供电、QUP、IRQ或FIFO。顺序仍为AHB投票→AHB ON→apps branch→ROOT_OFF清零→复核→引脚→快照；前后CFG受审字段改变，即使0↔1，也停止。
3. `patches/0022-lithium-touch-diagnostic-init.patch:86`、`:152`、`:181` 在完整上游init中加入默认关闭的 `touch_diag=1` 准入/保持。只有PID1在原控制台准备和非PID1命令分支之后、root/Data挂载及通用自检之前判断。正确诊断配置输出两条有界session日志，并用原libmini的usleep等待现有kernel loader timer；不执行通用自检/run_rc/RB_HALT。原 init 全功能保留。
4. 同步/审查由既有完整HakiOS流程完成。`scripts/hakios_touch_r26.py` 固定逐字节R25 ancestry与五个明确port差异；`review-hakios-ios16.py` 在临时目录重建上游＋R13＋精确0022，并比较活动init全文；`check-hakios-ios16-build.py` 以新报告/目录保留本轮实际产物和46项扫描，不覆盖R25冻结检查。
5. `scripts/package-hakios-ios16-touch-r26.py` 使用本轮正常完整构建的新内核和新RAM init卷；逐字节复用已审查、无物理UFS provider的R25 Mu/只读loader/FAT。正常完整Mu/loader也重新构建和扫描，但不作为本隔离包的固件。

诊断init明确边界：首次原 `boot_arg()` 查询失败或没有识别到赋值时无法确立opt-in，仍走原init；识别到请求后再读完整快照失败则进入阻止状态的等待。已进入hold后不会主动halt，不能扩大为任意错误下必定hold。若kernel timer、MMIO或sleep永久停滞，用户态等待不能救回。此边界已由独立测试复现并披露。

## RAM 拓扑和存储隔离

实际读取旧R25与新R26 APFS内容，保持如下链路：

| Payload项 | 卷 | 初始设备与程序 |
| --- | --- | --- |
| resident | flashboot.apfs / Boot | md0，/sbin/launchd为stage0 |
| ramdisk | root.apfs / redwood | md1，/sbin/init为本轮新程序 |

stage0只读挂载md1到/redwood后执行init。两个卷各4MiB。新root.apfs SHA256 `b42f92754517b178538f7df2c702d2613d96d479235366a1322b4d9e01bb46ee`，包含69,016字节新init；flashboot/stage0字节与R25一致。

打包前发现模块初稿曾把两个卷角色反置，尚未生成镜像；主代理指出后，Franklin通过真实apfs-tool cat确认旧卷内容并修正。之后才冻结R26。记录见 `package/topology-correction.md`，没有修改R25或绕过原启动链。

本包 `ufs=0`，没有root_apfs/var_hfs/ufs_write/ufs_shared_pa或存储bench；复用Mu实际FV map确认LithiumUfsDxe的名称/GUID缺席，loader固定只读。不是改变长期UFS路线。Mu之前厂商固件/TZ活动没有实测，不承诺整个硬件生命周期完全无存储访问。

## 实际构建和测试

资源沿用批准配置：WSL16GiB/12CPU/swap2GiB；systemd任务14GiB/CPU1200%/12并发，大任务顺序执行。执行入口与完整日志为 `reports/local/run-r26-host-step.ps1`、`evidence/touch-r26-20261119/main/`。

| 检查 | 实际结果 |
| --- | --- |
| prepare / config / deps | 通过，完整HakiOS固定pin8796/arm64 |
| 逐字节来源与装载审查 | 82项通过；审查init以完整生成比较替换旧快照，未放宽成任意hash |
| 完整kernel | 正常上游make kernel流程完成，新链接hash已核对；既有配置的kext symbol-set检查仍有上游已注明的exit2，wrapper按其既有规则确认mach输出；不是本轮取消功能或忽略编译错误 |
| 正常Mu / supplied D22AP loader | 构建通过，身份保持原20F66输入 |
| 完整用户空间 / RAM卷 | 413个job构建检查；两卷apfsck clean、486/8条目逐项读回；精确System沿用既有全量校验hash |
| 实际46项指令扫描 | 通过；新kernel/new init、loader与正常Mu按ARMv8.0限制检查 |
| 完整外围回归 | 16项base＋17项follow-up，共33套通过ASan/UBSan；旧351场景原样通过；新clock套件共5241场景（含旧351，新增4890）；init实际main共368场景 |
| 打包器拒绝/读回模型 | 13项通过，修正support报告契约后再次通过 |
| 冻结/实际APFS/整包 | Android头/SHA1/gzip/stock DTB/FAT/EFI/XNUP/内核/双RAM卷和精确命令行读回通过；416项实际权限核对 |
| 最终独立离线核对 | 重新核对所有产物、20项冻结证据、R15/R20/R25和合并parent/base/init三份测试来源；Git index未变 |
| 历史总回归 | 本轮不冒称通过；先前R9快照不识别既有0021几何补丁的hash门禁失败仍保留，没有改旧快照来掩盖 |

主命令，Task按顺序为prepare、config、deps、review、kernel、support、userspace、build-check、peripherals、package：

```powershell
& reports/local/run-r26-host-step.ps1 -Task <Task>
```

完整外围入口：`python3 -B scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir <new-directory>`。实际生产函数测试和hostmock结果只证明被测代码，不证明触摸硬件。

审查发现next顶层来源清单未单独列出新增clock测试依赖，实际base子报告完整列出；打包器同时校验parent与base，最终另合并parent/base/init三份来源逐项核对，未只用顶层hash冒充闭包。证据：`review/findings.md`、`main/final-offline-preflight.json`。

首次打包在创建输出目录前被 `Current normal support artifacts differ` 拒绝：新模块误要求support的kernel_built=true，而正常support脚本只负责Mu/loader，按契约写false。主代理核对真实loader/FD/build/scan各hash一致后修正该条件；新kernel仍单独绑定实际build、冻结拷贝与扫描。失败日志保留，第二次主机构建打包才成功，无设备重试。Windows直接调用Linux路径审计的尝试也保留为主机路径上下文错误；最终核验在WSL中执行，不改冻结内容。

## 并行执行和主代理复核

| 分工 | 交付和复核 |
| --- | --- |
| Curie：时钟原始依据 | clocks/findings/source-manifest/candidate-review；主代理抽查原厂HID0/1判断、位掩码与顺序，并修正契约中落后的Stage1 hash。约19:43–19:57，未被退回重写；硬件准入未认证。 |
| Einstein：诊断init | 精确0022、实际main提取测试、独立Python入口；主代理确认全量重建和实际镜像init一致。测试初稿依赖evidence路径且会被base glob错误编译，退回后移动到peripherals-next/tests并固化可重现入口。约19:44–19:59。 |
| Franklin：独立打包 | 新R26打包器、13项主机测试和实际旧卷/隔离Mu证据；主代理纠正最初卷角色说明并要求实际读取，之后又修正support.kernel_built元数据判断。约19:47–20:04，二者均在冻结前处理。 |
| Darwin：实际Stage1测试 | 新增4890场景并重跑原351；主代理复查adapter仅在显式CFG0场景适配旧mock硬编码，其余断言保留。最初adapter只在evidence，退回固化到maintenance并加入来源hash。约19:47–20:08。 |
| Dewey：独立侧审 | 复现完整init/default/错误路径、adapter负对照、来源闭包；主代理核对其指出的首读失败与next顶层遗漏，保留限制并合并证据。约20:03–20:14。 |
| 主代理 | snapshot/共享接口/精确source helper/正常构建/全部回归/冻结读回/报告与checkpoint。上述时间为活动记录近似区间，不作精确工时。 |

各代理写入范围独立，共享文件只由主代理整合；没有并发写同一生产文件或git合并冲突。主代理在落地时反馈三项具体交付问题并复跑整套集成测试。所有代理已关闭，无手机操作授权下放。

## 下一次上机的目标与判据

仍须用户对本轮查询和单次临时启动明确确认。只测试此固定R26，使用独立guard/私有副本与USB接收，不调用携带旧全局ready状态的普通boot入口，不刷分区，不进Android/Recovery。无需用户守屏或滑动。

期望日志：

```text
LITHIUM RUN A ... affinity=0x1 ... match=1
LITHIUM TOUCH stage=1 clock_plan ... hid=0 ... rcg_write=0
LITHIUM TOUCH stage=1 phase=enable_clocks ...
LITHIUM TOUCH stage=1 phase=qup_snapshot ...
LITHIUM TOUCH stage=1 qup state=... version=... i2c_status=...
LITHIUM TOUCH stage=1 end status=observed reason=snapshot_complete_not_touch_verified ...
LITHIUM TOUCH session state=ready pid=1 ram_only=1 timer_seconds=180
LITHIUM TOUCH session state=wait owner=kernel_loader_return_after
--- loader_return_after expired ---
LITHIUM USB: timer boundary drain=1 ...
```

`clock_poll_cap`表示尚未观察到允许的ON/ROOT_OFF状态；`root_or_reset_changed`表示复读不稳定；任何stopped/blocked或异常都保留日志、停止，不自动重试或强改硬件。session blocked需检查具体reason。快照成功也不是物理触摸通过。只有timer/drain正文及实物终态一致，才能确认新收尾路径实测通过。

软件300ms预算/2048访问/500次轮询只约束能返回的操作。时钟投票可能恢复遗留引擎活动；永久MMIO/屏障、external abort、SError/XPU或映射停滞不能由此恢复，未装通用异常hook，不承诺自动回滚。动态所有权、rails/NoC、真实clock/pin/QUP访问仍未验证。

R26上机通过后才设计最小I2C芯片ID读取；GPIO105/125物理线、控制器ID/固件/报告、复位/供电、IRQ/HID和可操作桌面仍分项记录。当前不推断其中任何一项已通过。

## 保存和回退

开工前312项文件快照及Git状态位于 `evidence/touch-r26-20261119/main/before/`；R15/R20/R25镜像与合同保持原字节。新镜像/Apple及厂商二进制只留忽略目录，不入Git或共享补丁。

同名JSON记录实际hash与证据。`reports/hakios-ios16-touch-r26-prepared.patch`仅导出本轮源码/脚本/报告/状态差异，按实际工作前快照验证正向应用及反向还原；不是对任意历史clean checkout独立可用的补丁。旧dirty/staged内容和Git index保留。下一次设备操作仍受AGENTS/OPERATIONS逐项确认约束。
