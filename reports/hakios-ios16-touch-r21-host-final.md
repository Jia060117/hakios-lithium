# R21 Stage 0 主机包最终核验

日期：2026-11-19（Asia/Shanghai）。R19 已上机但分辨率错误，归档且不再复测；R20-display 是唯一显示基线：1080×2040 BGR24、stride 3240、360×680 points、scale 3。

R21 只读 Stage 0 包已在新的 WSL 外部目录完成 loader、Mu、整包组装和独立读回。最终镜像位于 "/home/jia/ios-mix1-build/r21-stage0-package-20261119-g2/lithium-hakios-ios16-r21-stage0-readonly.img"，SHA-256 为 "07d9e4cd10ee37854d6fd65df5f8fe36d8d836d1212c77aeca6491a51d82c6f8"。

## 主机证据

- HakiOS kernel："f19ef6e026a10bef44003d91984c44eb475b000bc952f68d9247b3e2271b63b1"，来源为 pinned HakiOS "63fc2b4633bac3af8e57dadab9f5a8d4cb940438" / XNU 8796.121.2。
- loader EFI："efaeb95e73680922da42d8719b3160be1beb6cc7fcbdeb2a42e91a08072013f5"；AArch64 PE32+，基址 "0x40400000"。
- Mu FV："3c54f75e5d2fb04403c5ccd7f89c27dd8aab9dfdb663a30ef5dc195887f0b097"；Mu commit "6b3d90b79e191e08171bfedd3980234a7e49d340"。
- D22AP 原始 "DeviceTree.d22ap.im4p"："51e7e42b74fb5e4cf5c7c1eed30b3d79ec829edfe8c0884c2e7839ddf62a4853"；loader 使用的生成 "identity.h"："dd9ce97930dfe40fae6f01d52085197c679fea09a117a9376df947b0a6d5b59f"。两者分开记录，未把容器文件哈希误当成原始 im4p 哈希。
- R20-display 镜像哈希仍为 "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"。R20 root/resident 用户空间在包中逐字节复用并通过哈希核验；本轮没有重新构建 413 项用户空间。
- 包内 "kernel"、"loader.efi"、"lithium.fd"、XNUP、Android header、DTB trailer 和 FAT "HAKIOS.EFI" 均通过读回检查。
- overlay 的 10 个文件逐一与最终 kernel/loader/Mu 构建输入进行 SHA-256 比对；"bootargs.txt" 与最终只读 cmdline 一致。
- loader selector 的 ASan/UBSan 测试已绑定到本次 "build.json"，覆盖 busy 拒绝、非法阶段、一次性消费、参数冲突和容量不足。
- 12 个 R21 外围候选套件、精确 0020 队列补丁、ASan/UBSan、只读 UFS 过滤、loader selector 和包检查均通过。

本包保留 R20 的 "bootcpu=1 cpus=1"、UFS root、1080×2040 显示参数和 USB 日志；移除 "var_hfs=ufs_darwin_data"、"ufs_write=1"、"hfs_bench=1"。触摸默认关闭，Stage 0 只输出静态板级信息，Stage 1–3 fail-closed，不访问 QUP/TLMM MMIO。

## 设备边界

本轮未查询、启动或刷写手机，未写 userdata，未运行 ADB/Fastboot 设备命令，未运行 QEMU。这个包是**只读 Stage 0 主机包**，不是实体触摸包，也不具备触摸验收资格。QUP6 ownership/clock/pinmux、FT5x46 runtime ID、GPIO105/125 选择、GPIO89 reset、电源 handoff、真实帧和 HID 交付仍未验证。

下一步先实现并主机验证默认关闭的 QUP6/FT5x46 producer；只有真实触摸后端完成完整 HakiOS 构建、包检查和冻结后，才进行一次不刷分区、不写 userdata 的 Fastboot 临时启动。

## 基线对齐后的复核

按用户确认将 R19 标记为已测试但显示分辨率错误，并归档、不再复测。外围回归入口改为校验 R20-display 与 R15 冻结基线，不再要求 R19 历史快照通过。

- 通用主机回归：既有 14 套与后续 5 套，共 19 套 ASan/UBSan 测试全部通过。
- R21 候选回归：12 套、0020 实际队列函数、生成 overlay 和基于当前 g2 `build.json` 的 loader selector 均通过。
- 独立整包读回检查在一次性临时副本上通过；原外部包的镜像与 `checks.json` 哈希保持 `07d9e4cd…` 与 `45e63cac…`。
- 手机状态仍未查询；未执行设备命令、分区操作或 QEMU。R21 仍只是只读 Stage 0 主机包，实体触摸包未就绪。
