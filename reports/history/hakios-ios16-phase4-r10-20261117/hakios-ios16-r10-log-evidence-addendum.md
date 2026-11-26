# R10 中段日志与指定地址核对

2026-11-17。仅从已经保存的同次 R10 日志和用户提供的 20F66 arm64 缓存整理证据。未进行新构建、真机启动或 userdata 写入；本补充不改变最终诊断中的未解决状态。

来源：`reports/local/20261117-182055-575912-ios16-r10/console.log`，5956 行、664633 字节，SHA256 `13bc15ea425bdd7309c5751b08035814b47b52a5ee98c264939931148bd10c9e`。原始片段、带行号筛选、符号结果及校验清单保存于同目录 `requested-evidence/`，私有反汇编保存于 `requested-address-code/`；均不进入 Git。

## 可直接读取的原始证据

|文件|原始 console.log 范围或内容|
|---|---|
|`requested-evidence.txt`|启动参数、服务启动、完整前两轮取栈和三个地址解析的合并文本|
|`launchd-through-service-launches.log`|878–1433：原始 launchd hello 至 SpringBoard、容器、backboardd、runningboardd 启动记录|
|`launchd-through-first-frame.log`|878–2151：继续保留至首次基础图层提交|
|`service-errors-filter-numbered.log`|全日志按用户给定正则筛选，537 条，保留原始行号；包含目录清单匹配，不能把每一条都当运行事件|
|`ustacks-sequence-1.log`|4332–4457：完整第一轮，126 行，含 SpringBoard 与 backboardd|
|`ustacks-sequence-2.log`|4905–5019：完整第二轮，115 行，含 SpringBoard 与 backboardd|
|`boot-cpu-and-first-frame-numbered.log`|三核启动、online、完整 boot-args 和首个基础图层记录|
|`requested-addresses.json`|三个地址的运行地址、去 slide 地址、函数边界、方法元数据及回调调用点|
|`manifest.json`|源日志与上述提取文件的长度、哈希及原始范围|

提取范围已逐行与原文比较，前两轮均包含六个进程的采样段。源日志 SHA256 与已归档值一致。

## 对外部分析的核对

- 第 880 行有完整 boot-args：`bootcpu=2 cpus=3`。第 838 行为 `3 processors running`，875–877 行分别是 `phys=0x100/0x101/0x1` 的 online 记录。这些记录来自实际每核初始化路径，支持本轮三核已启动，超出了仅登记拓扑的证据；仍不等于长期 SMP 正确性已验证。`cntfrq=19200000` 是定时器频率，不是 CPU 主频。
- 第 2151 行记录 `first base-layer frame at 66602 ms`，用户也确认苹果标志。因此应称为“SpringBoard 自身尚未进入主循环/提交桌面”，不能称为“还没有绘制任何首帧”。
- 第 1246、1257、1270、1433 行分别记录 SpringBoard PID20、containermanagerd PID13、backboardd PID12、runningboardd PID7 成功启动。多线程样本还能看到这些进程的具体执行路径，不能预设它们缺失或死亡；进程存在也不能替代服务功能验证。
- SpringBoard 主线程不消耗新增 CPU 的判断与完整日志一致。但 lsd 处在其已观测的等待依赖链上，不只是另一个独立症状。五轮 lsd 数据库获取及安装恢复线程等待同一 ready dispatch group，而生产者线程仍在切换应用扫描、容器查询、替换及元数据写入路径。
- `receiver_pid=-1 lookup=15` 不能单独说明服务不存在：保存的接收阶段上下文不一定带有原始发送对端。AXSpringBoardServer 查询失败仅证明该端点当时未查到。
- R6–R8 的单核证据已出现 SpringBoard 初始化和 LaunchServices 等待，R8 后两轮已有 Blocking/ready-group 等待。因此症状早于本轮 SMP；它不是同一二进制的单核/多核受控 A/B，不能据此彻底排除丢唤醒或 IPI 问题。
- CPU monitor 告警应按实际 XNU 授权与限额路径解释，不能直接归结为服务权限拒绝或其重试死循环。容器方法名中的 `fixAndRetryIfPermissionsError` 也不是已发生权限错误的证据。

## 三个地址

使用用户提供的 20F66 arm64 多子缓存，UUID `e5420db330d033aa8b7e05b5038bb46d`，slide `0x12aa4000`。方法名通过 Objective-C IMP 与函数起点精确匹配；第三个回调通过原始 block 指针和 arm64 调用点另行核验。

|运行地址|去 slide 地址|解析|
|---|---|---|
|`0x194680bb8`|`0x181bdcbb8`|CoreServices，`-[LSApplicationRecord _initWithNode:bundleIdentifier:placeholderBehavior:systemPlaceholder:itemID:forceInBundleContainer:context:error:] + 0x8c`|
|`0x194680b20`|`0x181bdcb20`|CoreServices，`-[LSApplicationRecord initWithBundleIdentifier:allowPlaceholder:error:] + 0x34`|
|`0x202ab3c64`|`0x1f000fc64`|ControlCenterServices，函数 `0x1f000fc10 + 0x54`：`-[CCSModuleRepository _queue_filterModuleMetadataByAssociatedBundleAvailability:]` 使用的筛选 block 回调|

第三处没有恢复出原始被裁剪的 C/block 符号名，不伪造 `block_invoke` 的完整编译器符号。其所属方法地址为 `0x1f000fbc4`；该方法把 block `0x20cde8580` 交给 `bs_filter:`，block 的 invoke 指针精确等于 `0x1f000fc10`。回调先获取 `associatedBundleIdentifier`，随后以 `LSApplicationRecord` 为接收类、`allowPlaceholder=1` 调用 `initWithBundleIdentifier:allowPlaceholder:error:`；调用指令为 `0x1f000fc60`，返回地址正是样本中的 `0x1f000fc64`。

这定位到控制中心模块筛选期间的关联应用记录查询。样本未给出该次查询的具体 bundle identifier，不能点名某个模块或应用。

## 当前结论与下一步

采样关联链为：SpringBoard 控制中心模块筛选 → LSApplicationRecord 初始化 → CoreServices 锁的采样 owner → `_LSDReadService` 同步 Blocking 请求 → lsd 数据库 ready group；数据库生产者正在应用及容器处理。相同离散样本不证明整段期间是同一笔 RPC，也没有证明循环死锁。

下一修订仍应追实际 bundle/container 身份、已完成数量、同对象重复次数，以及查询、替换、数据保护和元数据写入的路径/返回值/耗时；关键 ready/完成事件需要独立保留。现有 LS 256 事件预算已耗尽，不能用缺失的 ready 或 enter/leave 差额推断最终计数。保留 UFS、2880MiB 和当前三核，不以单纯加时、伪造 ready 或无依据取消监控代替修复。

日志限制仍适用：已保存 USB 帧可重解码一致，但源端争用丢弃 10343 字节，第 3 轮部分取栈缺失，不能声称全部生成日志无损。正常 timer 返回和成功排空已捕获；尚未进入桌面，最终根因未定。

既有详细证据见 [R10 最终诊断](hakios-ios16-r10-final-diagnosis.md)、[R8 单核最终诊断](hakios-ios16-r8-final-diagnosis.md) 和 [R6 单核观测](hakios-ios16-device-r6.md)。
