# A530 G1 前置真机采集 — 2026-11-23

**G1 的供电/fuse 前置采集通过，手机已正常返回 Fastboot。** 六行完整 fuse 两遍有效且一致，GFX/MX 两路 S2 的七字段各两遍、28 个 READ 命令全部完成，无错误。缺失的 row63 已补齐；按固定小米 stock CPR3 规则计算，133 MHz / corner2 的启动 open-loop 模型为 **560 mV**，启动 floor 为535mV。

**GPU 尚未上电、未读取 chip ID。** GFX 当前保存的数字设定是 **1.000 V**，软件使能位为0；MX 数字设定 **0.850 V**，软件使能位为1。这些是实际读回的数字配置，不是模拟电压或供电 settled 验收。不能直接启用 GFX 的遗留1.000V配置，也不据此认定 MX 已满足 corner4/RPM/bus 前置。

## 同次运行证据

04:37:46.086139 +08:00，Fastboot 接受 `24f53f36277df2ccdd4cb2bc5e4226ea5edda403435820ff55a7a586d5b4bc6a` 冻结RAM镜像一次；实际内核为 `7c63f434fcae955c07299cf135025fb6b349bab854df3e7d8acf2c974ca090e8`。五项只读预检通过，USB 接收器先启动。

实际 G0/G1 worker 为 logical0/affinity0x1，XNU 三核运行。CPU 保持614.4MHz配置模型，未建立新的PMU MHz实测。PID1在隔离RAM hold，没有真实Data/UFS或SpringBoard；没有CP命令、固件装载、GPU/SMMU访问、IRQ连接、电压/PMIC寄存器写入。SPMI observer CMD 的28次MMIO写入只请求EXT_READL。没有刷写、分区、ADB、QEMU、启动重试或自动第二轮。

计数为 **HakiOS98 / iOS16 67**。原索引、已验614.4桌面/R20/low01、G0与旧01/当前02冻结字节均保持。

## 两路 S2 真实数字配置

两个 rail 均为TYPE=0x1c、SUBTYPE=0x09，即来源明确的FTSMPS2p5/common布局。两遍raw完全相同：

| 字段 | GFX：PMI8994 S2 / SID3 | MX：PM8994 S2 / SID1 |
|---|---|---|
| peripheral / PPID | 0x1700 / 0x317 | 0x1700 / 0x117 |
| 实际APID / observer地址 | 52 / 0x04da0000 | 127 / 0x04ff8000 |
| TYPE / SUBTYPE | 0x1c / 0x09 | 0x1c / 0x09 |
| RANGE / VSET | 0x00 / 0xb8 | 0x00 / 0x9a |
| MODE | 0x40 | 0x40 |
| ENABLE | 0x00 | 0x80 |
| STEP | 0x82 | 0x82 |
| 由数字编码解算的设定 | 80,000 +184×5,000 = **1,000,000µV** | 80,000 +154×5,000 = **850,000µV** |
| 数字软件使能 bit7 | 0 | 1 |

所有28个字段数据valid=1、error=0、attempted/completed/status_valid=1、传输STATUS=0x1；每次polls=2，共56次状态轮询。两路独立valid0/valid1/stable均为0x7f，实际version0x20010000、唯一PPID映射各matches1，未出现拒绝、缺失路由、配置或传输错误。

读回4038次32位MMIO与28个READ命令，和两次129-read probe +28×135-read单次事务一致；这项一致性不代替USB独立重放。TYPE/SUBTYPE选择来自固定MiCode QPNP表，ENABLE仅作数字SW位解释；MODE/STEP保留raw，未凭名字推断自动调压、外部控制或settled。

来源：[SPMI独立解码](/F:/IOS-MIX1/evidence/gpu-g1-20261123/spmi/runtime-precheck02/README.md)，固定MiCode `1492c8e4373b7428bb0accc4f4881e97b36508b9`，`qpnp-regulator.c:427–429/932–954/1666–1667/174–175`。

## 本机 fuse 与原厂133MHz模型

| row | 地址 | 双遍相同的raw64 | 原始console行 |
|---:|---|---|---|
|39|0x74138|0x401103ea7ab389d6|136 /137|
|41|0x74148|0xd4c1838e02e761f0|138 /139|
|63|0x741f8|0x3a5b3bb5f3badb39|140 /141|
|64|0x74200|0xc1dde74d5f3995ed|142 /143|
|66|0x74210|0xab0e61ddc60a1d04|144 /145|
|68|0x74220|0x00700000043ac54b|146 /147|

