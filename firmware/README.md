# lithium 最小 Mu UEFI

本目录保存 MIX 一代的自有平台层，目标系统仍为 iOS 14.8。当前固件已集成只读内存 FAT 卷及 7195 boot_args 版 DarwinBoot EFI 的实际加载；尚未调用其 Boot 方法，也未加载 XNU 或 iOS 用户空间。第八轮真机显示 `FAT FILE AND EFI PASS`，并到达五块青色标记；同次二进制日志和自动返回 Android 仍未通过，见 `reports/uefi-payload.md`。

Mu 基准固定为 `6b3d90b79e191e08171bfedd3980234a7e49d340`（v4.1），完整递归 checkout 位于 WSL 的 `/home/jia/ios-mix1-build/Mu-Silicium`。平台 overlay 与生成结果在相邻独立工作目录中，未修改上游源码。递归 pins 见 `reports/research-upstreams.lock.json`。

## 构建及验证

在项目 PowerShell 中依次执行：

```powershell
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/build-lithium-uefi.py --qemu
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/package-lithium-uefi.py --qemu
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py --strict-alignment
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py --corrupt-payload
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py --linux-early
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py --linux-early-trap
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-lithium-uefi.py --linux-post-mmu
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/build-lithium-uefi.py
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/package-lithium-uefi.py
python scripts/boot-lithium-uefi.py --boot
```

需先保留并验证 `out/journal-probe`：该采集器由 `build-entry-probe.py --profile journal` 和 `validate-entry-probe.py --profile journal` 构建验证。打包脚本检查其精确哈希。QEMU 与真机输出分开，QEMU 镜像显式标注禁止上机。最后一条命令才操作手机，检查单台设备身份、BL、镜像及源码一致性，仅临时启动。

`build-lithium-uefi.py` 显式选择 CLANGPDB，生成 BaseTools 配置、保留公共 DSC 在 Components 之前的库映射，移除未消费的图形动态 PCD 及关闭 RELEASE 日志的宏，并保留原许可证。实际组件由本地 DSC/FDF 明确枚举。生成规则、工具调用、源码与 FD 哈希记录在每个输出的 `inputs.json` 中。

## 同一次启动内的日志交接

1. aboot 将 shim、FD 与恢复 stub 加载至内核入口，将原厂内核加入口采集器的副本及原厂 initrd 作为明确声明的 ramdisk 加载。
2. shim 把 FD 复制至 `0x41000000`，把恢复 stub 复制至 `0x42400000`。UEFI 的堆、栈和可分配内存仍全部位于已经实测的原厂 Image 保留窗口内。
3. 平台 SEC 关闭匹配原厂 DT 的 APSS 看门狗，初始化 MMU/HOB。DXE 发布继承的 BGR24 显示的 Blt-only GOP、文本输出及只读 BlockIo。标准 DiskIo/FatDxe 挂载内存 FAT12 卷；BDS 完整读回 EFI，检查长度和 FNV64，正式发送 EndOfDxe 后调用 LoadImage/StartImage，核对 loader revision `0x20000` 和 Boot 指针。屏幕 PASS 仅在这些步骤全部成功时显示。FNV64 用于内容诊断，不是密码学签名验证；主机输入另有 SHA-256 与 mtools 逐文件读回。
4. 真正的定时器事件累计二十次并返回后，BDS 主循环取消本次事件，检查实际 FDT 中的 ramdisk 地址并把 initrd 改为 `0x44a00000`。取得最终内存图并调用 `ExitBootServices`，由定时器/GIC 驱动退出通知关闭硬件，记录状态后清理缓存，跳到位于原厂代码末尾之外的恢复 stub。
5. shim 已在进入 UEFI 前保存原始 EL1 状态。stub 在恢复复制覆盖信息页前把原始值装入寄存器，关闭翻译、缓存和定时器，恢复整个原厂内核加采集器副本。第八轮起另逐字读回比较源与目标，失败显示红块；通过后再恢复 SCTLR/TCR/MAIR/TTBR0/TTBR1/CPACR/VBAR/SP/DAIF。采集器在原厂内核清 BSS 前把 UEFI 日志页复制到预留 FDT 属性，随后接回原厂内核主体。
6. Android 返回后，从 FDT 属性读取二进制记录并核对本次构建标识。属性偏移 4608 存放本次原始 EL1 状态，和采集器读到的实际状态逐项比较；不以另一轮启动的地址作为恢复验收依据。最后普通重启确认诊断属性消失。

