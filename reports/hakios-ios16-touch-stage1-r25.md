# R25：R20 显示基线上的触摸 Stage 1 诊断候选

2026-11-19。本轮只做主机取证、源码、构建和离线检查；没有查询、启动、重启或刷写手机，没有访问手机分区。R20-display 是唯一已验收显示基线；R19 已确认分辨率不正确，归档，不再复测。

一个新的诊断镜像已冻结，主机检查通过，**尚未获准上机，也不是可操作触摸包**。它只准备时钟/引脚并记录 QUP 和 GPIO 快照；没有 I2C 传输、复位、供电控制、IRQ 注册或 HID 事件。动态所有权、供电及 MMIO 安全仍未验证。

## 产物和基线

| 产物 | SHA256 |
| --- | --- |
| R25，16,269,312 字节 | `f0fd106f2146bc176f448c9a694bf4e7ca0247656c323bb68ffe128f123a336d` |
| R20-display，构建前后不变 | `3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82` |
| R15，构建前后不变 | `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55` |

本地产物：`artifacts/hakios/lithium-ios16/phase4-r25-touch-stage1/lithium-hakios-ios16-r25-touch-stage1.img`。同目录保留 `contract.json`、`boot-args.txt`、实际 payload、Mu 构建报告和读回的 loader。镜像含用户私有输入，仅供本机测试，不进入 Git 或共享补丁。

精确的内嵌 payload boot-args：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1
```

loader 还会生成 debug、RAM 根卷和版本前缀；这不是手机已实测的最终 boot-args。按最长可能前缀保守核算，含结尾 NUL 共 303 字节，容量 608 字节。正常 R20 参数不含 `touch_stage`，新内核不会为其分配外围 worker、查询执行核或访问触摸 MMIO；已用实际 R20 payload 字节运行生产入口测试。

CPU 与显示参照保持 `bootcpu=1 cpus=1`、1080×2040 BGR24/stride3240、360×680 points/scale3。历史 R20 日志确认 affinity0x1；本轮没有新的实机核对。证据：`evidence/touch-stage1-20261119/handoff/historical-console-excerpts.txt`，原始 R20 控制台第121行的引用；`session/final-offline-preflight.json`。

## 取证结论

所有原始文件、来源、提交号、命令及哈希保留在被 Git 忽略的 `evidence/touch-stage1-20261119/`。这里只给结论和定位，不复制原始资料。取证先检查已有 ROM、DTS、历史 Android 输入记录和 USB 日志，再补公开源码，没有读取手机。

MiCode 使用实际返回的 `scorpio-n-oss` 提交 `1492c8e4373b7428bb0accc4f4881e97b36508b9`；没有把不存在于返回结果的 lithium 分支当作来源。Linux 对照固定 v6.12 提交 `adc218676eef25575469234709c2d87185ca223a`，Mu 固定 `6b3d90b79e191e08171bfedd3980234a7e49d340`。主内核仍为完整 HakiOS 提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`、XNU8796.121.2/arm64。

| 问题 | 结论及证据 |
| --- | --- |
| 控制器/总线 | stock DTS 声明 FT5x46 家族，I2C12/BLSP2/QUP6，基址0x075ba000、窗口0x1000、地址0x38；历史 Android 输入记录也绑定 ft5x46、i2c-12/12-0038。`board/sources/lithium-stock.dts:4844`、`:4863`、`:18267`；`board/historical-input-touch.txt`。运行时 ID/固件未知，不能把家族名当成 ID0x54；DT 配置0xe7在该驱动里对应 FTE716 命名，见 `board/sources/ft5x46_ts.c:145`。 |
| GPIO105/125 冲突 | DTS 的通用 interrupts 指105，但命名 irq-gpio 和 pinctrl 指125。驱动读取后者，再用 gpio_to_irq，申请下降沿/oneshot，因此125是最有依据的源码候选；物理线路仍未验证。DTS`:4866`、`:4882`、`:14027`；驱动`:3087`、`:3699`、`:3887`。本包只读两者，不注册 IRQ。 |
| GIC 中断号 | QUP 的 SPI106 对应常规绝对 INTID138；GPIO 走 TLMM 汇总 SPI208/INTID240，不能把 GPIO125 当成 GIC INTID125。DTS`:4851`、`:13204`；`handoff/findings.md` 的 IRQ 证据定位。Stage1 不改变 GIC 路由。 |
| 复位/供电 | 复位GPIO89；vdd/vddio同指PM8994 L14，声明1.8V；LAB/IBB与面板有关，禁止按独立触摸电源随意切换。DTS`:4868`、`:4881`、`:6323`、`:8309`、`:8338`、`:18063`。probe延迟工作有10ms后拉高、再300ms等待，其他唤醒/恢复路径时序不同；驱动`:3554`、`:3718`、`:1715`、`:1845`。这些是源码路径，非当前电压或复位历史。 |
| 时钟和引脚是否必须重开 | 不能由静态资料判断当前继承状态。本包只对严格匹配的继承根时钟做窄字段准备，不改PLL/频率/电压/复位。已有正确字段不重复写，条件不符立即停止。实时电源、NoC、访问权限和时钟频率未验证。 |
| UEFI 所有权 | 实际编译来源调查未发现包含的 Mu/loader 有 QUP6/I2C provider，但这不证明 LK/TZ 已释放控制器。动态共享和静默交接未验证。见 `handoff/findings.md`、`handoff/captures.json`、`handoff/manifest.json`。 |
| 参考实现软复位 | vendor 常规传输路径会写 SW_RESET，不适用于这里的继承态观察。本包完全不调用该入口、不复位。即使后续超时，也不能据此证明已有复位权限；不得自动用复位制造可访问状态。`clocks/primary/micode/drivers/i2c/busses/i2c-msm-v2.c:279`、`:2879`。 |

