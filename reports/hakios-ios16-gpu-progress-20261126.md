# PAS03 真实认证通过；G3 首次采集发现接口续接缺口

2026-11-26 05:37:18 单次 RAM `5256c305`：stock-direct PAS13 的 INIT、MEM_SETUP、AUTH、SHUTDOWN
四次真实返回均为 callback0/x0=0/x1=0，authenticated1/shutdown_ok1/hold0，G1 电源和时钟恢复通过。
正常180秒timer、四路stop、source0、drain1 target73314；USB295帧/73363console字节独立逐字节重放通过。
05:41:37 唯一 devices/product 核实同机 Fastboot。本结果解决的是安全世界认证与清退，尚无CP或Metal执行。
见 `reports/hakios-ios16-gpu-g2-zap-03.md/json`。

随后05:49:52单次 `gpu-g3-read-01`/`9e7feaa9`只新增SMMU只读开关，累计119/88。
SMMU四ID双遍8读有效且稳定：ID0=48014402、ID1=20008002、ID2=7511、ID7=0，
CPU PA44位、SMMU IAS/OAS36位、UBS48位。旧实现额外要求ID7 major2，因此尚未采集SMR/S2CR/CB。
固定Linux driver以MSM8996兼容绑定选择v2，没有ID7门禁；按该来源修正的候选已通过专项并接入端口，
保留ID7双遍原值与写模式未实现格式边界，不把旧采集补认完整通过。

本轮还捕获INIT返回 `x0=1/x6=61900003`，属于SCM interrupted；旧native后端未实现协议续接，
没有执行MEM/AUTH/SHUTDOWN，G1依赖保留。正常timer/四stop/source0/drain1仍捕获，
USB289帧/81011raw/71763console独立连续；05:54:33唯一Fastboot枚举为空，未重查或重试。
不能仅凭空枚举确定物理终屏或复位失败原因。后续先修SCM调用续接，不把interrupted当最终secure拒绝，
不重发原INIT，不放松DMA清退条件。当前主机侧继续构建；没有在终点未就绪时启动下一包。
见 `reports/hakios-ios16-gpu-g3-read-01.md/json`。

软件线：A5xx backend façade已加入实际Darwin arm64静态archive；C API/CA路由、真实动态SMMU/通用IB运行时仍在集成。
这不构成GPU命令、着色器、Metal或CoreAnimation实机通过。CPU614.4配置模型、R20、low01、旧冻结及原索引保持。

# PAS01 交接准备拒绝，自动Fastboot通过

2026-11-26 04:44:39一次RAM8f036a3b（116/85）。代码保留receipt cookie有效，但metadata receipt未通过，prepare error4，0PAS调用/0GPU电源写；不是安全世界认证拒绝。180秒timer、4stop、source0、drain1target55886；227帧63199raw/55935console独立重放一致，04:48:52唯一devices/product确认同机Fastboot。CPU614/R20/旧冻结/原索引保持。下一修复loader metadata交接后继续PAS。

Metal软件线：真实AIR varying identity恢复，MatchingNames通过且旧新VS/FS指令和IO逐字节一致；Darwin对象编译通过，无硬件着色器执行。

# SCM INFO01 实机接口查询完成

2026-11-26 GPU G2 SCM INFO01真机结案：2026-11-26T03:54:48.002067+08:00单次RAMbc0e4d9e，115/84；独立内核线程logical0affinity1，四次INFO查询通过=True，SCM64 self和INIT/MEM/AUTH可用性原始x0..x3均保存；0GPU/PMIC电源写、0GPU aperture访问、0PAS操作，PAS13/认证/CP/Metal仍未验。正常180timer/drain=True，4路stop，source=[0]，229帧/63632raw/56304console独立逐字节重放=True，接收器capture_failed原样保留；唯一同机Fastboot核验=True。04手动恢复及自动返回失败保持，不重复TRUST实验、不放宽返回门禁；CPU614/R20/旧冻结/原索引保持。见reports/hakios-ios16-gpu-g2-scm-info-01.md/json。

