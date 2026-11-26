# R37：首次有效频率测量与自动 Fastboot 返回状态

日期：2026-11-20，Asia/Shanghai。最新修正包仍未上机；本文将实际测量、已发现的源码问题和待验证项分开记录。

## 实际测量

`r37-frequency-02` 于 22:27:53.190178 临时启动一次。三核在线，PID1 为隔离 RAM hold；没有运行 SpringBoard，也没有访问物理 UFS。完整 boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_freq_diag=1
```

| 物理 affinity | CNTFRQ | 有效 CPU MHz | 样本 | 说明 |
| --- | ---: | ---: | ---: | --- |
| `0x1` | 19,200,000 Hz | 未验证 | 0 | 继承 enable `0x2a`、interrupt `0x31`，原策略拒绝接管 |
| `0x100` | 19,200,000 Hz | 未验证 | 0 | 继承 enable `0x4`、interrupt `0x8000007f`，原策略拒绝接管 |
| `0x101` | 19,200,000 Hz | **19.193 MHz** | 5 | min/avg/max 均为 19.193 MHz，控制位恢复读回成功 |

`0x101` 输出的边界为 19.119–19.266 MHz；考虑输出向下取整，外包络为 `[19.119,19.267)` MHz。每个样本约 1 ms，测量以 CNTFRQ 标称值校准，并包含中断时间，不是外部仪器测量。

这支持性能簇至少一个核的有效执行速率接近 19.2 MHz，但**不证明其 PLL/mux 实际选择了 XO**。另两核的 `valid=0` 和零统计不表示 0 MHz。非空 PMU 掩码的来源和所有权没有由本轮证明，不能简单称为“过期配置”。

## 返回结果与用户更正

本轮捕获正常 timer、`return_cpu logical=0 affinity=0x1`、armed 和首个 USB `drain=1 target=12262`。52 帧、12,311 console 字节独立重放一致，无缺口、重复、残帧、源端 lost 或争用丢弃。接收器在 22:31:25.042401 以 Pipe error 结束，该状态原样保留。

22:32:34 的唯一一次 Fastboot 枚举为空，因此本轮**没有自动返回 Fastboot**。不重查、不重复启动。随后用户提供的同轮照片确认停在 UEFI 正常 timer 摘要，明确显示 `LITHIUM FASTBOOT STATUS RETAINED REASON SMP RETURN AFFINITY`，以及 `drain=1 target=12262`，与已保存 USB 日志对应。旧 loader 在核身份检查处直接返回，未执行后续固件 Prepare/Commit；不能把这次失败归因为 PMIC 复位故障。

原图已完整归档至 `reports/local/r37-attempts/r37-frequency-02/screen-terminal-original.jpg`，SHA256 为 `420ae5949f74d6ec3a0d6bf08ee66d1f1a17f5a06ed23f4392b470bc2acc513e`；人工读图记录见同目录 `screen-terminal-evidence.json`。照片中的 retained raw bytes 为 34,734，与 USB 的 12,311 字节属于不同日志范围，不相减判定丢失。照片归档没有新增手机查询或启动。

此前 `r37-frequency-01` 在 22:27:25 查到同序列号和产品，但用户明确说明那是手动重启进入 Fastboot。其记录已更正为手动恢复，绝不计入自动返回成功。更早的 R37 RAM 尝试也没有有效测量。

本轮原始记录：

- `reports/local/r37-attempts/r37-frequency-02/console.log`
- 同目录 `console.frames`、`usb-audit.json`、`commands.json`、`return-verification.json` 和 `result.json`
- `evidence/cpu-r37-20261120/clocks/r37-frequency02-runtime-review.md/json`

## 本轮已修的主机问题

1. **搬运长度遗漏。** 早先 R37 打包器沿用 R34 的 BootShim 搬运长度，新 payload 尾部 34,368 字节没有搬完整。修正为更新唯一长度 literal，再对实际 Android 解包结果模拟整段搬运，逐项比较 kernel、root、resident 和 cmdline。旧包原样保留，不再启动。证据：`return-fix/shim-copy-length-audit.json`；实现：`scripts/package-hakios-cpu-r37-ram.py:125`。
2. **RAM PID1 生命周期。** `return-fastboot/init.inc` 加入独立的 `ram_smp_return=1` hold，等待内核 timer，避免落入通用自检或提前 halt。实际编译 init，APFS clean，486 项逐项读回一致；旧 R34 根卷未覆盖。
3. **次核停止证明。** 新独立补丁 `patches/0027-lithium-strict-smp-return.patch` 增加严格 PSCI OFF 读回。只在显式 SMP 自动返回路径使用，失败不恢复 UEFI；普通默认行为保持。timer 在仍可调度时先绑回 XNU 引导核 `0x1`。
4. **UEFI 核身份。** 原三核 loader 错误要求 `after_return` 在 XNU 引导核 `0x1` 执行；实际 `run_on_other_cpu` 在原 UEFI 驻留核上继续。用户照片已确认旧检查正是本轮实际终止点；旧构建源码第 94–96 行与照片的 `smp_return_affinity` 对应，源码副本见 `evidence/cpu-r37-20261120/frequency02-photo/frequency02-loader.inc`。新源码在 `return-fastboot/loader.inc:36` 记录 seed 时的 UEFI affinity，在第 96 行核对同一 owner，再严格确认三个 XNU affinity 已 OFF。修正已编译进 frequency03，**新包自动返回仍未实机验证**。
5. **采集器。** 命令记录改为追加；捕获上限覆盖返回窗口；设备缺席时不再发送会等待的 product 查询。每个阶段和包使用独立 attempt ID，旧 guard 不删除、不复用。
6. **补测 PMU。** 默认关闭的 `cpu_freq_pmu_borrow=1` 在受限前提下临时借用已全局关闭的 PMU，保存/恢复控制位，不复位计数器、不清 overflow、不改 LC、PLL 或电压。`0x100` 若仍继承周期中断使能，将以 `cycle_interrupt_unbounded` 拒绝，不承诺测齐三核。

## 主机验证与并行记录

- 主代理完成严格停核观察、timer 绑核、PID1、BootShim 搬运、UEFI owner 修正及整合；实际 XNU、loader、Mu 编译成功。
- Epicurus 的停核补丁草稿未通过 diff 语法检查，已退回并保留草稿；没有用于构建。主代理恢复原 R33 补丁哈希，另建 `0027`。
- Bernoulli 完成 runner 主体。初次用例未跟上新合同；主代理修复夹具和真实 Fastboot 空白匹配后，7 个用例通过。
- Hilbert 完成只读时钟审查与本次测量复核；主代理重新读取实际频率行、源引用和完整 USB 流。
- Huygens 完成 PMU borrowing 与专项：ASan/UBSan 2,256（audited）、2,176（audit off）、2,176（NMONOTONIC）检查。主代理独立复跑 2,256 检查，并实际编译进新内核。没有已观察到的共享文件冲突。
- 其他专项：实际严格停核函数 175 场景；实际 loader owner/PSCI 门禁 15 场景；RAM profile 46 场景。未重跑无关的历史总回归。

常用验证命令：

```text
python -B -m unittest scripts/tests/test-r37-ram-runner.py
wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/tests/test-r37-strict-return.py
wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/tests/test-r37-loader-owner.py
```

构建采用 14 GiB/12 CPU 的既有 systemd 限额；内核入口为 `scripts/build-hakios-cpu-r37.py kernel --revision strict-return02`，配套支持为 `scripts/build-hakios-r37-ram-support.py`，根卷为 `scripts/build-hakios-r37-ram-root.py`。对应命令日志与哈希保存在 `evidence/cpu-r37-20261120/main/`、`return-fix/`、`ram-support-return02/`。

## 新包和接下来的上机顺序

新内核 SHA256：`2a65728bf0520d6c4a01ca739e68b76a97c52740050bd41cd53c9e0a9c77f290`。以下包已构建和读回，**都未启动**：

| 顺序 | 包 | SHA256 | 唯一诊断用途 |
| --- | --- | --- | --- |
| 先测 | `phase4-r37-ram-frequency-03` | `2b55d7ec6df743009ac2b9ed5805c695f1b9fbfd22d863b6105f1f601daf17b6` | 频率及 PMU borrowing；验证修正后的自动返回 |
| 返回成功后 | `phase4-r37-ram-timer-02` | `5d384b4d5bf73f5a92db87858aacd187de5401ebd10cb7d673bbb54019c81ef2` | 仅计时迁移，不同时启用 PMU 测量 |

各包 `boot-args.txt` 为完整精确参数。频率包相对上文参数增加 `cpu_freq_pmu_borrow=1`；计时包去掉两个 `cpu_freq_*` 参数，单独加 `timer_migrate_diag=1`。两者都是 `ufs=0`、180 秒、`return_fastboot=1 return_fastboot_delay=10`。

结束画面已保存，实际停点已确认；下一步由用户手动恢复 Fastboot，随后直接测试已备 frequency03，不重复构建或无关测试。没有设备重新就绪的证据前，不发起新启动。正常 timer、两次 drain、严格停核、owner、固件复位任一环节失败仍保留摘要，不承诺异常情况下强制复位。

六项目标状态：频率部分完成；跨核计时未上机；SpringBoard 调度归因未验证；`slowcores_idle` 只有主机实现；时钟/电压来源为只读源码证据，实际选源/电压未验证。UFS 自动返回仍缺少真实写入封存与 HFS journal 证明，RAM hold 不能替代桌面负载。**尚不设计固定频率方案，更不写动态调速器。**
