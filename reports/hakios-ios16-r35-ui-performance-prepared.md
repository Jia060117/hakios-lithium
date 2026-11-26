# R35：UI减负与CPU诊断并行修订

2026-11-20，Asia/Shanghai。用户要求两路并行、减少非必要审计和测试；随后确认同次R34运行在应用内可用，触摸位置、滑动、长按正常，但SpringBoard桌面和后台卡片非常卡。本轮完成实际源码改动、增量内核构建和三个同内核配置的打包；未启动R35或查询手机。

## 最新判断与本轮改动

最新反馈支持优先处理SpringBoard场景的CPU/软件合成负载。此前已经看到输入积压；现在触摸恢复可用，说明积压并非始终固定的坐标/报告解析错误。不能从应用内流畅直接推导准确CPU频率或慢核原因。

本次只读已保存R34日志时看到不同窗口包括184帧/5294ms、130帧/5746ms，以及3帧/6375ms。它们是不同场景的运行窗口，没有同步场景标签，不能擅自把每个窗口一一指定为某应用或桌面。有效360秒top窗口曾达99%忙，backboardd65.0%、kernel29.1%，符合容量压力；“只要空闲就好转”的用户反馈不等于已证明调频器有缺陷。

UI线由Huygens完成：新增默认关闭的 `applog_ui_quiet`，只在值为1且进程名精确等于backboardd或SpringBoard时，抑制内核额外注入的 `OS_ACTIVITY_DT_MODE=YES`。保留stderr、普通日志和其他进程行为。caller/job已经提供的环境变量不被删除或覆盖；检查的实际launchd启动env和两个job没有主动提供该值。本改动降低一条明确的同步日志负担，收益需上机验证。

可复现集成在 `ports/hakios-lithium/patches/0025-lithium-ui-performance.patch`，修改上游 `xnu/hakios.patch`，不是仅修改生成目录。主代理prepare后也将同等delta应用到实际 `kern_exec.c`，完成增量构建。

轻量界面直接复用已包含在R34 APFS里的 `libhakios.sb.dylib` 的 `ui_lite=1`：启用系统Reduce Transparency和Reduce Motion，减少Dock/文件夹等透明模糊及应用切换动态效果。主代理读取实际源码819–846、核对实际库有该功能标记；本轮不修改或重编用户态库。

CPU线由Einstein完成：在原有默认关闭的 `cpu_sanity=1` 下，逐绑定在线核记录MIDR、SCTLR_EL1及I/C/M使能、实际affinity、logical_match。保留原整数/memcpy工作量和统计；没有频率、电压、缓存策略或CPU拓扑写入。有限源码核对显示公共启动路径本来就配置I/C/M，未找到足以据此盲写缓存寄存器的缺陷；慢核仍需真机读数定位。这是已编入内核的诊断补齐，**不是已完成调频器修复**。

当前resident UFS设计保留phys0供UEFI存储worker使用，仍最多三个XNU核；没有把cpus=4写入候选。`cpu3`仅启用已有三核路径并采样同一组诊断，不能保证性能提升。

## 已构建的同内核配置

所有配置沿用R20显示1080×2040 BGR24、bootcpu1快核、原UFS系统/Data、真实触摸读取。没有更换系统镜像、刷分区或启用UFS自动Fastboot。内核timer保留10800秒；验收无需等满该上限。

| 配置 | 相对R34 UFS的参数变化 | 用途 |
| --- | --- | --- |
| quiet（打包器默认） | 加 `applog_ui_quiet=1`，仍单核 | 单独对照强制日志回显成本 |
| lite（建议交互改善候选） | quiet再加 `ui_lite=1`，仍单核 | 直接降低桌面/后台切换视觉效果负担 |
| cpu3（诊断备选） | quiet中仅将 `cpus=1` 改为3 | 对比逐核状态和吞吐，判断能否继续多核线 |

这不是要求连续做三轮验收。可以先测试需要的配置，只有结果需要区分时再用对照包；不自动重启或串行启动多个候选。

