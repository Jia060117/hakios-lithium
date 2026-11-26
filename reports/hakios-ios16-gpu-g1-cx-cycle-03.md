# GPU G1 CX本地使能/恢复03 · 2026-11-26

一次隔离RAM启动`2026-11-26T00:54:43.644324+08:00`，镜像`51810ee0c4006aa2781bacebc14967c4ca97d81c734e7a6315f9b2b5c5c9f730`。
CX上电观察`True`，本地配置恢复`True`，生命周期`True`，本阶段通过`True`。

| 时刻 | CONTROL 0x8c4034 | STATUS 0x8c4038 |
| --- | --- | --- |
| 初始 | `0x222001` | `0x5ff0` |
| ON | `0xa0282000` | `0x80001fe0` |
| 退出 | `0x222001` | `0x5ff0` |

只写CONTROL，STATUS只读；ON观察要求硬件STATUS31=1及本地collapse0。
本地原软件位恢复mask`0x00fff001`，不强制votable域的全局PWR_ON归零，
不覆盖其他请求者、不循环关断。实际每次写入轨迹保存在JSON中；只有ON与恢复均成立才认可完整2或4步。
16读/4写/548µs，1映射/1释放。
error/cleanup=`0/0`，dirty/uncertain=`0/0`。
没有GX/clamp/PLL/RCG/分支、PMIC或RPM资源票操作；未读GPU/SMMU。
本轮CX本地请求结果按实际字段判断，不声称完整GPU上电、芯片ID或GPU加速。

180秒正常timer/四stop/source0/drain及唯一同机Fastboot终点`True`。
USB独立逐字节重放251帧/69999raw/61967console；
接收器`capture_failed`/`[Errno 32] Pipe error`保留，返回核验`2026-11-26T01:00:55.554110+08:00`。
原日志未改，派生视图只移除完整温度记录并保存原偏移。
主代理实际构建完整HakiOS、镜像读回、上机及USB审计；
Aristotle完成实际adapter专项和ARM64，main另有24个实际参数ASan/UBSan用例。
CPU仍614.4MHz配置模型、R20显示，RAM包无物理UFS。
下一是源支持的GFX/GX/133MHz链路；MX850mV数字ready仍不冒充已证明MX4。

失败位置与原有回调读回（未新增硬件读写）：