# 03:02 TRUST直接访问实验未完整退出

04单次RAM镜像`e1e7580d`已接受，累计114次HakiOS/83次iOS16。
03:07唯一Fastboot枚举未找到手机；用户随后明确报告仍停在总结界面，自动返回失败。
保存USB216帧/60536raw/53624console独立逐字节重放连续，但只有combined begin，
没有combined/TRUST结束、正常180秒timer、四路stop、最终源丢计数或drain证明。
接收器是核验失败后由主机stop结束（host_capture_complete），本轮不记Pipe error。

因此本轮TRUST是否已读/写、GPU供电清理和总结保留原因均未验证；不把缺失字段填0，
也不把03的上电/时钟/VBIF成功移用为04的结果。该实验未验收，不重复启动04、
不放宽panic/正常timer/排空条件以强制复位。当前缺的是总结页原文/照片，保存后才人工恢复Fastboot。

本次只修改保存证据的分析和记录流程：能够为缺少阶段结束记录的实验输出失败报告，
保持未知硬件值为null。三个针对实际03成功流和04缺失流的回归通过；没有新增设备命令或内核修改。
见`reports/hakios-ios16-gpu-g1-power-clock-04.md/json`及`reports/hakios-ios16-gpu-g2-trust-01.md/json`。

# A530 GPU推进 · 2026-11-26 电源、时钟与VBIF访问完成

## 最新实机结果：组合03

02:44:30一次隔离RAM启动`bdc795b8`，完整HakiOS内核`326ec66f`。
S1真实APID159、mapping11400；16个PMIC只读事务、609次load，两个字节快照完全相同。
TYPE1c/SUB09、RANGE0/VSET8e独立解码790000µV数字设定，ENABLE80/STATUS83满足既有ready谓词，
高于同次SMEM473中rail101/internal2的620000µV启动需求上界；不将数字设定称为模拟电压。

实际完成GFX使能、CX/GX、MMPLL2及133MHz目标配置读回。
GPU-facing两个总线分支读到`346018=1`、`309010=4221`，均处于ON状态；
随后访问`00b0c000`获得VBIF_VERSION=`20040001`。
42次时钟/域写、160次时钟读，清理错误0、依赖保留0、consumer_safe1，
GFX原始状态校验和恢复ACK均1，两个PMIC设置事务仅ENABLE80→00，无电压或RPM资源投票写。
该结果不是完整chip ID、CP/fence或Metal验收；133MHz为配置模型，未做实际计频。

180秒正常timer、四路stop温度、源丢0、USB drain通过；281帧/78240raw/69248console独立重放逐字节连续。
接收器原始Pipe error保留。02:48:33唯一devices/product核实同机Fastboot。
当前累计113次HakiOS/82次iOS16接受启动；CPU614.4配置模型、R20、low01和旧冻结保持。
详见`reports/hakios-ios16-gpu-g1-power-clock-03.md/json`。

本次修复有两项：S1发现范围从旧128项补到本机core资源对应的512项，observer仍只允许APID0..255；
RCG在写CFG后允许源码定义的CONFIG_DIRTY状态，再发UPDATE并保留完成检查。
前者39个实际include专项、后者旧版14写失败复现及新版7项通过ASan/UBSan；两版实际完整内核链接和包读回通过。

此前01受S1发现范围阻断，正常退出但工具中断错过原返回窗口，自动返回未验证；
02已读到S1的785000µV，但RCG旧检查导致正向/恢复HANDOFF失败，保留供电/时钟状态，
正常定时与同机Fastboot终点通过。两轮失败和原始流均保留，没有补认阶段成功或覆盖旧镜像。

下一步为G2安全模式路径：核对Linux无zap回退的严格适用条件，并整理PAS13的实际保留区与调用方案。
不以VBIF读回替代CP运行证据，也不把主机Metal模型测试当作实机界面加速。

## MX真实启动表已补齐

