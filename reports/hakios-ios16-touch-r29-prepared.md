# R29：QUP v2 初始化与条件式单次 ID

记录：2026-11-19T23:43:24.184263+08:00。本报告范围为主机准备；尚无 R29 手机操作。后续同次运行另见 device 报告。

镜像：`artifacts/hakios/lithium-ios16/phase4-r29-touch-qup-init/lithium-hakios-ios16-r29-touch-qup-init.img`；16285696 字节。
SHA256：`2dec085c75a29ea770a56231ff359ae76da0d13aa36e11836d657ea1a03b9d65`。

## 实现与证据边界

- 新 `touch-qup-init-core.h`：只接收完整且一致的 R28 继承状态指纹；一次 SW_RESET→检查 VALID/RESET→普通 STATE RESET→原厂 CONFIG207/ERREN3c/OPMASK300/MASTER1→逐项读回；配置后 OP 必须严格为零才调用未改动的旧 ID 核心一次。
- 固定 MiCode 原厂顺序有来源支持；没有来源保证 reset/config 后 OP 一定为零。若 0xc0 仍在则保存证据停止，不直接放宽掩码、不重置重试。
- `touch-qup-init.inc`：重用既有板级/映射，重新核验单核 affinity1、线程/开中断、八槽非零映射和 GIC 可见状态；每次写前检查 enable/pending/active。仅新显式开关准入，回调结束后封闭。
- 前、复位后、配置后/ID 前、终态四组双轮快照，逐项 valid/unread；写入顺序、实际发出及按时返回、首个失败访问与计数有独立记录。逻辑门禁停止可做有界终态观察，访问/时间失败后不补救 MMIO。
- `touch_qup_init=1` 默认关闭，与活动 snapshot/ID 开关互斥，要求精确 RAM/180秒参数。旧 Stage1、ID、snapshot 和0022 PID1 patch逐字节保留。
- 基础 ERREN=0x3c 真实启用 QUP 错误源；服务 mask 和 GIC 只读可见状态不等同全局屏蔽/独占证明。没有 GIC 写、外部触摸 GPIO 复位、供电切换、IRQ/HID 接入。
- 初始化总体100ms、每阶段20ms、4096次返回访问、256次共享poll/每次10us；旧ID仍有原20ms门限。软件限制只覆盖会返回的调用，不能约束永久MMIO/map/barrier挂死或abort/SError/XPU。

## 实际验证

- 完整HakiOS/Mu/D22AP/413job、82来源边界、46指令扫描；25+17=42套ASan/UBSan通过。
- 新core6095场景、实际adapter24632场景；原ID1838/adapter21884、snapshot622+7/adapter4611、clock5241保持；实际PID1完整main374场景。
- 82组打包验证独立复跑；100合并源哈希、整包与实际APFS程序/权限读回通过。
- 历史R9几何hash总门禁未解决，不冒称所有历史回归通过。既有上游kext符号集exit2为实际链接之后的已记录行为，未删功能绕过新编译错误。
- 命令：`pwsh -File reports/local/run-r29-host-step.ps1 -Task <prepare|config|deps|review|kernel|support|userspace|build-check|peripherals|package>`，大任务顺序执行；16GiB/12CPU WSL，任务14GiB/1200%，JOBS12。
- 独立读回：`wsl -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/evidence/touch-r29-20261119/main/final_offline.py`。

## 并行执行与主代理复核

- Ptolemy：Fixed MiCode/Linux/stock-DT protocol and reset/config/clock evidence。主代理复核：Read original driver215-295/704-724/1870-1903/2870-2885 and source hashes; OP-zero guarantee absent, error-enable caveat retained。
- Planck：Portable initializer core and actual default/enabled tests。主代理复核：Reviewed ordering, monotonic poison boundary, read/FIFO and write phase whitelist, shared limits and valid failed offsets; full final 6095 cases independently rerun by main regression。
- Meitner：Actual adapter/core, board/GIC/mapping/lifetime fault injection tests。主代理复核：Initial compile-blocked history retained; fixed fixture preinclude macros/nonconst observer to match production, without weakening assertions. All seven groups/24632 cases independently rerun。
- Locke：New R29 packager and synthetic validators。主代理复核：Read exact parent/protected/provenance/argument/APFS gates; independently reran final82 tests and full image readback. No package main or device operation delegated。

各代理写入范围分离，共享接口及构建/设备操作由主代理负责。初次测试装配宏/const接口错误已保留，修正后完整执行全部7组；没有通过排除测试宣称通过。核心最初主机命令引用错误已更正；可选清理遭自动审核拒绝后未重试，测试二进制保留，不影响验证完成。

## 隔离包及保留

复用固定不含物理UFS provider的Mu、只读loader/FAT与双4MiB RAM卷：resident=Boot/md0/stage0，ramdisk=redwood/md1/init。长期UFS路线未改变，本轮包不含存储参数；引导前原厂固件/TZ自主行为不在观测内。
R20-display为唯一显示基线，1080×2040 BGR24/stride3240、360×680 points/scale3保持。R15/R20/R25/R26/R27/R28镜像哈希保留；原索引/暂存内容不改动。主机准备将导出精确可逆补丁。

## 精确参数与上机范围

```text
bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1 touch_clock_init=1 touch_diag=1 touch_qup_init=1
```

本次候选请求首次执行一次QUP软复位与限定I2C配置；严格条件全部满足才做一次0x38/A3读取。属于可能改变控制器继承状态的实验，不是只读快照；所有权、电气、真实复位/ID/触摸均未验证。
用户最新已要求继续并表示Fastboot可测试；任何设备操作前主代理仍须说明这个对应范围。只允许身份/状态只读预检、单次临时boot和USB采集，不扩大为分区/刷写/ADB/Android/Recovery/QEMU，不自动重试。

## 剩余风险与下一关

首先核对SW_RESET后、配置后的实际有效寄存器及严格门禁；若拿到ID，再据真实ID推进控制器报告与HID。若0xc0持续或任一检查失败，用同次前后证据定位，不把主机通过当硬件通过。无需用户守屏或反复拍摄瞬时日志。
