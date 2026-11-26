# 扩大 ramdisk 与 initrd 交接的独立诊断

日期：2026-11-12。目标仍是 MIX 一代的 iOS 14.8 / 18H17。

当前结果：独立诊断镜像已构建，14 个 QEMU 用例全部通过，Windows 本地输入校验通过，**一次真机临时启动和普通原厂重启对照均已验证。** 完整副本槽、initrd 内容和地址调整已取得同次启动日志，尚未验证 UEFI 初始化或恢复复制。

## 实验目的与范围

第三次 UEFI 临时启动未返回 USB。用户照片随后证明它至少进入 BDS、GOP 自检通过且计时累计 20 次，但无法区分之后的检查、缓存清理、原厂内核恢复或 Linux 初始化中的停止位置。单独测试扩大的 ramdisk 和 initrd 地址调整，可缩小后续调查范围。

本镜像保留原厂内核主体和已有日志观察器，在其后追加 5,982 字节的诊断。入口改为先执行追加代码，再进入日志观察器，最后继续原厂 Linux。原厂内核不被 UEFI 覆盖，也不执行恢复复制。

aboot 输入的 ramdisk 与第三次 UEFI 方案保持相同布局：先放 32 MiB 的原厂内核副本槽，槽内包含已验证的日志观察器；随后是 2,264,305 字节的原厂 gzip initrd。启动时检查：

1. 实际入口为 EL1，MMU、D-cache、I-cache 关闭，FDT 为已测地址 `0x42800000`。
2. FDT 的结构、属性宽度及 `/chosen/linux,initrd-start`、`linux,initrd-end` 均符合输入布局。完整验证通过前不改写任一属性。
3. `0x42a00000` 起整个 32 MiB 副本槽，以及 `0x44a00000` 起的原厂 initrd，分别通过完整 FNV-1a 64 位校验。它用于检测装载差错；镜像身份另以 SHA-256 绑定。
4. 把 initrd 起点改为 `0x44a00000`，终点保持 `0x44c28cf1`，保留各属性原有的 4/8 字节宽度。
5. 还原入口 SP、DAIF 和 VBAR，x0 为 FDT，x1–x3 清零，进入原有日志观察器。后者把本次日志页复制到 FDT 属性，再继续原厂 Linux。

诊断未初始化 UEFI、MMU、GIC、定时器或看门狗；因此它通过也不代表第三次 UEFI 的原厂内核恢复已经成功。早期异常导出仍未完成。

## 固定输入与内存

- 镜像：`out/handoff-probe/lithium-handoff-load-probe.img`，57,135,104 字节。
- 镜像 SHA-256：`53b75806612df3e0a35b95efb2d384e849aac187395b7c54adc098f117dcf417`。
- 追加代码 SHA-256：`66509715de27c903d68e5b8defb628dbc67a51f01d80d918386522627d393b4a`。
- 构建标识：`0xfb8e8fdd417a1962`。
- 编译工具：WSL 的 clang/LLD，`aarch64-none-elf`、ARMv8-A、仅通用寄存器，无 libc 或 UEFI 依赖。
- 诊断入口：`0x42079000`；在原厂 Image 声明保留窗口末尾预留 8 KiB 栈空间，栈顶 `0x424b3000`。链接器禁止代码进入该栈范围。
- 沿用原厂 Image 保留窗口 `[0x40080000,0x424b3000)`、FDT 窗口 `[0x42800000,0x42a00000)`，以及明确声明的扩大 ramdisk `[0x42a00000,0x44c28cf1)`。静态检查对照已采集 RAM 和固定 reserved-memory。
- 沿用之前日志观察器使用的显示保留区日志页 `0x84000000`；不把该页描述为自由 DDR。

完整构建输入与哈希在 `handoff-probe-build.json`。原有三轮 UEFI 镜像和实验记录未被本诊断覆盖。

## QEMU 验证

