# lithium 最小 UEFI：构建与真机实验

日期：2026-11-12。目标固定为 iOS 14.8 / 18H17；本阶段仅开发其引导基础。

**第十轮用户确认八黄、一绿、无红：原厂Linux已到MMU开启后的高虚拟地址钩子。140秒未自动返回，用户明确手动重启，电脑核验回原厂lithium。第八轮已有FAT/EFI入口的目视证据；所有轮次仍无同次二进制journal，尚未调用loader Boot方法或启动XNU/iOS。无root、清除、分区刷写或重锁BL。下一阶段按 `reuse-route.md` 复用同芯片代码并构建7195，原厂Linux恢复诊断作为辅助工具保留。**

## 已完成的实现

`firmware/lithiumPkg` 提供本板 MemoryMap、SEC/PEI、BGR24 Blt-only GOP、最小 BDS 及 RAM 日志。使用公共 SEC/DXE、GIC、定时器、内存变量及必要架构协议；没有调用其他板的 vendor XBL 调度器、UFS 或 userdata 日志驱动。完整上游版本、构建输入、FD 和打包哈希见 `uefi-build.json`；源目录构建方法见 `firmware/README.md`。

GOP 基于一次真机入口采样的 `0x83401000`、1080×2040、stride 3240、24 位布局。协议使用 PixelBltOnly，不把三字节扫描缓冲错误公布为四字节 GOP framebuffer。BDS 自检模式查询、RGB 读写、水平/垂直重叠复制及边界拒绝，再以实际中断事件递增计数。用户照片中实际文字方向与 RGB 色块正确，且显示 `GOP TEST PASS`。

## 离线与 QEMU 证据

- 固定 Mu：`6b3d90b79e191e08171bfedd3980234a7e49d340`，全部递归子模块已初始化，overlay 在独立 WSL ext4 工作目录构建；上游源码未修改。
- 输出 FD：3,145,728 字节。第三轮硬件 FD SHA-256：`538a1dbaca3837e81c7a688d3b46989d7f7b229388589c90feb4fa33ffc1de22`。
- 第三轮真机 boot 镜像：45,113,344 字节，SHA-256 `70dee4d7f32905a22485156ad6cab1433130329afab6252eda3fd7dda327579a`。当前构建和最新实验以 `uefi-build.json` 与 `uefi-device.json` 为准，历史尝试单独归档。
- QEMU 三次运行采样 stage=6，ticks=2/7/12，GOP 自检通过；同次启动交接后记录 stage=7、ticks=20。
- 恢复出的 33,525,760 字节原厂内核加采集器与输入逐字节相同；FDT initrd 已指向原始 ramdisk；日志采集器执行完成。见 `uefi-qemu.json`。
- 日志采集器本身的十个 QEMU 用例通过，见 `journal-probe-qemu.json`。原入口 profile 重建仍得到既有的 `d600e068...` 镜像哈希。

QEMU 的 GIC 地址属于 virt，硬件看门狗写入被省略；备份内核的原厂继续入口在模拟中替换为停止指令。它没有运行 Qualcomm Linux，也不能证明真机中断、显示、缓存交接或 Android 返回正常。

## 真机实验

