# R28：事务前QUP/FIFO只读快照

记录：2026-11-19T23:04:38.343341+08:00。范围为上机前；设备操作为零，后续运行另见同轮device报告。

镜像：`artifacts/hakios/lithium-ios16/phase4-r28-touch-snapshot/lithium-hakios-ios16-r28-touch-snapshot.img`；16281600字节。
SHA256：`9a01c3db68164949a8f093a3625dbea9c852047b35417b1fe3597be6bf4342d1`。

## 实现及边界

- `ports/hakios-lithium/peripherals/touch-snapshot-core.h`：独立只读回调，17个寄存器两轮，共34读；20ms总预算；逐项有效位、原始值、软件时间和双轮差异。未知OP不截断读取，版本不符或访问/预算失败即停止。
- `touch-snapshot.inc`：复用Stage1映射、重新观察板级/GIC入口；不读取FIFO数据端口，无写回调。共享one-shot并封闭返回后的迟到回调；读取失败后不补救MMIO。映射表非零和长度契约重新核验。
- `boot-options.h`/`kernel.inc`：默认关闭的touch_snapshot=1；精确RAM参数，和活动ID探测互斥；快照结束不会调用ID。
- R27 ID core/adapter、R26 Stage1、0022诊断PID1 patch逐字节保留。R20-display为唯一显示基线。
- 两轮相同只证明这些先后读取相同，不证明原子性/所有权；count端口读副作用、硬件单位仍未验证。软件预算不处理永久MMIO/map/barrier挂死或abort/SError/XPU。

## 验证

- 完整HakiOS/Mu/D22AP/413job、82来源边界、46指令扫描；22+17=39套ASan/UBSan全通过。
- 新core622+默认7、新实际adapter4611；保留ID1838/adapter21884、clock5241、实际PID1 main372；46组打包隔离检查，95合并源哈希及APFS/整包读回通过。
- 主命令（PowerShell7）：`reports/local/run-r28-host-step.ps1 -Task prepare/config/deps/review/kernel/support/userspace/build-check/peripherals/package`逐项顺序执行。
- 独立读回：`wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/evidence/touch-r28-20261119/main/final_offline.py`。
- 历史R9几何hash总门禁未解决；不冒称所有历史回归通过。已有kext符号集退出2由上游wrapper明确标记，实际内核链接通过。

## 并行执行和主代理复核

- Herschel：Read-only core, 622 enabled+7 default cases and writer/delay negative compile checks。主代理复核：Core/API hashes checked and exact-source core/default tests independently rerun; interface mismatch corrected before final build。证据文件时间窗口约372.1秒（不是计费/执行时长）。
- Harvey：17 offsets, fixed shared-QUP bit7 definition and geometry evidence。主代理复核：Main read fixed Linux SPI blob and independently matched all17 MiCode offsets; I2C/reset/side-effect caveats retained。证据文件时间窗口约149.0秒（不是计费/执行时长）。
- Tesla：New R28 packager, 46 synthetic tests and 112 source audit checks。主代理复核：Main independently reran46 groups; normal build/provenance and full-image/APFS readback verified separately。证据文件时间窗口约612.2秒（不是计费/执行时长）。
- Hume：Actual adapter tests and stale project boundary review。主代理复核：Excluded-group4584 result rejected as full pass. Main preserved strict cases, added mapping-contract validation and ran complete4611 cases; no assertion weakening。证据文件时间窗口约690.0秒（不是计费/执行时长）。

共享文件由主代理统一修改；没有写入冲突。初期API不一致和回调窗口已修正。代理部分排除mapping的4584结果未充当全通过；最终4611保留全部严格用例。索引stat-cache被刷新后，主代理核验1330项staged内容和patch相同，再还原原始索引字节，恢复证据保留。

## 精确启动参数

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_snapshot=1
```

本轮复用不含物理UFS provider的固定Mu、只读loader和双4MiB RAM卷；resident=Boot/md0/stage0，ramdisk=redwood/md1/init。长期系统仍在UFS，本轮不访问/刷写分区。Stage1仍可有原有时钟/引脚字段写；零写仅指新QUP快照模块，不能宣称整机启动零MMIO写。

## 当前来源结论和后续风险

- 全17偏移与固定MiCode头匹配；Linux SPI-QUP共享块将bit7命名IN_FIFO_FULL，bit6为OUT_FIFO_FULL。MIX I2C/RESET语义未证明，不能直接扩旧ID掩码。
- 上机需完整valid0/valid1和实际STATE/CONFIG/IOMODES/计数；未读打印unread，不填零。异常则停止，不自动重试。
- 若证据表明初始化缺口，后续单独讨论原厂QUP SW_RESET流程；此包不包含该操作，也不等同触摸GPIO复位。
- 物理触摸、真实I2C/IRQ/HID均未验证。R15/R20/R25/R26/R27冻结字节保留。