本地 stock boot 独立解析得到20个DTB；index3是唯一匹配已保存身份属性的候选，DTS重新编译后1898节点/属性/保留区语义一致。具体校验和原包路径见 `board/dtb-validation.json`、`board/manifest.json`。F盘清单因受保护目录而不完整，WSL home 清单在记录的范围内完成；没有把文件搜索无结果当作硬件不存在。

## Stage 1 实现与风险

源码：`ports/hakios-lithium/peripherals/touch-stage1.inc:33`、`:86`、`:102`、`:176`；接入：`peripherals/kernel.inc:15`、`:48`、`:66`。全部新行为只由显式 `touch_stage=1` 开启，后台线程执行；stage2/3仍阻止，普通启动不等待诊断。

顺序严格保持用户要求的 **GCC准备和确认 → TLMM准备和确认 → QUP读取**：

1. 检查普通线程、开中断、单核affinity1；原子one-shot防止重入。
2. 只读共享BLSP2 BCR0x00325000和QUP6 BCR0x00330000，要求bit0清零；CMD的UPDATE/DIRTY清零；CFG掩码0x371f必须等于1，即源码中规范XO19.2MHz分频编码。实际频率仍未测。
3. 仅允许AHB vote0x00352004的bit15、I2C apps CBCR0x00330008的bit0；保留其他字段。AHB/CBCR只接受vendor已知ON状态高四位0或2，并检查root-on。再次核对reset/CMD/CFG稳定。
4. 仅允许GPIO87/88 CFG的mux/OE掩码0x23c写为0x0c；保持pull/drive，未写输出、电源、GPIO89或IRQ配置。
5. 读取QUP STATE/VERSION/I2C STATUS以及GPIO105/125配置/电平，输出有界 `LITHIUM TOUCH` 日志。单次电平快照不能裁决实体触摸中断。

寄存器依据：`clocks/primary/micode/include/dt-bindings/clock/msm-clocks-hwio-8996.h:304`、`:333`、`:360`、`:374`、`:459`；`drivers/clk/msm/clock-gcc-8996.c:997`、`:1937`、`:2070`；`drivers/clk/msm/clock-local2.c:668`；TLMM见 `board/sources/pinctrl-msm8996.c:29`、`:1245`、`:1825`。个别QUP BCR地址用固定Linux源码交叉确认，不误归为vendor头文件定义。

独立时钟复核提出三点：共享BCR检查、严格ON状态、先读QUP再改mux并先证明静默所有权。前两点已采纳并测试；第三点的QUP/mux次序与用户明确要求相冲突，本包保留用户次序，**不声称因此证明无副作用或得到复核者的live admission**。开启时钟可能恢复遗留RUN/PAUSE/FIFO活动，OE=0不等于外围绝不驱动引脚。静态取证不足以消除该风险；本包只是等待用户确认的实验性诊断候选。

300000us软件预算、2048次访问、每轮最多500次轮询/失败轮询间隔1us，只约束能够返回的操作；不是对总线交易的硬件超时。MMIO/屏障永久不返回、同步external abort、异步SError/XPU，以及ml_io_map分配停滞均无法由这些检查兜底。ARM64 `osfmk/arm64/loose_ends.c:739`、`:748` 中ml_probe_read两个版本均会panic，本包不调用，也未装通用异常恢复hook。证据：`session/ml-probe-read-source.json`。

