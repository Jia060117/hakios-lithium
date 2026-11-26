# R35 combined — 三核运行已证实，触摸在初始化前被拦截

2026-11-20 18:14:16.880545+08:00 单次临时启动被接受，累计 HakiOS 62 / iOS16 31。
镜像 `bfc3a041b7d81f33582dae04ba9894c53b3b44f16e1b5f70ba10b1672500370d`；原五项只读预检及一次 boot 记录保留。

现有触摸初始化检查同时要求 affinity=1 和 max_logical=0。
本轮后台线程实际位于 affinity=0x101、max_logical=2，因此打印
`LITHIUM TOUCH status=blocked reason=cpu_contract` 后返回，没有启动真实触摸。
这是本次集成遗漏，不能用本轮“点不动”判断三核触摸延迟或芯片无应答。

| affinity | 整数平均 µs | memcpy MB/s | SCTLR I/C/M |
|---|---:|---:|---|
| 0x1 | 2491 | 3534 | 1/1/1 |
| 0x100 | 162535 | 110 | 1/1/1 |
| 0x101 | 162603 | 110 | 1/1/1 |

三个核心均有在线记录、逐核 logical_match=1，五样本均 errors=0。
慢核现象仍在；启用位不能证明 cache 命中正常，也不是实际 MHz 测量。
定时器 CNTFRQ=19200000 是计时频率，不是 CPU 频率。没有修改频率、电压或缓存策略。
SB 已出现 constructor/main_queue 日志；真实屏幕及 UI 可用性未由本轮目视确认。

截至 2026-11-20T18:29:41.575849+08:00，只从主机保存文件取得连续前缀：
11842 帧 / 2454508 console 字节，
独立重放一致、缺口/重复/残帧/device-loss 均为零。
这是运行前缀，不是结束日志；没有取得本轮 timer/drain 或物理终态。
接收器保持原运行，不额外查询、重启或重试手机。

下一候选为 R36：显式 touch_smp=1，仅把触摸初始化与每次轮询绑定至
logical0/affinity1，保留三核调度与 quiet/lite。慢核与桌面性能尚未解决。
运行原始证据：`reports/local/20261120-r35-ui-perf-combined-attempt2/`。
