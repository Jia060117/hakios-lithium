# R27：最小触摸芯片 ID 读取包（主机准备完成）

记录：2026-11-19T22:26:14.157964+08:00。本报告范围为上机前，手机操作数为零；运行结果另见同轮 device 报告。

镜像：`artifacts/hakios/lithium-ios16/phase4-r27-touch-id/lithium-hakios-ios16-r27-touch-id.img`；16277504 字节。
SHA256：`a029ad76623e957661988893ca84b885a1162f771cf21ae90dad1d110d6ff7f3`。

## 本轮改动

- `ports/hakios-lithium/peripherals/boot-options.h`：独立 `touch_id_probe=1`，严格 RAM/参数门禁；默认不开启。
- `touch-id-core.h`：按原厂连续 TX9/RX3，0x38/A3 单次读、100kHz 名义分频、正向 RX 完成/STOP 核验；失败不恢复、不重试。
- `touch-id.inc`：复用 Stage1 QUP 映射，重新观察九个时钟/引脚字段，只读检查 INTID138 禁用/活动/入口 pending；每次 QUP 写前重新检查。
- R26 Stage1 和 0022 PID1 patch 逐字节不变；无 SW_RESET、触摸复位、供电切换、GIC 写、IRQ/HID 接入。
- 修正和复现证据：`evidence/touch-r27-20261119/main/review-corrections.json`。初版构建隔离后重跑全部正常构建。

## 构建与测试

- 构建命令：PowerShell7 `reports/local/run-r27-host-step.ps1 -Task prepare/config/deps/review/kernel/support/userspace/build-check` 逐项执行。
- 测试命令：同入口 `-Task peripherals`；包检查 `-Task package`；独立读回 `wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/evidence/touch-r27-20261119/main/final_offline.py`。
- 82 来源边界、完整 HakiOS/Mu/D22AP/413-job userland、46 指令扫描；19+17=36 套 ASan/UBSan 全部通过。
- ID 核心1838、适配层21884、时钟5241（含原Stage1 351）、PID1完整main 370；11组打包隔离检查。90项合并测试源哈希、实际APFS程序/权限、整包各层读回通过。
- 历史R9几何快照总回归未解决；不冒称所有历史回归通过。已有kext symbol-set退出2由上游wrapper明确区分，正常内核已链接。

## 并行执行记录

- Nash：fixed vendor protocol evidence；主代理复核：Inspected vendor FIFO/count/state/clock lines; rejected promotion of split candidate and unsupported ownership claims。
- Plato：new production I2C core and 1838-case fixture；主代理复核：Verified final source SHA and independently reran enabled/default tests and full regression; conservative pre-load flag stop retained。
- Goodall：independent parser/topology audit and new packager；主代理复核：Fixed reproduced separator/duplicate-rd gaps; corrected base report schema predicate; independently reran 11 package unit groups。
- Godel：GIC audit and 21884-case actual adapter test；主代理复核：Checked address arithmetic and actual GIC source; confirmed/fixed late barrier accesses, kept strict checks, independently reran full regression。

各代理写入范围不重叠，四个代理均已关闭。共享解析/集成/构建/契约由主代理修改。GIC测试初次fixture失败和第二次两项真实屏障失败均保留，修正后未放宽断言。

## 精确上机参数

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_id_probe=1
```

此次使用无物理UFS provider的固定Mu、只读loader和双4MiB RAM卷；resident=Boot/md0/stage0，ramdisk=redwood/md1/init。长期系统仍在UFS，不改变长期路线。仅进行一次临时启动，不查询或操作分区。180秒内核timer负责结束，无需守屏。

## 剩余风险及下一步

- Actual I2C acknowledgment/raw chip ID and target power/reset/electrical readiness。
- No-SW_RESET inherited clean-state equivalence, hidden sequencer state and hardware FIFO/tag/STOP behavior。
- Secure-world ownership/exclusion and full GIC security visibility; a disabled readback is only an observation。
- Actual nominal XO/SCL frequency and duty cycle。
- Permanent MMIO/map/barrier stalls, external abort/SError/XPU are uncontained by software deadlines。
- Initial userspace boot_arg read failure can miss the opt-in and follow normal init; unchanged R26 caveat。
- IRQ wire GPIO105/125, physical reports/HID/backboardd/UI input are unverified。

只有完整返回字节、完成/STOP和结束日志才能记为单字节事务观察；不能据此宣布真实触摸可用。预检或启动结果异常即停止，不自动重试。R15/R20/R25/R26冻结镜像均保留。
