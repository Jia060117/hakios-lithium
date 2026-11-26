# Lithium A530 GPU 推进 · 2026-11-25

当前已完成 GFX **ENABLE → PMIC ready → 恢复关闭**，以及真实 RPM VERSION/双向OPEN/OPEN_ACK，均核实自动返回 Fastboot。最新OPEN03于23:12:31接受、23:17:03回Fastboot，总计104 HakiOS /73 iOS16。CPU保持614.4 MHz配置模型、R20显示。MX/bus资源请求和GPU芯片ID尚未执行。

## 已完成的真机步骤

| 阶段 | 同次硬件结果 | 尚不能据此声称 |
|---|---|---|
| G0 | GPUCC/GCC 双遍状态一致；CX/GX 和分支时钟关闭 | GPU寄存器可安全访问 |
| G1供电/fuse前置 | 本机完整64bit fuse与两S2数字配置；GFX关闭、MX开启 | 模拟电压、供电settled |
| G1权限 | GFX写入EE0、MX归属EE4；GFX命令通道CONFIG0/STATUS1 | MX可由APPS直接写 |
| **G1 GFX循环01** | **两次PMIC写均ACK、STATUS00→81、原配置恢复、自动Fastboot** | 完整GPU上电、芯片ID、CP或Metal |

GFX循环实际启动时间为 **2026-11-25 22:10:29 +08:00**，22:14:39 一次 devices/product 核验同一手机回到 Fastboot。没有请求用户按键恢复。

120条observer读取、2次PMIC写入，总计16,980次窄地址访问、30,099µs。所有阶段和清理error0，原值恢复后没有遗留或不确定状态。未改电压、MX、GDSC、GPU时钟，没有读取GPU aperture或SMMU。

284帧 / 79,286 raw bytes / 70,198 console bytes独立重放一致，0缺口/重复/残帧/device lost，源日志丢0、正常180秒timer、drain1。Pipe error终态保持原样。四CPU传感器本次峰值46.4/47.0/43.5/44.5°C，**不是GPU温度**。

完整结果见 `hakios-ios16-gpu-g1-rail-01.md/json`。计数100 HakiOS / 69 iOS16。

## 当前代码与验证

- 默认关闭 `gpu_rail_cycle=1`，只在显式 `gpu_stage=1 gpu_probe=1` 与隔离RAM、614.4配置下运行，拒绝触摸及其他PMIC诊断混用。
- 来源绑定的 `STATUS=0x1708 / mask0x81` 只用于已实际确认的TYPE1c/SUB09。QTI authored源码由固定第三方镜像取得，未把该镜像标成官方发布源。
- `g1-status-core.h`负责使能和原值恢复；`g1-rail-adapter.inc`将写入范围限制为SID3 ENABLE80→00，不重放含糊命令。
- 主代理实际adapter测试覆盖正常、默认关闭、复用拒绝、写入归属变化、MX未ready、ready超时、命令拒绝与六处映射失败；另有19个参数专项。完整HakiOS已实际链接。
- 新内核 `d2ab3bc3…`、镜像 `e88d93a5…` 已冻结，Android/FAT/XNUP/APFS及最终嵌套FV读回通过，无物理UFS provider、Fastboot DXE精确。
- 目前继续实现默认关闭的只读RPM通道采集，用实际TOC、FIFO和继承索引决定下一次MX/bus请求。不把host transport模型当实际ACK。

## 并行执行和复核

Aristotle完成STATUS来源绑定与循环core，主代理接入真实adapter、构建打包并操作手机。Aristotle在恢复后做了小范围源码复核，没有重复重跑全部测试；主代理复核实际源码hash、MMIO轨迹与USB原始帧。

Gauss的打包/runner草稿保留，先前因子代理服务配额中断；主代理接管并修正WSL/Windows报告路径准入后完成实际读回和启动，没有等待中断任务或重试旧启动。

AIR桥、SMMU host core以及Darwin静态库的既有完成结果沿用11月23日报告；本轮没有将其当作真实shader/GPU合成证据，也没有重建未修改的Mesa archives。后续RPM adapter与GPU专用bus计划分别在独立文件中推进。

## 下一硬件依赖

1. 获取实际RPM GLINK TOC和FIFO继承状态，不清索引、不发送doorbell、不写MX。
2. 建立有ACK的MX corner及GPU专用bus/clock请求，明确退出处理。
3. GFX供电、bus、CX/GX及固定133MHz时钟链成立后才读芯片识别。
4. Zap/PAS13、SMMU、CP/fence；之后连接HakiosGPU ABI和A5xx Metal探针。

新功能默认关闭；614/R20/low01、历史冻结和原索引保持。回退补丁与本轮构建记录位于 `evidence/gpu-metal-20261125/rail01/`。没有分区操作、刷写、ADB、Android/Recovery或QEMU。

## RPM实际推进补充（22:51）

只读通道采集通过后，OPEN01已收到双向VERSION和远端OPEN（channel2），但最终ACK处EPROTO-4，未确认完整通道。182读、23协议存储、4次doorbell、519µs；未发任何MX/bus资源请求。正常180秒退出、source0/drain1、246帧/60651console独立重放与同机Fastboot核验通过，102/71。RX拒绝包尚未留存；下一最小修订补证据并按协议源码修正，不推断TZ拒绝。详见`hakios-ios16-gpu-g1-rpm-open-01.md/json`。

## RPM通道已在实机成立（23:17）

OPEN02的RX原字证明额外OPEN/rcid3/name_len10触发旧单服务拒绝；OPEN03改用有界多通道确认后，实际名称为`glink_ssr`，目标`rpm_requests`五项握手全成立。200读/29协议存储/5doorbell/560µs，资源请求仍0。254帧/62801console独立重放及正常退出、同机Fastboot全通过。总计104/73。下一进入MX和总线前提，而不是再次协商OPEN。

## 总线状态已实机采集（23:45）

一次RAM完成16字段双遍32读/110µs，6映射全部释放；248帧/61325console独立连续重放，正常timer/drain/source0及同机自动Fastboot核验通过。总计105/74。MMSS→BIMC分支enable1而halt1，AHB继承XO19.2MHz配置；BKE未读，不把它当作0，也不把OFF分支定位为某个GDSC。下一最小动作是BIMC本体debug计频与配置恢复，不再重复OPEN，不发原AP投票未知的降票/恢复票。芯片ID、CP/fence、Metal加速仍未验。

## BIMC本体计频已上机（11月26日00:00）

新鲜short/full为434521/6947177，BIMC公式508799377Hz；三项配置恢复True、自动终点True。本次只使用debug计数器，未改电压或资源票；总计106/75。上游BIMC已高于200MHz，下一推进GPU自身电源域/时钟与只读识别。MX原AP投票、chipID、CP/fence和Metal加速尚未验。
