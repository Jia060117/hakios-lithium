# R38：性能簇时钟实际状态

2026-11-20（Asia/Shanghai），23:13:08.798747临时启动R38 clock-read01一次，复用R37 frequency04 RAM profile。镜像`228c420e81d127ead854bbaf876ab0e89b511a106b88636cf3c6d61401cb8bc5`。没有频率、电压或分区写入。

## 实际证据

日志108–194行，两簇各19寄存器、两遍读回全部相同，另记录droop/SSS/PSCTL原始值与bin word。

| 字段 | pwrcl | perfcl |
| --- | --- | --- |
| 主PLL MODE | 0x81018007 | 0x01000000 |
| 主PLL L | 0x40 | 0 |
| 主PLL ALPHA | 0 | 0 |
| 主PLL USER_CTL | 0x9 | 0x8 |
| MUX | 0x4 | 0 |
| CLK_CTL | 0x40440071 | 0x71 |
| requested PMUX/SMUX | 0 / 1 | 0 / 0 |
| auto select | 0 | 0 |
| 主PLL MODE[31] lock | 1 | 0 |
| 备用PLL MODE/L/lock | 0 / 0 / 0 | 0 / 0 / 0 |

性能簇的PMUX0/SMUX0与auto0，在匹配源码中对应XO；主备用PLL均未初始化/未锁定。它与前两轮phys101的19.193/19.202MHz有效速率相互支持。问题已收窄为性能簇缺少CPU时钟初始化，不能仅靠调度参数修复。仍不把CNTFRQ直接当CPU频率，也不把未测两核记0MHz。

pwrcl主PLL early的标称编程模型为19.2MHz×64=1228.8MHz，但当前main输出postdiv编码0的对应除数缺少现有固定来源证明，未将其称作实际CPU MHz。bin word实读`0x1091005f`，raw speedbin0；CPR revision和供电仍需下一包取证。

本轮PMU phys1在接管后出现`pmcr_readback_failed / overflow_after=0x80000000`，没有有效样本；另两核周期IRQ条件拒绝。恢复控制位标志为1，未清溢出；这不等于完整PMU原始状态无变化。下一包显式`cpu_freq_pmu_setup=0`，不重复接管。APCC零写与PMU控制写分开记录。

## 返回与验证

正常180秒timer、drain1；89帧/21541 console字节独立重放一致，gap/dup/残帧/device_lost均0，接收器Pipe error原样保留。23:17:28唯一devices/product核实同一设备处Fastboot，无请求手动恢复，不以USB断开代替终点证明。

实际HakiOS增量构建/链接、打包逐层读回通过；loader/Mu/root均复用。Mill实现纯解码与33用例/1743断言，主代理独立ASan/UBSan复跑通过，并独立解析实际两轮寄存器值。所有新开关默认关闭。未做不相关历史总回归。

完整boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_freq_diag=1 cpu_freq_pmu_borrow=1
```

原始证据：`reports/local/r37-attempts/r37-frequency-04/`，其中`clock-snapshot.json`逐条保留log行号/raw/字段，另有console/frames/commands/return-verification/usb-audit。

下一步已准备并启动单独供电快照，读取LDO/BHS、SAW数字电压状态、AVS、memacc及完整必要fuse。未证明电源要求前不写PLL或猜电压。四核调度另有phys0 resident UFS占用限制，见`evidence/cpu-r38-20261120/fourth-core/r38-fourth-core-boundary.md`；本次未解决该限制。计数到本轮68/37，后续power01另计69/38。