失败后记录原因并退出；可能已保留时钟vote/branch或部分pinmux修改，不尝试未经验证的自动回滚。发生总线挂死或panic时可能没有end日志，180秒loader计时也可能无法抢救。用户届时应手动长按电源＋音量下进入Fastboot，保持USB并保留日志；不要进入Android或Recovery，以免其处理现有userdata。软件恢复和真实硬件副作用均未验证。

## 隔离现有 UFS 数据

仅设置 `ufs=0` 不够：原Mu的BDS先递归连接BlockIO，Partition/FAT可能读取分区起始扇区；这发生在loader解析参数之前。R21虽拒绝写入，其Mu仍有物理UFS provider，不能称为“只读GPT”或“不接触userdata”。证据定位见 `handoff/findings.md`。

因此R25重新构建独立Mu组合：只在外部工作目录的DSC/FDF移除LithiumUfsDxe，保留RAM FAT所需驱动；实际FV map没有其名称/GUID，独立新目录没有生成其EFI。普通板级源码和R20/R21产物未被改写。实现：`scripts/package-hakios-ios16-touch-stage1.py:46`；实际来源、命令、FV map哈希在产物contract的ram_mu字段。

loader逐字节复用已冻结的R21只读版本，移除持久日志分区写入路径、拒绝write/flush；不启用native UFS。payload移除root_apfs、var_hfs、ufs_write及存储基准等参数，只保留两个与R20逐字节相同的4MiB RAM引导卷。新内核没有ufs_shared_pa，UFS初始化在设备注册前返回。没有新加阻塞launchd的内核屏障；剩余init可以继续RAM路径，其用户态结果未验证。**本包不用于验证SpringBoard/桌面或触摸操作，也不改变系统长期存放UFS的路线。**

这是包含的软件路径隔离证据；Mu之前厂商固件/TZ的行为未验证，不能据主机构建承诺整个硬件生命周期绝无存储访问。

## 构建和测试

主机环境：WSL16GiB/12逻辑CPU/swap2GiB；构建使用 `config/hakios-resources.json` 的任务限制和12并发，大任务顺序执行。

| 检查 | 结果/证据 |
| --- | --- |
| 正常prepare/config/deps/source review | 通过；83项来源/装载边界。`session/prepare-reviewed.log`、`config-reviewed.log`、`dependencies-reviewed.log`、`source-review-reviewed.log`。 |
| 完整HakiOS内核/D22AP loader/Mu/用户空间 | 通过；413个launchd job构建检查。本包仅选用新内核、独立RAM Mu和明确固定的只读loader。`kernel-build-reviewed.log`、`support-build-reviewed.log`、`userspace-build-reviewed.log`。 |
| 实际链接/指令检查 | 46项扫描通过；真实hakios.input.d包含Stage1，链接有worker和lts1，未链接旧MMIO恢复候选。`session/actual-linked-integration.json`、`build-check-reviewed.log`；仓库报告 `reports/hakios-ios16-touch-stage1-r25-build-checks.json`。新RAM Mu另外检查5个相关EFI模块。 |
| 外围完整回归 | 15项既有/新增基础套件＋16项后续套件，31套通过ASan/UBSan；其中Stage1为实际production include的351个场景。`session/all-peripheral-tests/passed.json`及`previous/passed.json`，不能只看父级哈希；打包检查也核验子级源哈希。 |
| 显示实际函数 | 10项flattened DT测试通过。`session/display-regression-final.log`。第一次直接调用因遗漏JOBS环境被门禁拒绝，记录在display-regression-reviewed.log，按既有资源配置重跑成功。 |
| 历史总回归 | **未通过**：R9快照不认识此前0021几何补丁，在userland/src/inject.c哈希处停止，后续未执行。仅在临时副本反向应用固定哈希0021后，字节哈希准确恢复R11预期值；未修改生产代码或放宽历史测试。`session/system-regressions.log`、`old-r9-gate-diagnosis.json`。不能宣称全部历史总回归通过。 |
| 冻结/读回/默认关闭 | 通过Android头/摘要、gzip/DTB、Mu、FAT/EFI、XNUP/内嵌命令行、真实内核与RAM卷核验；再次运行R20原参数入口、构建前后基线哈希核验。`session/package-freeze.log`、`final-offline-preflight.json`。 |

