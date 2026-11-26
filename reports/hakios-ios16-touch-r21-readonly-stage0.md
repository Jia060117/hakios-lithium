# MIX 1 R21：只读启动与触摸阶段 0 主机结果

> **状态更新（2026-11-19）：** 本文前段记录 R21 早期主机准备状态；末尾“后续更正”及 [最终核验报告](hakios-ios16-touch-r21-host-final.md/json) 为当前有效记录。R21 Stage 0 整包读回已通过（SHA-256 `07d9e4cd10ee37854d6fd65df5f8fe36d8d836d1212c77aeca6491a51d82c6f8`），但没有实体触摸后端，不可用于真机触摸验收。R19 分辨率错误，已归档；R20-display 是唯一显示基线。

R19 已上机且显示不正确，只保留历史记录。唯一显示参照为用户验收的 [R20-display](hakios-ios16-device-r20-display.md)：1080×2040 BGR24、步长 3240、360×680 points @3。候选保留 bootcpu=1 cpus=1、预期 affinity 0x1、UFS 根和 USB 日志。未查询或操作手机，未运行 QEMU。R21 Stage 0 只读主机包已在 WSL 外部目录组装并通过独立读回；它不是实体触摸包，不具备真机触摸验收资格。

## 已完成

- [候选覆盖层](../maintenance/hakios-lithium/peripherals-next/r21-readonly-overlay.py#L47) 按 SHA256 核对输入并生成到新目录。Mu 始终发布 ReadOnly、拒绝 Write，Flush 不发送设备命令；[传输入口](../maintenance/hakios-lithium/peripherals-next/r21-ufs-readonly.h#L7) 在描述符和 doorbell 之前只接受 LUN0 的 TUR、READ CAPACITY(10)、READ(10)，有数据时固定为设备到主机。
- [loader 改动](../maintenance/hakios-lithium/peripherals-next/r21-readonly-overlay.py#L79) 删除落盘日志；保留 RAM 日志、返回摘要和 USB 路径。桥接直接拒绝 WRITE、WRITE_FAST、FLUSH，不向内核发布原生 UFS 接管区或可写 Data。参数从冻结 R20 只删除 `var_hfs=ufs_darwin_data ufs_write=1 hfs_bench=1`，保留 `ufs=2 root_apfs=ufs_userdata`。禁写涵盖已审查路径；不是对特权代码直接 MMIO 的硬件隔离。
- [内核接入](../maintenance/hakios-lithium/peripherals-next/touch-r21-kernel.inc#L7) 省略 `touch_stage` 时无动作；显式 0 报告三行静态资源；重复或非法请求禁用。1–3 尚未接硬件，在检查 CPU 契约后明确拒绝寄存器访问。
- [Stage 2 trace](../maintenance/hakios-lithium/peripherals-next/touch-stage-trace.h#L94) 可选保留最多四份有效 63 字节原始帧，最多 32 行触点变化，最后分开报告 GPIO105/125 计数。采用现有集中映射；同一轮询内的变化仅是相关证据。记录器尚未接物理轮询。

## 构建和检查

- `wsl.exe -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals-next/test-touch-r21-host.py --compiler clang` 退出 0。Clang ASan/UBSan 11 个候选套件及补丁 0020 的实际队列函数通过；禁写过滤覆盖 768 种 opcode/传输形状组合。R15、R19、R20 冻结件及旧严格测试入口的前后哈希一致。
- 隔离输出在 WSL `/home/jia/ios-mix1-build/r21-ro-check.oQTDVX`。上游 loader `LOADER_CFLAGS=-DHAKIOS_LITHIUM=1 LOADER_IMAGE_BASE=0x40400000 bash xnuloader/build.sh`、Mu `build -p lithiumPkg/lithium.dsc -a AARCH64 -t CLANGPDB -b RELEASE -n 12 -D LITHIUM_BUILD_TAG=<loader-hash-prefix> -y <private-report>` 均退出 0。使用本机现有 D22AP 头及 Mu `6b3d90b79e191e08171bfedd3980234a7e49d340`。PE 读回确认 loader 基址 0x40400000、映射大小 0x758000；DXE 带 `Data_write=0 policy=r21-readonly`，loader 无 `logdump` 文字。Mu SHA256 `181efbd44d5b75800b2a21ffde472e0426cbb043840cc95c3b9a833dac671ff2`。
- HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`：使用项目 venv、`HAKIOS_SOC=msm8996`、`make kernel SHELL=/bin/bash DEVICE=lithium JOBS=12`。第一次漏加 venv，LLDB macros 缺 `python`，构建正常拒绝；第二次沿用了存有旧绝对路径的缓存，读回发现没有新日志，废弃该产物。移走候选自己的 `BUILD` 后全量重编译，退出 0，内层仅已有 symbol-set/export 检查退出 2。真实 `.d` 指向新覆盖层，最终 arm64 Mach-O 含三行阶段 0 日志，SHA256 `f19ef6e026a10bef44003d91984c44eb475b000bc952f68d9247b3e2271b63b1`，日志 `stage0-kernel-fresh.log`。
- 未运行候选整包的用户空间 413 任务、指令回归和实际 XNUP/FAT/APFS 读回；尚无冻结整包或离线预检记录。

## 剩余验证顺序

1. 禁写候选默认不开触摸。第一轮验收只读系统和两个 cryptex、RAM `/private/var`、LaunchServices ready 及 R20 比例的主屏。R20 依赖此前的持久 HFS Data，不能据此认定空 RAM Data 也能进桌面。以 2880 MiB 窗口计算，ramfs 文件页名义上限约 720 MiB；冷建库峰值待测。
2. 仍需一个同一冻结文件的易失阶段选择入口。Stock Android header 经 LK 可到 `/chosen/bootargs`，但 R20 使用嵌入 XNUP；修改外层 header 会改变上传字节。原厂 DTS 把音量加放在 PM8994 GPIO2、音量减放在 PON `resin`，不可套用 TLMM GPIO 读法；Mu 暂未接入这两个键。当前没有可执行的上机切换命令。
3. 阶段 0 只验收三行资源日志。阶段 1 仍缺 QUP6 独占交接、活跃时钟和 pinmux、可安全访问的 MMIO 映射及触摸供电运行证据；满足后才许可一次 0xA3 芯片 ID 读，不动复位、供电和 IRQ。软件期限不保证已经挂住的 MMIO 能返回。
4. ID 成功后用一轮四角、中央、滑动、松手同时采集触点与两路 GPIO。窄脉冲可能被轮询漏掉；坐标和松手验证后才启用真实 HID，区分队列接收和实际交付。物理帧一致性、GPIO、IRQ、HID 和 RAM Data 主屏均未验证。

## 并行执行记录

| 子代理 | 交付与主代理复核 |
| --- | --- |
| Parfit | 查同字节启动的选择入口；主代理核对 XNUP 参数、Mu 路径和 DTS 按键节点。 |
| Boole | 查 R20 根、RAM Data 和写出路径；主代理核对合同和实际 loader/DXE 并实现禁写。 |
| Hume | 提供 GPIO/触点记录和 5 组测试；主代理复核几何和日志预算，补原始帧并重跑 6 组。 |
| Lorentz | 提供阶段 0 输出及测试；主代理复核 DTS、接入内核并读回最终 Mach-O。 |

四项均完成并关闭，无共享写冲突；未估填各代理耗时。当前无需操作手机；待整包离线核验和冻结后，再请求本轮临时启动确认。

## 后续更正（2026-11-19）

本节及 reports/hakios-ios16-touch-r21-host-final.md/json 取代前文“没有可上机镜像”及“整包尚未读回”的旧阶段状态。只读 Stage 0 整包已在 WSL 外部目录组装并由独立检查器读回通过，SHA256 为 07d9e4cd10ee37854d6fd65df5f8fe36d8d836d1212c77aeca6491a51d82c6f8。R20 root/resident 用户空间是逐字节复用，未重新执行 413 项用户空间构建。

loader 中的一次性易失 Fastboot stage selector 已随 R21 loader 构建并通过绑定 build.json 的 ASan/UBSan 测试。Stage 0 仍只输出静态数据；Stage 1–3 仍 fail-closed。此包没有实体触摸后端，因此 touch_package_ready=false，不允许作为触摸验收包。R19 已上机但分辨率错误，归档、不复测；R20-display 是唯一显示基线。