`handoff-probe-qemu.json` 的 14 项均通过：三种正常地址属性宽度组合；错误 FDT magic、过大总长、越界结构、未知 token、缺失起点、错误起点、错误终点、错误属性宽度、损坏 Image magic、损坏副本槽、损坏 initrd。

正常用例确实执行追加的生产诊断代码及已有日志观察器，校验日志构建标识、两个完整装载校验值、入口状态恢复、initrd 地址和其余 FDT 属性不变。拒绝用例检查原因码及整个 FDT 未被改写，覆盖了“起点合法而终点错误时也不能先改起点”的情况。

QEMU 仅将主内核的原厂 Linux 继续入口替换为 `b .`，副本槽仍保持生产输入。它没有执行 Qualcomm Linux、原厂 aboot 或真实屏幕。模拟通过不能证明真机扩大 ramdisk 的装载位置、aboot scratch 使用或 Android 启动。

## 屏幕与日志

使用已采样的 1080×2040、stride 3240、BGR24 画面，在顶部显示三位数字。真实方向与可见性仍需实物确认。

| 数字 | 含义 |
| --- | --- |
| 001 | 进入诊断并初始化本次日志 |
| 002 | FDT、输入地址和 Image magic 通过，开始全量校验 |
| 003 | 副本槽与原厂 initrd 的全量校验均通过 |
| 004 | initrd 属性调整完成，准备进入原厂日志观察器 |
| 101–111 | 失败；数字减 100 为原因码，对应上述 FDT/地址/内容检查 |

日志页标记为 `MIX1HANDOFFPROB1`，与 UEFI 的标记不同。结构版本、阶段和原因码在偏移 16–31，FDT 地址、原始 ramdisk 起止地址和两个 FNV 值在 32–71，原始 SP/SCTLR/DAIF/VBAR/EL 在 72–111，构建标识在 112–119。成功时阶段为 4、原因码为 0。

## 真机结果

2026-11-12 20:34 执行固定镜像，Fastboot 接受后约 37.34 秒进入原厂 Android。同次二进制记录的标记、构建标识、stage=4、reason=0 和两个完整 FNV 值均匹配；initrd 实际范围为 `[0x44a00000,0x44c28cf1)`，原厂观察器无捕获异常。

随后普通重启的 boot ID 已变化，原厂版本、启动完成状态及诊断属性消失已确认。首轮脚本末尾错误地假定 `adb exec-out cat` 会返回远端缺文件退出码；本机 Android 7 实际返回 0，并将缺文件错误放到 stdout。已改用显式 shell 缺失标记并独立复核末尾检查，无需重新执行诊断启动。完整结果见 `handoff-probe-device.json`，原始命令与记录仅保存在其 `local_log_path`。

本次结果证明该扩大 ramdisk 布局及 initrd 地址调整能够在本机进入 Android。它不包含 UEFI 初始化、恢复复制或相关缓存/中断交接，不能据此声称第三次 UEFI 的故障已修复。

## 复现与下一次上机

先恢复原厂 Android 的 USB 连接并检查屏幕线索；不在仍无连接时启动实验。

```powershell
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/build-handoff-probe.py
wsl -d Ubuntu --exec /home/jia/ios-mix1-build/venv/bin/python /mnt/f/IOS-MIX1/scripts/validate-handoff-probe.py
python scripts/boot-handoff-probe.py --check-only
python scripts/boot-handoff-probe.py --boot
```

前三条完全离线且不接触手机。最后一条核验唯一授权 lithium、原厂版本、Android 启动完成、Fastboot product 和 BL 解锁，随后只使用 `fastboot boot`。它在同次 Android 启动中检查日志与实际 initrd 属性，再普通重启确认原厂启动和诊断属性消失。设备详细标识与原始二进制仅记录到忽略的 `reports/local/`。

本实验已通过。下一轮给 UEFI 恢复路径添加细分屏幕检查点，并把交接从计时回调移到 BDS 主循环；依据实际检查点继续分离缓存清理、恢复复制和 Linux 交接。当前不需要 root、清除数据或刷写分区。最终的 SpringBoard、触控和 iOS 系统应用目标尚未达到。
