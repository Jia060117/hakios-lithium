# GPU G1 CX本地使能/恢复02 · 2026-11-26

一次隔离RAM启动`2026-11-26T00:45:41.606260+08:00`，镜像`6a00e64124f76b4d5a6df3a34a38bdbb54137d26d144f2cc31368fcfb0e5ad18`。
CX上电观察`False`，本地配置恢复`False`，生命周期`True`，本阶段通过`False`。

| 时刻 | CONTROL 0x8c4034 | STATUS 0x8c4038 |
| --- | --- | --- |
| 初始 | `0x222001` | `0x5ff0` |
| ON | `UNREAD/INVALID` | `UNREAD/INVALID` |
| 退出 | `UNREAD/INVALID` | `UNREAD/INVALID` |

只写CONTROL，STATUS只读；ON观察要求硬件STATUS31=1及本地collapse0。
本地原软件位恢复mask`0x00fff001`，不强制votable域的全局PWR_ON归零，
不覆盖其他请求者、不循环关断。实际每次写入轨迹保存在JSON中；只有ON与恢复均成立才认可完整2或4步。
8读/2写/79µs，1映射/1释放。
error/cleanup=`-5/-4`，dirty/uncertain=`1/1`。
没有GX/clamp/PLL/RCG/分支、PMIC或RPM资源票操作；未读GPU/SMMU。
本轮CX本地请求结果按实际字段判断，不声称完整GPU上电、芯片ID或GPU加速。

180秒正常timer/四stop/source0/drain及唯一同机Fastboot终点`True`。
USB独立逐字节重放247帧/68836raw/60932console；
接收器`capture_failed`/`[Errno 32] Pipe error`保留，返回核验`2026-11-26T00:49:58.221110+08:00`。
原日志未改，派生视图只移除完整温度记录并保存原偏移。
主代理实际构建完整HakiOS、镜像读回、上机及USB审计；
Aristotle完成实际adapter专项和ARM64，main另有24个实际参数ASan/UBSan用例。
CPU仍614.4MHz配置模型、R20显示，RAM包无物理UFS。
下一是源支持的GFX/GX/133MHz链路；MX850mV数字ready仍不冒充已证明MX4。

失败位置与原有回调读回（未新增硬件读写）：
- phase=`request_on` step=`compare` rc=`-5`；pre=`0x282001` valid=`1`，expected=`0x282000` valid=`1`，after=`0x8282000` valid=`1`，mismatch=`0x8000000` valid=`1`。
- phase=`restore_request` step=`ownership` rc=`-4`；pre=`0xa0282000` valid=`1`，expected=`0x0` valid=`0`，after=`0x0` valid=`0`，mismatch=`0x0` valid=`0`。
