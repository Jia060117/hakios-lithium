# HakiOS iOS 16 lithium 触摸：Step 3 主机实现审查

日期：2026-11-19。范围：并行规则更新、FT5x46 主机解析边界、QUP6 静态核对、触摸队列测试。没有查询、启动、重启或写入手机，没有刷写分区，没有运行 QEMU。

## 结果

项目规则已更新为：独立任务优先交给子代理并行处理，最多同时 9 个；共享接口和集成文件由主代理协调，子代理必须有独立写入范围，主代理复核文件与测试结果。

当前仍没有可以验收物理触摸的镜像。R19/R20 只有默认关闭的外围准入诊断；`touch=1` 只会输出 `platform_backend_unverified`、`mmio=0`、`irq_attached=0`、`hid_events=0`。用户报告手机在 Fastboot，但在真实触摸后端构建、检查和冻结前不启动旧包做无效测试。

## 已确认的硬件边界

- 匹配原厂 DTB 为 `artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dtb`，SHA256 `93dc50ef24a5a6e56c19346ce5c1aa43c11f1e1842970d150020cde6c3b79985`。
- 触摸控制器静态配置为 FT5x46/FTE8716 家族，I²C12/BLSP2 QUP6，地址 `0x38`，控制器基址 `0x075ba000`，总线 400 kHz；SDA/SCL pinmux 为 GPIO87/88。证据：`lithium-stock.dts:4844-4865,14221-14236`。
- 坐标上限静态配置为 1080×2040：`lithium-stock.dts:4877-4878`。
- IRQ 有冲突：通用 `interrupts` 指 GPIO105，厂商 `irq-gpio` 和活动 pinctrl 指 GPIO125；保存的 Android 驱动审计更支持 GPIO125，但没有运行时证据。不能把 GPIO105 或 GPIO125 直接当作已验证线路。复位 GPIO 为 89，供电和显示共享 L14/LAB/IBB，不能在没有所有权证据时擅自切换。
- 固定 Linux QUPv2 参考实现的组合传输是写寄存器、repeated START、读数据、最后 STOP；入口会写 QUP `SW_RESET`。仓库的 `qup-combined.h` 也会复位。QUP 软件复位与 GCC QUP6 块复位不同；是否能安全跳过入口复位尚无证据。

## 已完成的主机工作

`maintenance/hakios-lithium/peripherals-next/ft5x46-frame.h` 和 `ft5x46-frame-test.c` 现在提供主机侧 63 字节帧解码和按硬件 ID 的触点状态机：

- 解析 10 个 6 字节触点槽，ID `0xf` 终止；
- 接受 DOWN/UP/CONTACT，拒绝协议保留事件码 3；
- 依据 lithium DTS 拒绝超过 1080×2040 的坐标；
- 处理槽位重排、缺失触点自动释放、显式抬起、重复 ID、长度错误和输出空间不足；
- 所有拒绝路径保持输出和状态不变。

`0020-lithium-touch-batch-queue.patch` 仍是候选补丁，未并入生产内核。`test-touch-batch-patch.py` 会从补丁提取真实新增函数、从 HakiOS 驱动提取真实 `count_fingers`/`touch_feel_add`，再在临时宿主程序中测试；新增了超过 20 条、20 条成功、精确填满、满队列、uint32 回绕和损坏占用量用例。`git apply --check` 通过。

## 验证结果

以下测试在 WSL/Clang/ASan/UBSan 下通过：

```text
FT5x46 host parser/state tests: PASS (5 groups)
14 peripheral test suites: PASS
combined FIFO/QUP model: PASS
touch delivery model: PASS
IRQ affinity audit: PASS
A5xx fence packet model: PASS
exact patch-0020 function + HakiOS helpers: PASS
```