原厂代码先从分离副本完整恢复。扩大的 ramdisk 是 aboot 按 boot header 明确加载的输入，区间检查覆盖 `[0x42a00000,0x44e00000)`；其中 FAT payload 位于 `[0x44d00000,0x44e00000)`。原厂 initrd 的真实末尾恢复为 `0x44c28cf1`。新增的 12 MiB DXE Extra Heap 位于原厂 Image 已测保留窗内的 `[0x40400000,0x41000000)`，不把未描述 RAM 当作自由堆。

QEMU 使用虚拟 GIC 地址并省略硬件看门狗写入；原厂内核副本的正常继续入口被替换为一个停止指令。验证会比较恢复代码后的完整内核副本，检查原厂 initrd 地址、采集器记录及二十次计时事件。它不模拟 Qualcomm Linux、手机面板或实际 GIC。

`--strict-alignment` 在原始 SCTLR 中设置 A 位，并使用记录只按 4 字节对齐的另一个原厂 DTB。它曾捕获到状态副本的未对齐 64 位读取；副本读写现在全用 32 位访问。该场景报告为 `reports/uefi-qemu-strict-align.json`。`--corrupt-payload` 只在 QEMU 的临时副本中翻转 EFI 首字节，要求内容检查拒绝加载。真机脚本核对这三项与另三项Linux早期/MMU报告及当前输入哈希；正常、严格对齐场景实际执行 EFI 入口，负例要求不执行。

恢复文字检查点和底部青色块用于区分执行位置：第一个块表示已关闭 MMU（06），第二个表示复制、I-cache 同步及本版 CPU 完整读回比较通过（07），第三个表示原始状态恢复后、最终分支之前（08），第四个表示 Image 分支已经执行并到达采集器入口前（09），第五个表示采集器完成、即将进入 Linux 主体（10）。复制比较或采集器记录检查失败时另外绘制红色块并停止。读回比较是第八轮新增，不能据旧轮次的块数倒推通过。最后文字仍可能显示 `RESTORE 05 CACHE CLEAN`，须结合青色块数量判断。服务退出结果与 timer/GIC 状态另存 journal 偏移 84–127；模拟验证检查这些状态确实关闭。

固件没有 UFS、USB ADB 或触摸驱动，也没有在 SEC/DXE 崩溃后自动回传完整异常的兜底路径。正常执行日志回读与早期异常回读是不同的验收项。退出本固件进入 Android 的路径只用于本次有限诊断，不是已验证的 iOS 启动协议。

第十轮版本有九个原厂逐指令核验的 Linux 钩子：八块黄色为开启MMU前，第九格绿色为高虚拟地址入口。临时恒等/显示映射使诊断stub在开启MMU后仍可执行和绘图。短期异常向量在Linux接管VBAR前记录ESR/ELR/FAR，异常用第九格红色。QEMU附加场景分别验证MMU前、异常像素和真实MMU后的PC/SCTLR/绿色像素；启动门槛为六项报告与当前输入哈希匹配。用户已目视确认第十轮八黄、一绿、无红，手动恢复原厂。详见 `reports/uefi-linux-early.md`。该诊断现归档为辅助工具；下一阶段按 `reports/reuse-route.md` 复用同芯片平台代码并构建7195，尚未调用DarwinBoot Boot方法或执行XNU。