**lite会修改Data中的辅助功能偏好**，并可能跨启动保留。后续省略ui_lite不会恢复原值；显式 `ui_lite=0` 会把这两个设置设为关闭。它不是仅在当前进程内生效的开关，也不保证恢复用户之前的任意混合设置。普通UFS文件系统写入与此前相同。

共同内核SHA-256：

`86f36fdf4e047e0f2da18138a39629e584975e4c6689036946ae71195bd924aa`

| 配置 | 路径（artifacts/hakios/lithium-ios16/下） | 镜像SHA-256 |
| --- | --- | --- |
| quiet | `phase4-r35-ui-perf/quiet/lithium-hakios-ios16-r35-ui-perf-quiet.img` | `bb8eae118bde5e877510b6ea17ba96e866378eb1e42d29a7320ff67059718511` |
| lite | `phase4-r35-ui-perf/lite/lithium-hakios-ios16-r35-ui-perf-lite.img` | `fda8fd7a18b8f0468c7be483e3080a30a2b9aee97d862fefc077f4c99eced9d3` |
| cpu3 | `phase4-r35-ui-perf/cpu3/lithium-hakios-ios16-r35-ui-perf-cpu3.img` | `74db47b0879e3148f0d3a438dd22cc197f62d96d89d8ae12cf280810c9761475` |

根APFS逐字节复用R34，SHA-256 `38c69bbe8caf11ceb4e90fc6614976ef37a6c2ef51057a119e9c53d0ebe1e71c`。三份完整boot-args、来源及实际逐层读回结果见各自contract.json；608字节command line容量分别保留63/53/63字节。

## 构建和必要验证

```powershell
pwsh -File reports/local/run-ui-perf-r35-build.ps1 -Task prepare
pwsh -File reports/local/run-ui-perf-r35-build.ps1 -Task kernel
```

```text
wsl -d Ubuntu -u jia -- python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-ui-perf-r35.py --kernel /home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.r35 --root /home/jia/ios-mix1-build/hakios-lithium-ios16/userland/root.touch-live.apfs --output-root /mnt/f/IOS-MIX1/artifacts/hakios/lithium-ios16/phase4-r35-ui-perf --profile all
```

仅进行新增quiet选择逻辑的聚焦编译/540组行为检查、打包器语法和profile参数检查、实际内核增量编译链接及三份实际镜像逐层读回。主代理直接检查实际内核含新增SCTLR字段和quiet tunable，核对各profile参数差异。没有重复52套历史总回归、重建Mu/loader或重编未改用户态。

实际增量构建约10.4秒，保留完整构建日志；已有kext symbol-set检查退出2由原构建封装按既有规则处理，内核先成功链接，未删除功能来规避。仅报告实际通过的范围。

主代理独立读回UI common/actual delta、540组检查结果、CPU新增字段及公共启动来源、实际SB库和plist、打包器profile实现、实际kernel markers和image contracts。Poincare负责独立打包脚本；三位子代理写入互不重叠，未发生生产文件合并冲突。主代理负责共享prepare/构建/打包/报告。分工来源分别在 `evidence/ui-perf-r35-20261120/{ui,cpu,package}/`，主代理证据在main目录。

## 当前手机与未验证事项

本阶段没有手机查询、启动、复位、分区命令、ADB或QEMU；计数60/29不变。R20/R32/R33/R34冻结保留，原索引字节保持。主代理在WSL保留了修改前内核及实际源；仓库回退补丁与本报告同名，不含Apple、Qualcomm或Xiaomi二进制。

主机侧发现原USB接收器PID23680已不在运行，但状态文件仍为capturing；没有据此认定手机崩溃或停止，也未擅自重新打开设备。保存的18826帧/4120429控制台字节独立重放一致、无传输缺口/重复/残帧/device-loss，未包含最终timer/drain；终态未知。原状态文件原样保留，另记 `usb-audit-host-stopped.json`。手机当前状态采用用户的“应用内可用”反馈；不再宣称接收器还在实时采集。

R35实际流畅度、quiet日志减负收益、lite效果和偏好持久化、三核状态/吞吐及任何MHz/调频修复都未验证。下一步切换Fastboot后按用户授权选一个候选临时启动；保持现有R34运行直到用户选择切换。