核心命令：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals.py --compiler clang
wsl.exe -d Ubuntu -- bash -lc 'set -o pipefail; cd /mnt/f/IOS-MIX1; ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 -B maintenance/hakios-lithium/peripherals-next/test-touch-batch-patch.py --compiler clang'
wsl.exe -d Ubuntu --exec clang -std=c11 -pedantic -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer "-fsanitize=address,undefined" /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals-next/ft5x46-frame-test.c -o /tmp/ft5x46-frame-test
```

完整 `test-hakios-lithium-peripherals-next.py` 本次没有作为总回归依据：它首先检查 R19 冻结合同，而工作区已有后续构建证据漂移，具体为 `reports/hakios-kernel-build.json`；因此脚本在冻结检查处停止。这个停止保护了旧合同，不能解释成触摸测试失败。独立运行的 14 组旧回归、5 组新增模型和精确补丁测试均通过。

R19/R20 和桌面基线产物仍保持：

- R19 镜像：`2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a`；
- R20-display 镜像：`3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`；
- R15 desktop baseline：`ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`；
- R19 集成内核 payload：`8e9209c1ff077ba6ad16cbd467c6cceb3cb6b16c0357667e513b913190fd9b4b`。

构建证据限制：当前 `reports/hakios-kernel-build.json` 记录的 `kernel_sha256` 为 `32cec5999d77e9e9bc1642b1e942f35f218e2983892fe3cbd5503684552a13b1`，与 R19 目录和 `host-admission-build-v3` 的实际内核哈希不符；因此受冻结保护的 next-runner 在冻结检查阶段停止。现有构建日志也有上游 kext symbol-set 检查失败记录，包装脚本按既有规则继续报告产物。此候选不包含实际 FT5x46 producer，本报告不将其称为触摸包或完整设备验证。

## 并行执行记录

- 设备树/Android 资源审查：完成；主代理抽查了 DTB 哈希、DTS 行号和 inventory 路径；结论保留 GPIO105/125 冲突及运行时未验证。
- HakiOS 输入链审查：完成；确认 `/dev/hakios-touch` → backboardd 虚拟 Digitizer 路径存在，但没有真实 producer。
- QUP6 参考审查：完成；确认 repeated START 序列和入口 `SW_RESET`，没有把静态资料当运行时证据。
- MMIO/GIC 接口审查：完成；确认 `ml_io_map`、`gic_v3_spi_attach` 和当前缺少 lithium GPIO/TLMM producer。
- FT5x46 帧解析实现：完成；主代理复核并收紧坐标/事件边界，ASan/UBSan 通过。
- 0020 精确补丁测试：完成；子代理补边界测试，主代理复跑退出码 0。

独立代码复核未另行完成：负责 HID 链路的代理已结束其只读审查，但没有返回对本次 parser 边界改动的独立审查结果；主代理通过源代码检查并实跑 ASan/UBSan。代理运行耗时统计未提供，报告不估算。

## 下一阶段和上机门槛

下一阶段才实现默认关闭的 QUP6/FT5x46 生产后端：先完成资源所有权检查、QUP combined transfer、identity 读取、有限原始帧日志，再接入 `hakios_touch_post`。不能把主机模型直接打包成真机驱动。

新后端冻结并通过生产构建前，不上机。冻结后只做临时启动，建议顺序为：

1. 基线包，确认当前显示基线仍能进入主屏；
2. 新包 `touch=1 touch_log=1`，只看控制器 ID/固件、QUP 初始化、错误计数和 IRQ 计数，不发送 HID；
3. 新包加 `touch_poll=1`，对比空闲、单点按下/抬起和短滑；
4. 仅在原始帧可信后接入队列，验证 DOWN/MOVE/UP、坐标四角、抬起释放和下游 HID；
5. 最后才考虑 GPIO125 IRQ 路径，并将 GPIO105 作为待证伪候选，不同时改变多个变量。

每次测试都保持 `bootcpu=1 cpus=1 ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334`，仅增加一个触摸开关。触摸控制器 ID、QUP 所有权、GPIO125 实际投递、原始帧一致性、队列样本和屏幕响应目前均为未验证。