| 尝试 | 输入和方法 | 已观察结果 | 可以得出的结论 |
| --- | --- | --- | --- |
| 1 | 11,390,976 字节最小 UEFI，20 次计时后 PSCI reset；再启动原厂串联采集器 | Fastboot 接受；约 53.67 秒返回 Android；日志页读回全零；最终普通重启通过 | 临时启动命令和返回路径已观察，UEFI 具体阶段没有日志证明 |
| 2 | 增加匹配 DT 的 IMEM restart reason，请求 reset 后进入 bootloader | Fastboot 接受；实际约 54.26 秒回到 Android，未进入 Fastboot；日志仍无有效标记；最终普通重启通过 | 该 IMEM 请求没有实现预期的直接采集，未把结果记为成功 |
| 3 | 同次启动恢复原厂内核与 initrd，在原厂内核清 BSS 前采集 UEFI 日志 | Fastboot 接受；140 秒内 Android/ADB 未返回；用户照片显示 BDS、GOP 自检通过及 ticks=20；后续已重新检测到原厂 Android | 真机 UEFI 基础阶段目视通过；停止位置在第 20 次计时显示后的路径或更晚的 Linux 启动中；恢复交接和同次日志未通过 |
| 4 | 将交接移出计时回调，增加恢复检查点并在清缓存前屏蔽 IRQ | Fastboot 接受；未自动返回 Android；照片显示 `RESTORE 05 CACHE CLEAN` 和两个青色块；随后原厂 Android 重新连接 | 已走过缓存清理，执行到完整复制循环及 I-cache 同步后的检查点 07；后续寄存器恢复、分支或 Linux 早期启动尚未定位 |
| 5 | 正式调用 `ExitBootServices`，驱动退出通知关闭 timer/GIC，再恢复原厂内核；增加最终跳转前标记 | Fastboot 接受；未自动返回 Android；用户文字反馈 `RESTORE 05 CACHE CLEAN` 和三个青色块；随后原厂 Android 重新连接 | 已完成正式退出并到达最终跳转前检查点 08；不能仅据此确认 Image 分支、采集器或 Linux 主体执行 |
| 6 | 恢复原始 EL1 状态；增加采集器入口和 Linux 主体入口前标记；在 FDT 尾部保存同次原始状态 | Fastboot 接受；用户文字反馈四个青色块、无红块、仍显示 `RESTORE 05 CACHE CLEAN`；用户明确确认手动重启；正常 Android 无本轮诊断属性 | Image 分支已执行并到达采集器入口前检查点 09；第五块前可能包含采集器、返回检查或状态副本访问。无同次日志，不能判定具体停点 |
| 7 | 修正状态副本未对齐 64 位访问；常规与严格对齐 QEMU 通过后启动 | 用户确认五个青色块、无红块；140 秒内未返回，稍后原厂 ADB 重连，恢复方式未确认 | 到达采集器和状态副本完成检查点 10；末尾分支、Linux 主体和自动返回均未证实 |
| 8 | 新增只读 FAT、控制台、DarwinBoot EFI 启动及 CPU 内核复制读回比较 | 用户确认 FAT/EFI PASS、五个青色块、无红块，未进入 Android；随后明确手动重启回系统 | EFI 入口/协议、复制读回及采集器完成有目视证据；Linux 主体和同次二进制 journal 仍未证实 |
| 9 | 加入八个原厂 Linux head 指令钩子及早期异常显示 | 用户确认八黄、五青、无红块或异常数值，未进入系统；140 秒监测无返回，随后明确手动重启回 Android | 真实 Linux head 到开启 MMU 前有目视证据；MMU 和高虚拟地址执行未证实；下一候选需同时补足诊断 stub 自身的恒等映射 |
| 10 | 补足诊断stub恒等映射，增加MMU后高虚拟地址与绿色标记；六项QEMU通过 | 用户确认八黄、一绿、无红；青色数量本次未重述；140秒无自动返回，随后明确手动恢复，电脑核验原厂Android | 真机高虚拟地址钩子有目视证据，没有恢复二进制日志；尚未执行XNU，按用户要求转入现成源码复用路线 |

各次独立报告保留在 `uefi-device-attempt-01.json` 至 `uefi-device-attempt-10.json`；`uefi-device.json` 记录最新实验。USB 标识与实际发送输入保存在其 `local_log_path` 指定的忽略目录，不写入共享报告。首次实验主要保留日志和哈希；后续实验留有本地输入快照。

### 第三轮屏幕证据与边界

原照片与 SHA-256 已归档，结构化结果见 `uefi-screen-observation.json`。照片由用户关联到第三轮；画面不显示构建标识，不能独立将照片绑定到镜像哈希。核对时当前 BDS 源码与第三轮冻结源码 SHA-256 相同。`TIMER TICKS 20` 的绘制先于两次 `LithiumStage` 和 `RestoreAndroid()`，因此照片不能证明 FDT/副本检查、缓存清理、恢复复制或 Linux 入口已完成。画面中的 `ANDROID AT 20 SECONDS` 是预期行为文字，实际没有证实自动恢复。

20:34 重新检查时唯一已授权设备为 lithium、`V9.5.2.0.NAHCNFA`，`sys.boot_completed=1`。不将此次重新连接算成第三轮自动交接成功。随后独立诊断约 37.34 秒返回 Android，同次日志证明完整副本槽与 initrd 校验、地址调整均通过；普通原厂重启与诊断属性消失也已独立核验，见 `handoff-probe-device.json`。

第四轮镜像 SHA-256 为 `fa976eef94509d6328756f8a325063e40e594f5fbe8d2929eb45ba7a5dae4437`，详细状态和照片边界见 `uefi-device-attempt-04.json`、`uefi-screen-attempt-04.json`。两块青色标记不能提供真机复制内容的逐字节校验，也不能证明最终分支或 Linux 入口已经执行；`RESTORE 05` 只是未被后续文字覆盖的上一行。

## 第三次的内存与交接设计

| 内容 | 范围 |
| --- | --- |
| shim/FD 输入 | `0x40080000` 起，限制在 3.5 MiB |
| 固件 FV 工作区 | `0x40400000..0x40c00000` |
| FD / RAM logger | `0x41000000..0x41400000` |
| SEC、页表、栈和信息页 | `0x41500000..0x41600000` 内显式描述的各段 |
| DXE Heap | `0x41600000..0x42400000` |
| 原厂内核恢复 stub | `0x42400000..0x424b3000`，实际 stub 小于 4 KiB |
| aboot FDT 窗口 | `0x42800000..0x42a00000` |
| aboot 加载的原厂内核副本 | `0x42a00000..0x44a00000` |
| 原厂 initrd | `0x44a00000` 起，实际 2,264,305 字节 |
| 显示保留区 / 日志页 | `0x83401000..0x85800000` / `0x84000000..0x84001000` |

固件堆栈及恢复 stub 均位于已实测的原厂 Image 保留窗口 `[0x40080000,0x424b3000)`；额外原厂副本由扩大且明确声明的 boot ramdisk 提供。区间检查对照真实 RAM/固定保留区，不把动态池或未描述 DDR 当成可分配堆。aboot 自身下载/解压 scratch 仍由原引导器管理，这部分没有新的真机采样证明。