正常命令：`scripts/run-wsl-limited.ps1 -Task hakios-prepare`、`hakios-check`、`hakios-deps`、`hakios-r19-review`、`hakios-kernel`、`hakios-support`、`hakios-r19-userspace`、`hakios-r19-build-check`。其中r19名称只是既有wrapper映射到通用当前源码检查/构建的别名，没有重建R19包或进行R19验收。外围回归命令为 `python3 -B scripts/test-hakios-lithium-peripherals-next.py --compiler clang --build-dir evidence/touch-stage1-20261119/session/all-peripheral-tests`。

冻结命令为在相同systemd资源限制、venv和JOBS环境内运行 `scripts/package-hakios-ios16-touch-stage1.py`。它拒绝覆盖既存输出和外部Mu目录；不要直接重跑或另开候选。所有主机检查只证明被检查的源码、字节和模型，不证明电气、真实IRQ、总线完成或触摸可用。

## 并行执行和主代理复核

| 分工 | 实际交付 | 主代理复核/整合 |
| --- | --- | --- |
| Newton：板级取证 | `board/findings.md`、DTB独立解析/语义比较、公开分支确认和输入记录。 | 抽查DTS节点、供电phandle、驱动GPIO读取/gpio_to_irq/下降沿申请真实存在；没有照抄兄弟机型参数。 |
| Pascal：时钟/引脚复核 | `clocks/findings.md`、固定提交的vendor/Linux源码及哈希。 | 抽查共享BCR定义、vendor branch ON判定；采纳两项修正。保留其所有权/次序异议，没有冒称获得硬件准入。 |
| Epicurus：交接/包路径 | `handoff/findings.md`，Mu实际包含来源、历史核号、只读R21路径。 | 确认R21仍可读分区的缺口；据此新建无物理UFS的Mu，核验实际FV输出。其未认证主代理后续构建，构建由主代理独立核验。 |
| Dalton：实际include测试 | `maintenance/hakios-lithium/peripherals/touch-stage1-test.c:1`、351个模拟场景和verification证据。 | 抽查所有16种clock nibble、后置root/reset突变、写掩码/顺序；主代理重新运行完整31套并复核源哈希。 |
| 主代理 | 生产接入/Stage1、两项clock修正、正常构建、隔离Mu/打包、读回、报告和补丁导出。 | 确认真实.d依赖、链接符号、镜像各层字节和R15/R20哈希；没有设备操作。 |

分工使用互不重叠的文件范围；共享生产源码仅由主代理修改，测试代理只写其C测试/证据。没有共享文件合并冲突；两项时钟问题修回后由同一测试代理增加覆盖，主代理重跑；发现Mu探盘问题后更换组合，没有修改旧包。代理工作完成后已关闭。没有单独计每个代理的墙钟耗时，不能给出精确工时；各证据manifest/命令记录保留形成时间，整个本轮从10:32:32UTC开始、在13:32:32UTC截止前收口，实际完成时间见同名JSON。

## 未验证与下一步

动态QUP/TZ所有权、继承engine是否静默、MMIO/SError实际表现、rails/NoC/时钟率、GPIO105/125物理接线/波形、运行时芯片ID/固件、I2C收发、IRQ/HID/backboardd投递，以及本包RAM init能否持续到180秒并正常返回，均未验证。真实长阻塞无法用主机mock证明安全。

已完成一个冻结诊断候选，不开新R轮次、不查询设备。下一步等待用户明确说“可以上机”，并先说明具体查询/临时启动操作、风险并获得对应确认。不会启动Android/Recovery或执行任何分区写入；R20原图不拿来代替这个隔离探测。

上机若获准，期望采集 `LITHIUM TOUCH stage=1 begin/phase/field/clock/inherited/qup/gpio.../end`、CPU affinity和正常返回日志。`inherited_root_or_reset_unverified`或`root_or_reset_changed`表示条件不符并停止，不能反复强开；clock_poll_cap表示未观察到允许状态；snapshot_complete只表示快照结束，不是触摸通过。只有GPIO单次快照不足以做触摸动作关联，不要求用户守屏或持续滑动。

回退产物：R15/R20固定路径与哈希保留。同名checkpoint补丁只导出本轮列明的源码/报告变化；开工前已有改动保留，既有未变化依赖仍按原工作区及前序checkpoint使用。导出时在临时目录验证正向应用和反向还原，不能把补丁盲目应用到已有相同修改的工作区。没有动Git index、reset/clean/add-A或Git配置。原始证据和所有厂商/Apple二进制均不进入补丁。
