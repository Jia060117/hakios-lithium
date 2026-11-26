# R38：供电诊断的页表撤销 panic 已定位

2026-11-20，Asia/Shanghai。**CPU频率尚未修复，四核正常频率与四核XNU调度均未验收。** 本轮已把慢核原因从性能推测推进到实际APCC证据，并准备了受条件约束的固定频率核心；供电诊断当前没有有效运行证据，未执行PLL或电压写入。

## 用户照片补充（本条更新下方历史未知状态）

原图已保留在本轮local目录，SHA256 `4d841817262987a51484b5df8a87dd00481b59882bf60468f70243fb008542db`。照片明确出现 `write_pte_fast: PTE write is unaligned`、pte=0、pmap.c:626。caller `0xfffffff007403104` 对本轮冻结内核f4ab3f14解析为 `_pmap_remove_range_options+0x81c`；实际反汇编 `...3100` 是调用panic，`...3104` 是返回地址。PTE指针末尾0x8570，mod32=16，符合16KB VM页使用4个4KB硬件PTE时的对齐断言。完整保留日志75193字节未由USB回收，不能凭照片补造供电数值。

新增供电采集器把 `ml_io_map(physical,0x1000)` 返回的含页内偏移指针传给 `ml_io_unmap`；后者直接 `pmap_remove` 并释放kernel_map。本机16KB VM页下这不满足撤销范围契约。另普通map分配在永久io_submap，不与释放kernel_map配对。主代理修正为显式page-base/rounded-extent、独立data-offset、`ml_io_map_unmappable(...VM_WIMG_IO)`，释放仅用原allocation与完整extent。没有删除页表断言，也没有以硬件访问保护为名跳过错误。

照片同时确认 `smp_return_verified owner0 kernel_cpus_off3`，然后 `retained reason=kernel_proof error12 ctx0`。error12是协议COMMIT缺失，不是新SMP owner失败，也不是已尝试PMIC复位。panic不具正常timer结束凭据，保留摘要符合既有路径；本轮不计自动返回成功。照片已保存后可人工Fastboot恢复，等待用户就绪后继续，无需新审批。

最终候选为clock-power03/frequency07，见 `reports/hakios-ios16-r38-power03-prepared.md/json`。中间power02/frequency06虽已构建但在API配对复核发现后被替代，保留不启动。两个新候选均未上机，本次照片处理没有任何设备查询/启动。

## 实际上机结果

| 轮次 | 接受时间 | 新增用途 | 结果 |
| --- | --- | --- | --- |
| R38 clock-read01 / frequency04 | 23:13:08.798747 | 两簇APCC两遍快照、bin | 有效：perf MUX0、auto0、primary/alt L0且未锁；89帧21541字节重放一致、正常timer/drain1；23:17:28同设备Fastboot |
| R38 clock-power01 / frequency05 | 23:21:14.436697 | LDO/BHS/SAW/AVS/fuse及既有性能基准，PMUsetup0 | 未验证：3分钟内未枚举USB、0帧；23:25:15唯一Fastboot枚举空，product查询跳过；屏幕停点未知 |

不能把第二轮0帧/空Fastboot直接解释成特定MMIO访问故障、panic、卡死或掉电。需保留屏幕证据后再定位；未重查、未重复启动。该包代码没有时钟/电压配置写入，PMUsetup0，但只读硬件访问仍可能发生异常，未实现虚假的fault guard。

当前精确boot-args：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_freq_diag=1
```

当前包SHA256：`2113ac8f3deb3bc21917e3423061b4a838c24c0a35a06bb91dd2932c8f87ded9`。内核SHA256：`f4ab3f14d8ef394d4b0dc8df98964c35ce4e6a1f3e22fea6a9df591c1aa3b690`。主机实际增量编译/链接、Android/XNUP/FAT/APFS组件读回完成；不代表本次内核运行成功。

## 已完成的实现与复核

- 主代理：`diagnostics/cpu-clock-r38.inc`只读实际APCC、bin；在`cpu-sanity.inc`接入默认关闭的`cpu_clock_diag`/`cpu_power_diag`。构建脚本`build-hakios-cpu-r38.py`、打包脚本`package-hakios-cpu-r38-clock.py`保留旧冻结产物，复用已验证RAM loader/Mu/root与自动返回，不重复重建未改组件。
- Mill：`cpu-clock-decode-r38.h`及专项测试，33用例/1743断言；主代理ASan/UBSan独立复跑通过，实际两轮寄存器另由主代理解析。后续`voltage-requirement/requirement.py`为固定最低档307.2MHz的stock CPR策略计算器，30个host测试，不是PMIC驱动；缺少实际fuse时输出BLOCKED，不生成写入许可。
- Goodall：`cpu-power-r38.inc`的两遍限定供电采集及只读来源；主代理核对地址/版本条件/默认门禁和冻结哈希，再做实际编译。模块并未取得真机供电读数。SCM IO_READ的明确源码ABI已调查，但没有执行可用性查询/SMC，也不能从服务号推本机允许该地址。
- Mencius：四核驻留限制报告及`cpu-perf-fixed-r38.h`纯C固定频率core，466个mock检查。它要求调用者已经验证供电、Pro/bin/revision、同代快照、perf两核静止和独占上下文；未满足则零写。11写序列仅给primaryPLL614.4MHz→main307.2MHz的源支持模型，不是最大频率、不是动态调速器，也没有连接到生产执行路径。主代理核对发布/已测草稿哈希、source记录与测试日志；没有把授权字段盲填1后上机。

来源/代码细节分别见`evidence/cpu-r38-20261120/decode/`、`power-plan/`、`voltage-requirement/`、`perf-fixed/`。共享集成仅主代理修改，未发现代理写入冲突；主代理未重复无关历史总回归。

## 四核与下一步

实际loader在phys0运行UFS邮箱服务，当前仅把1/100/101交给XNU。四颗物理核的时钟域修复与四颗核都参与XNU调度是不同验收。前者还缺供电/锁定后有效速率；后者还需移植已有warm UFS运行期读/写/flush、所有权与返回流程。不能把cpus4或RAM测试冒充真实UFS桌面四核。

优先下一步是取得当前手机屏幕上的结束原因/异常PC/ESR；随后手动Fastboot恢复。依据实际停点，把供电读取移到USB连通并已排空前置标记之后，逐次危险读前保留主机可见的检查点，或评估严格协商的secure IO_READ（失败无直接MMIO fallback）。不在终态未知时连启，也不凭缺失数据改PLL/电压。

目前目标未完成；这是需要设备屏幕证据的真实阻塞，不是等待额外审批。当前没有新增分区/刷写/擦除/ADB/Android/Recovery/QEMU；接受启动计数69/38。原R20和既有冻结包保留，原Git索引FDF775保持，未reset/clean/add-A。
