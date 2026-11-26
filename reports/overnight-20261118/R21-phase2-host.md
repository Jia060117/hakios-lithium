# R21 阶段 2：隔离主机模型与测试

> **后续状态更新（2026-11-19）：** 本文记载的是 R21 早期隔离主机测试。当时 runner 的 R19 冻结输入门槛已由后续 R20-display/R15 基线门槛取代；R19 已因显示分辨率错误归档，不再复测。当前候选包和真实触摸后端状态见 [R21 Stage 0 最终核验](../hakios-ios16-touch-r21-host-final.md)。

日期：2026-11-18。范围：只做源代码模型、主机测试和证据导出。没有查询、启动或刷写手机；没有 ADB/Fastboot 设备命令、userdata/分区操作或 QEMU。R15/R19 启动镜像保持原样。

## 交付

- `maintenance/hakios-lithium/peripherals/touch-queue.h` 新增唯一的有理数坐标变换 `lithium_touch_map_fraction()`。队列和独立 digitizer adapter 共用同一份旋转、viewport 与物理/逻辑 geometry；`touch-digitizer-test.c` 加入与 16-bit HakiOS queue ABI 量化精度一致的交叉检查。该测试使用合成输入，不证明面板校准或真实触摸。
- `maintenance/hakios-lithium/peripherals-next/` 包含四个主机侧原型：QUP 组合传输候选、整帧触摸交付状态模型、SPI affinity 审计模型、A5xx fence packet 编码模型。它们没有接入活动内核，也没有设备 MMIO、GPIO、I²C、IRQ、GPU DMA 或 IOHID 后端。
- `maintenance/hakios-lithium/peripherals/gpu-submit.h` 与对应测试增加 IOVA aperture 边界/对齐、五字 ring 容量、畸形 ring fail-closed、watchdog recovery 条件覆盖。`SOFTWARE_FALLBACK` 是主机模型状态；并不实现实际 GPU reset 或屏幕回退。
- `scripts/test-hakios-lithium-peripherals-next.py` 一条命令运行已有 14 套与新增 4 套 ASan/UBSan 主机测试，并校验 R19 冻结合同、149 项证据快照与 R15/R19 镜像哈希。

## 验证

命令：

```text
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-lithium-peripherals-next.py --build-dir /mnt/f/IOS-MIX1/artifacts/hakios/lithium-ios16/overnight-next-20261118/peripheral-tests-r4
```

结果：退出码 0，14/14 既有套件与 4/4 新增套件通过，C11 `-pedantic -Wall -Wextra -Werror`，ASan 与 UBSan。新增覆盖包括 QUP 所有长度 1–32 的 MMIO 故障注入、触摸批次拒收/重试/失流、988 个 SPI offset/亲和性边界和 A5xx fence packet golden/boundary vectors。完整命令、逐项 stdout/退出码/二进制哈希及源码 SHA256 在 `artifacts/hakios/lithium-ios16/overnight-next-20261118/peripheral-tests-r4/passed.json`。测试执行前后均核验 R19 的 149 项快照和输入哈希不变，以及 `r15_and_r19_images_unchanged=true`。

R15 桌面基线实文件 SHA256 仍为 `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`；R19 候选实文件仍为 `2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a`。未替换或覆盖任一镜像。

## 未完成/不得据此上机启用

R19 内核 hook 目前仅为默认关闭的 admission 诊断；没有触摸控制器/ QUP/TLMM/电源/IRQ 后端，没有物理 digitizer producer；GPU stage 0/1 也没有安全 MMIO、电源、时钟、SMMUv2、固件/PAS 或真实 fence/IRQ 后端。因此 `touch=1`、`touch_poll=1`、`gpu_stage=0/1` 不能当作可工作的硬件测试选项。现有 C helper 和 mock suite 不组成可刷写的功能镜像；本阶段没有构建新启动包。

回退：源码仅通过 `R21-phase2-host.patch` 提供独立差异；可不应用该补丁保留 R15/R19 镜像与工作树。ASan/UBSan ELF 留在本机 ignored artifact 目录，不进入补丁。
