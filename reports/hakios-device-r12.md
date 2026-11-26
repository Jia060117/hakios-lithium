# HakiOS lithium 第12轮同包复测

第12轮同次结束照片确认硬件计时660003ms，PPI27由0增至39518；11次采样停滞0次、最小增量2992，最后timer样本650128ms/38927。select600次成功、errors0、elapsed_ms=602276、nvcsw=600、nivcsw=6，PID1保持存活并正常定时返回；同一镜像连续合格计数为3/3。 原图、哈希和冻结输入见 [机器记录](hakios-device-r12.json)。本轮实际临时启动时间为 2026-11-15T15:46:25.417080+08:00，此前已核验授权MIX身份、解锁和userdata容量250710192128字节。

| 观测 | 同次照片中的实测值 |
| --- | --- |
| 硬件计数器 | 起点1719575884，终点14391644451，频率19200000Hz，valid1 |
| 硬件时长 | 660003ms，起止差值重新计算一致 |
| PPI27 | 起点0，终点39518，增量39518 |
| 采样 | 11次，stalled0，min_step2992 |
| 最后timer样本 | 38927 IRQ，heartbeat66，650128ms |
| select | wakes600，errors0，602276ms |
| 切换 | nvcsw600，nivcsw6 |
| 返回 | 正常定时返回，PID1保持存活，测量摘要保留 |

原图SHA256 `d13979e4f29e19b5790f42d76aada49b7e9eb3b2d3e0b15393fd04a54356caf2`；返回屏显示日志18713字节。原始RAM日志未读回电脑，照片不等于完整日志。控制台字符归一并保留物理换行转录；跨行的ticks_end、irq_delta、nvcsw与LOADER逐行连接核对。未见panic原因行，返回原因明确为KERNEL TIMED RETURN OBSERVED。

硬件计时从EARLY_BOOT计起，hello有自己的起点；两者共享SoC时基，不宣称独立外部计时。最后timer样本与最终IRQ统计为独立时刻。验收依据同次照片实测数值、采样增长和实际等待/切换，未依据参数、Fastboot接受或上轮结果推算。

镜像SHA256 `0c47097279c73ff89a024da8ce499da1f846fcb8529bffeb2852deab878604de`，内核SHA256 `f66cdf17db064cba0dc5cf37209f691db8b1b1cef975715f7b45f6a37b5d6105`。冻结boot解压后与冻结shim逐字节相同；DTB、payload、内嵌内核、hello、loader和命令行核验一致。全部成品与第十轮相同，完整HakiOS固定提交63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2。

合格轮次为R10, R11, R12。P1连续三次门槛已满足，下一项沿现有台账推进P2/P3，iOS14兼容与图形仍待验证。 本次只整理证据和核验输入，没有新boot或构建。累计12次HakiOS临时boot，无分区写入、旧7195或QEMU执行。hello只验平台，iOS14、真实UFS、GPU、触摸和SpringBoard未验证。照片时停在结束摘要，尚未确认回Fastboot。