12个完整64-bit读取均有效且一致，fuse_valid0/valid1/stable=0x3f。解得MMSS CPR revision1/bin0/limitation0/aging raw1；四个initcode为20/22/25/27，对应sign/magnitude -4/-6/-9/-11，SVS/Turbo offsetraw均0。此处MMSS revision与CPU revision不同，未混用。

stock Pro compatible、combo1与speedbin查表保持固定來源。133MHz对应fuse corner0且不需插值：

```text
Pro reference 670mV + (-4 × 10mV) =630mV
stock fused trim -85mV             =545mV
5mV向上舍入与520..670mV限制       =545mV
启动最大aging +15mV              =560mV open-loop/ceiling
启动floor                         =535mV
```

这是**computed stock policy only**：不是实测rail、不是动态CPR闭环目标，不是裸PMIC写值许可。原来缺row63的历史模型继续保留为未就绪；新的同次模型独立保存，不回填历史缺失数据。

原始console实际按row依次打印pass0/pass1，并非先打印整个pass0再打印pass1。旧计算器的跨pass全局行号检查不适用该日志；Raman仅在新的runtime wrapper中验证每个pass行号递增/唯一、两组互不重叠、原始完整字段和双遍一致，再调用旧的固定策略/算术函数。**未重排或改写console**，原计算器及已冻存测试源保持。

[同次模型](/F:/IOS-MIX1/evidence/gpu-g1-20261123/fuses/runtime-precheck02/model-final.json) · [来源与算式复核](/F:/IOS-MIX1/evidence/gpu-g1-20261123/fuses/runtime-precheck02/README.md)。主代理直接从真实row63独立计算，结果与子代理固定策略输出相同。

## 映射、温度与退出

fuse、SPMI core以及两路observer共4次完整16KiB映射/释放，后续summary证实释放调用返回；结果静态保留。末行：fuse_reads12、observer_cmd_writes28、poisoned0、regulator/GPU writes0、settledUNVERIFIED、chip_idUNREAD。G0双遍18项状态继续一致，未越入GPU aperture。

| CPU affinity | 本轮原生采样峰值 | phase=stop |
|---|---:|---:|
|0x0（UEFI resident）|52.2°C|49.9°C|
|0x1（XNU引导核）|52.8°C|46.7°C|
|0x100|49.3°C|46.1°C|
|0x101|50.2°C|46.1°C|

这些是CPU TSENS原生值，非GPU温度/校准精度证明。正常180秒timer、四路stop、return_cpu logical0affinity1、source_dropped_bytes0、**drain1 target65140** 均捕获。RAM DATA SYNC lookup error2是没有Data挂载路径，非UFS失败。

独立重放原始USB：**263帧 /73,605原始字节 /65,189console字节**，offset0→65189，缺口/重复/残帧/device-lost均0，冻结receiver重解码逐字节一致。04:41:18接收器保留capture_failed/Pipe error；它不影响已独立核对的正常timer/drain，也不当作自动返回证明。console第152行存在两个printf前缀交错，原字节原样保留；28个关键rail字段及12个fuse字段完整，独立核对未用交错行推算电压。source drop为0不等于所有printf天然整行原子。

**04:42:09**在原窗口内唯一devices/product核验同一`54223ce9 / MSM8996_A8`已Fastboot。不再查询，未请求手动恢复；终屏未目视，UEFI内部Prepare/Commit未单独捕获。USB断开不是该终点证明。

## 分工、下一步与范围

主代理执行单次lifecycle、保存流解析、USB独立重放、原始数值与算术核对；Raman只对保存fuse和固定策略计算；Pasteur只对保存S2数据和固定QPNP表独立解码。子代理不操作设备。本次代码/构建没有再修改，测试没有重复跑。

**G1前置采集完成，完整G1上电/chip-ID仍未完成。** 下一包应先准备指定GFX rail的受控配置/使能与真正settled读回，确认MX RPM corner4/bus vote和共享资源前置，再按来源明确的CX/GX/clamp/时钟序列上电。不能将遗留1.000V直接启用，也不能因计算出560mV就降低共享MX rail或擅自接管CPR。CPU保持614.4MHz。

完成这些前置后才读取`RBBM_CHIP_ID`，目标0x05030004；PAS13/G2和CP/fence仍后续独立步骤。新的写入范围先形成可审查包，再依用户GPU计划§0确认上机。本次没有将前置采集批准扩大为GPU写操作。

[完整结构化结果](/F:/IOS-MIX1/evidence/gpu-g1-20261123/main/runtime-precheck02/analysis.json) · [原始console](/F:/IOS-MIX1/evidence/gpu-g1-20261123/package/runs/gpu-g1-precheck-02/console.log) · [USB独立审计](/F:/IOS-MIX1/evidence/gpu-g1-20261123/main/runtime-precheck02/usb-audit.json)
