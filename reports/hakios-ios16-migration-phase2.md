# iOS16迁移阶段二：完整内核与设备层构建

2026-11-16。阶段二的电脑端集成与构建完成，停在阶段三之前。目标仍是6GB机型全部可用RAM、USB日志和真实SpringBoard；本次没有查询手机、启动手机、分区写入或QEMU。**本次不构成iOS16已启动、6GB已开放或USB已传输的证据。**

## 已完成

- 已创建备份分支 `archive/ios14-r31-20261116`，提交 `bff07fb1922fdb7a2c8793dafa7a90144f20caae`；仓库外E盘独立备份完成校验。迁移归档的50份iOS14适配文件再次逐字节比对独立备份一致。旧WSL树保留，新工作空间为 `/home/jia/ios-mix1-build/hakios-lithium-ios16`。
- 完整HakiOS固定提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2，正常上游XNU/HFS/库/配置/构建流程。iOS14共享区域、旧Mach消息、权限/persona、D20AP及IOSurface适配不再进入活动编译与打包输入。上游iOS16 init、注入库和IOSurface文件逐字节一致。
- 保留MIX的ARMv8.0能力选择、GIC redistributor stride、Mu板层、UFS buffer映射和安全栈交接。物理BGR24适配保留上游iOS16 IOSurface的BGRA32接口，仅在输出到面板时转换，实际绘制时交出文字控制台。
- 完整内核、完整Mu固件和加载器已编译；修改后的BootShim也已用真实Mu FD和假载荷单独汇编核验。该假载荷只用于汇编检查，未生成手机启动镜像。

## 内存：512MiB改为2880MiB，6GB目标尚未完成

| 用途 | 起点 | 终点（不含） | 大小 |
| --- | --- | --- | --- |
| XNU独占窗口 | 0x100000000 | 0x1b4000000 | 2880MiB |
| XNUP启动载荷 | 0x1b4000000 | 0x1b8000000 | 64MiB |
| UEFI驻留堆 | 0x1b8000000 | 0x1bc000000 | 64MiB |

Mu内存图、BootShim复制地址和加载器上限已同步。加载器要求实际EFI独占分配描述符匹配，不能仅靠命令行虚报。上述范围位于已测量上部DDR，并与记录的固定保留区、动态分配窗口互不重叠；上部DDR余量25.3125MiB。扩大窗口**尚未上机**。

已记录的DDR总量为6105.3125MiB；不能把机型标称6GB直接连续上报。低地址3072MiB含固件/显示等保留段，/reserved-memory/adsp_region, /reserved-memory/qseecom_region, /reserved-memory/secure_region的动态地址未知；当前XNU ARM启动页分配使用连续物理窗口，仍需接入可验证的多段/保留区信息。2880MiB只是首个可核验窗口，不是终点。后续在联合启动日志中获取保留区证据，使用HakiOS/XNU现有页管理接口扩大可用范围，不能重写一套VM或把保留页当空闲页。

## USB：源码、编译和电脑端就绪，硬件未验证

复用固定ios-sailfish同版本DWC3 EP0组件，使用MIX原厂DT和Linux MSM8996 GCC寄存器资料，未采用Sweet的地址。来源与哈希在 `ports/hakios-lithium/drivers/usb/usb-provenance.json`。新增USB2 vendor bulk IN日志接口及Microsoft OS兼容标识；电脑采集器 `scripts/capture-hakios-usb.py` 使用PyUSB/libusb，保存原始帧和可读文本。不会接收执行命令或写存储。

驱动已进入最终内核链接。`lithiumusb=0`关闭，`1`只读探测，`2`在电源、时钟、DWC3标识与device mode满足条件时接管；当前默认关闭，阶段四联合包才启用并观察。初始化与传输在后台，检查/命令等待有界；条件不满足则跳过，不把USB成功作为桌面启动前提。DMA显式缓存维护并使用独立缓存行TRB。

沿用上游256KiB启动头和1MiB环形日志。每帧带绝对偏移与丢失计数，主机检测断段与重复；USB reset后可重放保留启动头。静态日志容量有限，长时间断线会丢中段；panic发生时尚未发送完的尾部也不保证已传到电脑。枚举、Windows绑定、传输、重连和完整launchd日志均须真机确认，不能写为“USB已可用”。早于原生驱动的UEFI日志也尚无已验证USB出口。

## 修复与检查

1. 构建并发 `JOBS=12` 与上游用户空间同名任务选择变量冲突，改用 `HAKIOS_DARWIN_JOBS` 选择守护进程，保留上游默认任务表以及同时注入containermanagerd和containermanagerd_system的路径。实际用户空间打包在阶段三；这不是旧container error51已解决的运行证据。
2. 新驱动使上游配置生成器更新 `bsd/conf/files`，旧整份补丁反向检查不适用于生成后的清单。现在从固定Apple源、完整HakiOS补丁和正常配置生成器重建该文件并逐字节核验，其余补丁仍要求可反向检查；未取消验证。
3. USB组件的线程退出函数不在BSD可见头文件中，按上游驱动已有用法使用 `thread_terminate(current_thread())` 并声明同一内核API。未删除USB、日志或其他配置功能。

52项源码/加载边界检查通过；实际日志生产/读取函数通过ASan/UBSan，主机分帧覆盖拆包、合包、重复、缺段、损坏/超长拒绝及全部USB2传输长度。8份产物完成目标LSE/PAC/PAN/UAO指令扫描，SHA256常量表逐字节核验后才排除，ARMv8兼容HINT单独计数。扫描有明确范围，不能替代真机SIGILL排查。完整内核仅剩上游已限定的kext symbol-set导出检查失败，由正常make-kernel判定脚本接受；没有编译或链接错误。加载器有一个未调用reboot函数的警告，MIX返回后保留摘要而不自动复位。

| 产物 | 字节数 | SHA256 |
| --- | --- | --- |
| HakiOS内核 | 14880512 | 00581a2aa72bf34d2d6ec5f4c22433fbfea47cc748ec37c607b5f225dbe22ad3 |
| 完整Mu FD | 3145728 | 906d5c1ec4c53368583ed2b48dc69f1f40c37a00f9d996baeae0d3ade1fb98b1 |
| loader（占位身份） | 67072 | e7aa575e88a8a1b49e2cf2a5c49afeaf75b7b1fbbbb4c1112253d1fb1d831b5f |

冻结产物在 `artifacts/hakios/lithium-ios16/phase2/`，不进Git。**loader当前身份为编译占位，不可用于启动Darwin；不是可以上机的完整包。**未把Apple程序或固件加入仓库。

## 留待阶段三与下一风险

确认阶段三后，仅用用户提供的16.5/20F66 IPSW准备原始D22AP身份、System/cryptex/Data与上游用户空间布局，核验两个容器daemon、/private/var可写、容器与mobile目录的所有者/权限，再准备实际集成包。系统继续放UFS按需读取。UFS读通不等于持久写通：现有板层仍是只读保护，不能仅格式化分区便宣布Data可写；需对接并核验上游写保护/布局契约，写入前重新确认身份、几何及冻结输入。

阶段四才验证扩大内存、USB枚举/日志、服务、显示与SpringBoard。物理触摸和A530加速仍未通过；不要求重做三次十分钟P1或重复只读存储验收。本阶段按用户逐阶段报告要求停止，阶段三及四尚未开始。

机器可读结论与证据哈希：`reports/hakios-ios16-migration-phase2.json`。