01:17:52单次RAM启动MX只读01，镜像`8ad17b04`、实际完整内核`5ce77ab1`。
SMEM473位于`0x86003470`，大小1024；version1、三条rail中的`0x100/vddmx`为kind1，
internal corner3的ceiling/floor/initial均850000µV。16个稀疏payload字与五个descriptor字双遍一致，
实际42次read32、124µs、一个16KiB映射释放，零写/票/轮询/重试。
保存原始数据经独立stock RPM格式解码，与内核850边界判定一致；`resolved_valid=0`的raw0不当作电压0。

正常180秒timer、四路stop、source0、drain1 target59677；242帧/67470raw/59726console逐字节重放连续。
接收器原始Pipe error保留；01:22:04唯一devices/product核实同一手机回Fastboot。
当前110次HakiOS/79次iOS16接受启动。见`reports/hakios-ios16-gpu-g1-mx-plan-01.md/json`。

该结果是启动交接的电压上界，不是实时模拟电压或RPM私有副本的身份证明。
下一组合上电在同次重新核对SMEM、PMIC850数字设定和ready，复用已验GFX/CX与继承运行BIMC；
不再依赖不可恢复的旧AP资源票。GPU域/时钟、硬件身份、CP/fence与Metal仍待实际验证。

## 此前CX结果

命令工具恢复后已完成三次单次隔离RAM启动，均按180秒定时退出，分别以唯一Fastboot devices/product核验同机终点；当前已接受109次HakiOS/78次iOS16临时启动。

| 修订 | 镜像SHA前缀 | 关键实机结果 | 阶段结论 |
| --- | --- | --- | --- |
| CX01 | dd4f5ee3 | 请求后整字读回失败、恢复未确认；8读2写 | 失败记录保留 |
| CX02 | 6a00e641 | 捕获请求后08282000、恢复前a0282000；其余位变化使旧整字检查误拦 | 诊断完成，未认ON/恢复 |
| CX03 | 51810ee0 | STATUS80001fe0，原CONTROL222001恢复，16读4写548us，错误/dirty/uncertain均0 | CX本地请求与恢复通过 |

03实际写轨迹：`00282001 → 00282000 → a0282001 → 00222001`。
只改CX CONTROL的时序和collapse位；STATUS只读。字段核对覆盖`00fff007`（含两个模式守卫），实际修改只覆盖`00fff001`。依固定MiCode gdsc.c定义及其实时RMW实现，不给bit27/29编造语义，不用整字稳定代替字段检查。

03正常180秒timer、四路stop温度、源日志丢字节0、drain1 target61918；251帧/69999原始字节/61967控制台字节独立逐字节重放连续，接收器Pipe error原状态保留。2026-11-26T01:00:55+08:00同机Fastboot核验通过。最终硬件STATUS5ff0、本地CONTROL222001；未以全局OFF作为votable域的强制条件。

实际完整HakiOS内核3757afdb、隔离RAM镜像逐层读回；最终嵌套FV无物理UFS且FastbootDXE精确保持。Aristotle49个实际适配器专项及ARM64编译，main核对current/tested/prepared哈希、真实链接、镜像、生命周期及USB；保存日志解析器另以4个负向变体检查ON/写轨迹/字段比较/最终恢复判定。

本次未改电压、RPM资源票、GX、PLL/RCG或GPU分支，未读GPU/SMMU，未装载固件；不算完整GPU上电、硬件ID、CP/fence或Metal加速。CPU仍614.4MHz配置模型，R20显示和旧low01保持，新开关默认关闭。

后续已找到具体供电证据路径：本地匹配stock RPM的smpa2内部corner3缺省850000uV，但其SMEM473/XBL CPR配置可覆盖缺省。先只读采集该真实配置的resolved/ceiling，再决定是否能继承当前850mV；不猜旧AP票或另一个SoC电压表。参考`evidence/gpu-metal-20261126/mx-after-cx/RAIL-PLAN.md`。下一包尚未上机。
