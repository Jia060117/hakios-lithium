# iOS16 R6 真机联合调试结果

R6 已正常定时返回 UEFI，用户确认手机停在结束摘要。桌面尚未进入；明确停点是 SpringBoard 主线程在控制中心相关初始化中长期等待用户锁，尚未进入主运行循环。锁持有者和根因仍待定位，不能仅凭该栈宣布死锁。

## 日志已收完，范围明确

后台 USB 采集未因对话中断停止。2026-11-17 00:53:45 临时启动一次，USB 从 00:53:46 采集至 01:43:28；启动到接收结束约 49 分 42.876 秒。最终控制台 **325,433 字节 / 7,373 帧**，原始帧 561,369 字节。用会话冻结接收器重新解码，与全部 console.log 逐字节一致；缺口、重复和不完整帧字节均为 0。

捕获从 Darwin 内核横幅开始，包含原始 launchd PID1 的日志初始化，最后收到：

```text
--- loader_return_after expired ---
LITHIUM USB: normal timer return boundary; draining before quiesce
LITHIUM USB: timer boundary drain=1 target=325290
```

固定排空目标为 325,290，解码流结束偏移为 325,433，目标后还收到 143 字节。**已验证的是从内核横幅到正常定时返回排空结果的已输出内核控制台流**。接收器最后仍报 `[Errno 32] Pipe error`，发生在该边界和成功排空结果收到之后；不能据此判为 panic。UEFI 结束摘要属于另一个控制台，本 USB 设备未采集它，终态由用户确认。所有 daemon 的未输出日志、输出端截断及完整固件日志不在本次完整性验收范围内。

## SpringBoard 卡点

构造阶段在观察器计时 82,727ms 完成，主队列回调已安排、运行循环观察器已安装，但没有 `main_queue_enter` 或运行循环活动/提交里程碑。早期快照把 `observers=1/1` 算成主运行循环已进入，本报告已纠正这一匹配错误。

24 次主线程采样中，index 3 至 23 共 **21 次原始栈完全相同**；观察器 elapsed 从 991,152ms 至 2,587,968ms，跨度 **26 分 36.816 秒**。状态始终是固定 XNU 定义的 `TH_STATE_WAITING`（3），user_us=9,421,050、system_us=138,172,903 均不增加，cpu_permille=0，loop_mask=0、commits=0。采样使用观察器自身时间，不能当成从手机启动计时。

使用用户提供的 20F66 普通 arm64 多分片共享缓存只读核对，运行 slide 为 `0x38c74000`，分别由 `_mach_msg2_trap` 和 `___ulock_wait` 原始 syscall wrapper 的 svc 后续地址核验。重复栈对应：

**ControlCenterServices → BaseBoard `BSCollectionFilter` → CoreServices → `__ulock_wait`**。

| 未滑动地址 | 已核实模块 / 函数位置 |
| --- | --- |
| 0x1c1853a90 | libsystem_kernel / `___ulock_wait` +8 |
| 0x181c867b8 | CoreServices / 0x181c866e4 +212 |
| 0x1f000fc64 | ControlCenterServices / 0x1f000fc10 +84 |
| 0x18dd3b0f4 | BaseBoard / `_BSCollectionFilter` +216 |

私有方法名在缓存中未导出，不能猜某个具体 CoreServices 或控制中心方法作为已证实根因。早期 `os_log_create` 和 `_BKSDisplayGetMainScreenInfo` 等待后来都已推进，不是最终固定停点。SpringBoard 已连接 backboardd System shell；activity assertion 后来在 240.901 秒返回，containermanagerd PID14 后来运行，虚拟 touch service 后来有非空对象。这些记录不能代替完整容器 RPC 或物理触摸验收。

## 时间、显示与内存

首次基础图层在内核计时 1,143,907ms（约 19 分 4 秒）提交，随后保留了完整 30 分钟观察窗口。最后 BootUI 仍是 Apple Logo，没有明确屏幕所有者，不能将基础图层提交记为桌面。

三次实际软件显示提交的准备分别为 330/329/329μs，画行分别为 3,288,748 / 4,793,328 / 3,804,794μs，即约 3.29～4.79 秒。该耗时包含调度等待，不等于纯 CPU 绘制时间。物理 framebuffer 为 1080×2040 BGR24，逻辑尺寸 750×1334。末尾仍 free 1,754MB、available 2,646MB；持续相同等待栈和不变的 CPU 累计时间不能继续只归因于等待余量或 RAM 不足。

捕获流未见实际 userabort/usertrap/kernel panic 正文；软件 renderer 的 `Simulating crash!` 文案不是实际崩溃证据。当前仍是 2880MiB 内存窗口、XNU 单核和软件显示；全 6GB、SMP、A530、物理触摸、桌面与跨重启持久读回均未完成验证。

## 下一修订

1. 复用固定 HakiOS 的 `user_exception_trace.c` 有界多线程取栈，关联 SpringBoard 主线程、工作线程和服务依赖，定位谁持锁、谁未返回。上游已有 `ustacks`、`ustacks_at`、`ustacks_ms`、`ustacks_threads`、`ustacks_tname`、`ustacks_count`；先核对集成及输出开销，保留有限采样。
2. 仅用现有 20F66 镜像映射未导出帧，核对完整上游初始化/依赖后做有边界的修复；不绕过控制中心、不自造通用服务。
3. 同步检查软件画行耗时和调度，修复后完整构建、检查并冻结一个新的联合包。单纯继续加时缺乏依据；不重新测试已消费的 R6/R5，不重刷 userdata。

## 冻结证据与本次改动

准备和构建验证见 [R6 准备报告](hakios-ios16-r6-prepared.md)。完整 HakiOS 固定基线 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`，真实 D22AP loader、Mu、413 项用户空间任务和 ARMv8.0 检查沿用 R6 冻结结果。本次只分析与归档，没有修改运行代码、重新构建、查询手机、启动或刷写分区。

| 内容 | SHA256 |
| --- | --- |
| R6 临时启动镜像（16,232,448 字节） | 5faba1f7a5ea0ac0f7717b68fdffd3013bf14c4a2d757d5c1f260532a72d1118 |
| R6 内核 | 17117d4a0f9a247d617fbe20a43f3c3ea58d828dd2841f93b9f78d333caa87d6 |
| 最终原始 USB 帧 | 24a7602e7ae1653e604de4734cce0df5b84b58a00018d112091d0c8dde559b26 |
| 最终控制台 | cb4f301f361cbc45cad9c4d64ae7498c1f2ee660e94702037ec42265fe484bf8 |
| 会话冻结接收器 | d5a9aecdd3c5ac728b233628d9ae810a598a35ba05d9068d0db3ed70fde3791c |

私有快照为 `reports/local/20261117-005345-319335-ios16-r6/snapshot-final/`；原始日志、共享缓存和原始代码字节留在忽略目录，不进入 Git 或共享包。公开 [同名 JSON](hakios-ios16-device-r6.json) 保留计数、哈希、地址映射和关键行索引。R5 备份分支与冻结历史报告保持原样。

报告更新于 2026-11-17T02:40:52.465789+08:00。