恢复流程先验证实际 FDT 和输入地址，再清缓存，跳到原厂文件范围以外的 stub，关闭 MMU/缓存/定时器，恢复整个原厂内核加采集器副本，最后进入它；没有跳入已被 UEFI 覆盖的原厂旧代码。QEMU 证明了这个算法与镜像内容，第三次真机结果说明硬件交接仍有缺口。

## 当前验收与下一步

不把任何一次 `Booting OKAY`、自动重启、QEMU 画面或静态文字当成 iOS 上机。当前已有 7195 boot_args 版 DarwinBoot EFI 的 FAT 集成和入口执行证据，尚无移植后的 XNU 7195、iOS14 profile、触控或 iOS 用户空间。G2 真机验收与 M1 的可靠异常回传没有通过，M4 目标保持不变。

不进入 UEFI、不执行恢复复制的扩大 ramdisk 与 initrd 诊断已通过 14 个 QEMU 用例及一次真机验证，见 `handoff-bringup.md`。它排除了本次布局中单纯装载或地址调整导致原厂 Linux 无法启动的情况，尚未验证 UEFI 初始化后的缓存、中断状态和恢复复制。

第四轮已将交接从计时回调移到 BDS 主循环，先取消并关闭定时事件；FDT 两个属性全部检查后才改写；全缓存清理前屏蔽 IRQ。01–05 为文字检查点，关闭 MMU 后显示第一个青色块（06），复制与 I-cache 同步完成后显示第二个青色块（07）。journal 偏移 80 记录最新检查点。

核对固定 Mu 的 `CoreExitBootServices`、TimerDxe 和 GicV3Dxe，确认此前交接未执行正式的 Boot Services 退出流程；定时器和 GIC 驱动在退出事件中关闭硬件。第五轮使用预留的内存图缓冲区和有界重试调用 `GetMemoryMap`/`ExitBootServices`，成功后仅执行直接内存及缓存操作，并记录服务退出、定时器、CPU 中断组和 GICD 状态；最后跳转前再显示第三个青色块（08）。QEMU 验证退出通知和关闭状态；真机三个青色块说明这项修正仍未解决自动返回。

第五轮镜像 SHA-256 为 `85aca9342e60834901521a81afc2db6bef263ca789adb065d976c552c16871d7`，构建标识 `0x7185290759b01872`，用户文字证据见 `uefi-device-attempt-05.json`。没有将 QEMU 中读到的 timer/GIC 数值当成真机实测数值。

第六轮实现先保存、再恢复原始 EL1 状态，并加入采集器入口（第四块，09）和采集器完成后、Linux 主体入口前（第五块，10）两个标记。打包只修改诊断 Image 的两个分支，不修改 Linux 主体；采集器校验失败时显示红色块并停止。原始状态副本保存在 FDT 记录偏移 4608，不覆盖偏移 512–4607 的原有 journal。同次状态与 QEMU 入口采样交叉核对后，才进行新一轮真机实验；构建和真机结果见最新 JSON 报告。

第六轮镜像 SHA-256 为 `e39f5a65231e519ae06ae47d896844a685d77e399f39dee5afcc48cd9ab413fd`，构建标识 `0xbb37db9a56eff712`。常规 QEMU 通过，但真机在第四块后未继续。后续启用 SCTLR.A 的 QEMU 反例捕获到新增状态复制代码的对齐错误：`ELR=0x42400144`、`FAR=0x4240019c`、`ESR=0x96000021`。这证明该代码存在实际缺陷，尚不能单独证明真机也停在同一指令。

修复将状态副本的文字常量和 FDT 访问全部改为 32 位；严格对齐验证同时选择记录只按 4 字节对齐的 DTB。真机脚本要求常规和严格两份 QEMU 报告均匹配待启动版本。Android 返回但缺少诊断属性时明确记录验收失败，不再因 legacy ADB 的缺文件输出误入解码。反例与修复验证索引见 `uefi-alignment-regression.json`。

第七轮镜像 SHA-256 为 `2ea53d144b7d4dd6d831a55fa2578ccf72c01991997052b704da020272899820`，构建标识 `0xd591471d4316e440`。修复后的常规和严格场景均验证逐字节复制、同次原始状态副本及恢复状态、采集器、检查点 10、服务退出和 timer/GIC 关闭。21:20 发起临时启动，Fastboot 接受，140 秒内 Android 未返回；用户确认五块青色、无红块，真机已到检查点 10。随后原厂 ADB 重新连接，但恢复方式未明确；不计为自动交接。

第八轮的内存 FAT、EFI 执行、连续分配和完整 CPU 复制读回比较详见 `uefi-payload.md`。真机输入哈希 `f3d3f4b0fc89dc12c2fb183b2c3f000c41a3696cec62e274597160ef70251636`，tag `0x23523fa6b21e4e74`。新增读回比较不能倒推旧轮次；五块青色仍只到 Linux 主体前，后续将增加 Linux 早期标记。
