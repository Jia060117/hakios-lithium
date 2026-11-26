# R38 perf-fixed02 / frequency11 主机准备

上一轮照片将故障定位到 UEFI 的 `LOCK_WAIT` 中的 `READBACK_ERROR`，当时第 10 次 PLL 写入的立即读回通过，未执行切换 MUX 的第 11 写。旧镜像未保留触发错误的轮询 MODE 原值。

本修订由主代理在 `ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h` 的等锁循环保存已有成功读取的首值、末值、次数、有效位、失配掩码和耗时，`ports/hakios-lithium/return-fastboot/cpu-perf-uefi-r38.inc` 在 UEFI 结束屏打印 `LITHIUM PERF UEFI lock`。未增加 MMIO 或计时调用；保留 `0x7ffffff8` 掩码、写入顺序、超时和停止条件。307.2 MHz 是实验目标；最多 11 次时钟寄存器写，不写供电设定。

实际 loader/Mu support02 构建成功，FV 无物理 UFS provider。命令：`scripts/build-hakios-r37-ram-support.py --revision perf-fixed02`；打包使用 `scripts/package-hakios-cpu-r38-clock.py --perf-readback`。新包为 `artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-11`，SHA256 `1a34e8be1e008a469828e01109841254791e07fe66292b701e1d914569cc0d1c`。与 frequency10 的内核、XNUP payload、两层 RAM APFS 及全部 boot-args 相同。Android boot header、XNUP、FAT、FD、PID1、交接与搬运读回通过。

实际 core/helper 的 ASan/UBSan 模拟验证为 525 项与 103 case / 23,169 项；测试源码哈希与 support02 输入一致。运行结果见 `reports/hakios-ios16-device-r38-perf-fixed02.md`。若前置检查失败，保留 UEFI 屏幕；自动 Fastboot 只适用于进入 XNU 后的正常计时退出。

Popper 独立复核了两份生产源文件与构建、测试输入的哈希及源码差异。主代理重新核对哈希、两包参数和组件读回。独立复核见 `evidence/cpu-r38-20261121/pll-readback-source/final02.md`。
