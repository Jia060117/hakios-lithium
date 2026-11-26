# R38 原生测温与双绘制线程候选

日期：2026-11-21，Asia/Shanghai。本阶段仅做一次只读 Fastboot 枚举。已接受启动次数仍为 HakiOS 78 / iOS16 47。

当前已体验的 UFS 桌面为 `phase4-r38-ufs-perf-low01`，镜像 SHA256 `bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6`。用户确认交互明显改善。本阶段保留该包作为回退基线。

## 已完成

1. 原生只读 TSENS 测温接入 UEFI 和 XNU，开关 `cpu_thermal_diag=1` 默认关闭。采集本机 stock DT 映射的四个 CPU 温度传感器；无 TSENS 初始化、阈值、校准或供电寄存器写入。每次最多 24 次读。无效结果输出 `native_mC=unavailable`，不伪造温度。
2. UEFI 在已有 307.2 MHz 低档 PLL 初始化前后各采样一次，经 200 字节 DT receipt 传入内核；XNU 在基准测试前、中、后及每五秒记录，正常结束时停止并排空回调后释放完整 VM allocation。日志前缀为 `LITHIUM CPU TEMP`。内核温度是硬件原生单位解码，校准来源和转换新鲜度标志仍为 0。
3. 实际完整 HakiOS 内核增量链接、独立 RAM loader/Mu 构建及最终整包读回成功。第一次内核编译发生头文件声明冲突，改用 `kern/misc_protos.h` 后成功，未删功能。
4. 双绘制线程候选仅改变 `fb_threads=1` → `fb_threads=2`。解压后的完整 Image 只有一字节不同；内核、loader、Mu、FAT、用户态 APFS、DTB 和 BootShim 均相同。两线程分担上下各 1020 行并等待完成；实际加速幅度尚未验证。

## 候选与运行顺序

### 1. 当前低档测温

目录：`artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-15`。

镜像 SHA256：`50d19aa239ff0b396d2e44acfaec6af8f1db19f050e2d33fe3daa0b3ed8cab39`，16,338,944 字节。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_spmi_diag=1 cpu_perf_fixed=1 cpu_thermal_diag=1 cpu_freq_diag=1
```

目标仍为已有 307.2 MHz 配置模型，最多 11 次既有 PLL/MUX 写入，无电压写入。隔离 RAM/PID1 hold；不启动 SpringBoard，也不访问物理 UFS。正常 180 秒 timer 后按已有协议排空日志、停止其他核、返回 UEFI，再延迟 10 秒进入 Fastboot；失败或 panic 保留摘要，不把失败自动改判成功。

准备好 Fastboot 后，仅运行一次：

```powershell
python -B reports/local/run-cpu-r37-ram-measure.py --boot --phase frequency --attempt-id r37-frequency-15
```

本次接受启动后 240–900 秒内，单次核验：

```powershell
python -B reports/local/run-cpu-r37-ram-measure.py --verify-return --phase frequency --attempt-id r37-frequency-15
```

检查 `LITHIUM CPU TEMP` 的 UEFI 前后样本、start、cpu_bench_mid、after_cpu_bench、periodic、stop，以及 raw/valid/reason/时间；同时检查 benchmark、timer、USB drain 和 Fastboot 终点。若控制器关闭或无 VALID，不提频，先解决测量。

### 2. 桌面绘制线程对照

目录：`evidence/cpu-r38-20261121/fb-workers/phase4-r38-ufs-fb2-01`。

镜像 SHA256：`cc8d2b52bd8672ff7ec9285882f1c33bf9012a020f67f2522c20bab280612a6d`，16,334,848 字节。

上机入口为 `python -B reports/local/run-ios16-r38-ufs-fb2-01.py --boot`。同一脚本已通过 `--verify-only` 主机检查。精确完整参数见候选的 `boot-args.txt`。唯一参数变化为 `fb_threads=2`，保留同一低档 CPU 设置、三核、触摸和 R20 显示。正常 UFS/Data 文件系统写入与当前桌面相同。该包没有自动 Fastboot，原 10800 秒 timer 不变。

比较相似的桌面滑动、后台卡片和应用返回操作；用相同日志窗口比较绘制 CPU/wall 耗时、提交速率和输入体验。后台负载及调度到较慢核心可能抵消收益，不预判双线程一定更快。

### 3. 下一档频率

384 MHz 尚未实现或上机。固定版本 MiCode 表和本机 rawbin0/rev2 fuse 模型显示，307.2 与 384 MHz 的 CPU 侧开环要求同为 585 mV（包含 15 mV aging）；当前共享 S11 的数字设定为 905 mV。两者属于不同层级，不把 S11 降成 585 mV，也不为满足“提频升压”的字面表述而盲目加压。

先取有效原生温度、核对目标档所需供电及 settled 状态；若确需升压，先完成并读回升压，再升一档频率；降频顺序相反。只推进 384 MHz，记录负载前中后温度，保留 307.2 MHz 包。四核尚未完成，phys0 仍被 UEFI/UFS worker 占用。

## 验证与并行记录

- Laplace：内核只读采样、回调停止和 MMIO allocation；17 个适配器场景 / 5401 checks，ASan/UBSan 通过。
- Darwin：UEFI 原生采样；RAM/UFS 各 17 个主机场景通过，并检查默认关闭和原有 11 次 PLL 写数。合成输入不是真机温度。
- Mendel：双绘制线程单变量打包、源码说明和读回。
- 主代理：receipt/正常结束集成、实际内核及 loader/Mu 构建、测温打包；核对当前源码与测试/构建输入哈希一致。独立解析最终 FD → LZMA → nested FV → FFS/PE32，确认 Fastboot DXE 精确一致且物理 UFS GUID 缺失。上机 runner 只做主机校验通过，未执行设备命令。保留 41 项冻结文件、已体验桌面包和原 git index。
- 主机最终核对：`wsl -d Ubuntu -u jia --exec /home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/verify-hakios-r38-thermal-host.py`。
- 构建入口：`scripts/build-hakios-r38-thermal.py`、`scripts/build-hakios-r37-ram-support.py`；封装入口：`scripts/package-hakios-r38-thermal.py`。实际命令、日志、结果与哈希见 `thermal-main/kernel-result-thermal01.json` 及 `perf-uefi-support06/ram-support.json`。

## 同次旧 USB 采集更新

low01 的原接收器于 14:01:09 以 `capture_failed / [Errno 32] Pipe error` 退出。保存的 15,349 帧 / 3,192,352 console 字节已独立重放一致，无缺口、重复、残帧或 device-lost。没有最终 timer/drain；手机物理终态未查询，不由断流推断关机、崩溃或自动返回。详见原运行目录的 `usb-audit-capture-ended.json`。

14:12:32 的一次只读 `fastboot devices` 返回空列表。当前等待用户将手机手动返回 Fastboot 并保持 USB 连接。测温和双线程效果、384 MHz 稳定性均未验证，不新增启动计数。
