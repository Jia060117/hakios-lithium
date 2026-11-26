# R8 最终日志排查

本次只分析已经结束的 R8：366347 字节、6849 帧，保存日志 SHA256 与最终采集报告一致，原有独立重解码审计通过，正常 timer 和成功 USB 排空正文均已捕获。末尾 Pipe error 不代表本轮发生 panic。此结论只覆盖已输出的 USB 流，不代表所有固件、daemon 或 UEFI 日志齐全。没有重新启动或查询手机，没有构建运行镜像、重刷 userdata 或再次关机。

当前明确的桌面阻挡点是 SpringBoard 的应用记录初始化等待 LaunchServices 数据库就绪。SpringBoard 主队列回调和主循环标记均未出现；System shell 连接、基础图层和用户确认的苹果标志均不能替代桌面通过。最终物理画面没有目视确认。

|采样轮次|服务端与客户端观察|
|---|---|
|1|目标进程尚未入样|
|2–3|其他启动依赖等待；lsd 开始初始化|
|4|lsd 在本地化信息和字典遍历；SpringBoard owner 等待 NonBlocking 请求|
|5|lsd 在注册 bundle、处理数据库内容；客户端仍在 NonBlocking 请求|
|6|lsd 在数据库回收/分配；客户端仍在 NonBlocking 请求|
|7–8|SpringBoard、backboardd、runningboardd 的采样客户端转入 Blocking 请求；lsd 安装恢复与数据库请求等待同一 dispatch group|

同一 SpringBoard 锁与采样 owner 的关联有 5 次，跨度 1441.096197 秒，但这不是同一笔 RPC 一直不返回：第 4–6 轮的返回地址对应 `getServerStoreNonBlockingWithCompletionHandler:`，第 7–8 轮对应包装函数调用 `getServerStoreBlockingWithCompletionHandler:`。两条调用的 selector、原始 arm64 指令和返回地址均已按用户提供的 20F66 缓存核对。早期 NonBlocking 判断适用于早期样本；不能把它扩展成最终停点，亦不能把后期 Blocking 反写成整个 24 分钟的状态。

最后两轮 lsd 主线程在 `_LSInstallProgressService performJournalRecovery` 的 `dispatch_sync` 等待。工作线程分别从 `LSDatabaseBlockingFetchClient getServerStoreBlockingWithCompletionHandler:` 和 `restoreInactiveInstalls` 等待同一个 dispatch group，两个样本相距 360.293492 秒。原始代码为 `dispatch_group_wait(..., DISPATCH_TIME_FOREVER)`；运行时底层是 ulock_op=1/word=0，不能按 unfair-lock 解析出所谓持锁 owner。这里的安装恢复也不同于启动时 HFS 文件系统 journal replay，后者已打印完成。

lsd 的另一个工作线程第七轮等待容器代码签名缓存失效回复，第八轮已换到 CFPreferences 同步 XPC。另有 ManagedConfiguration 的系统组容器查询路径进入错误日志。说明依赖还在变化，现有离散样本没有证实循环死锁，也没测到这些 receive-only 同步等待原请求的实际服务端 PID。`receiver_pid=-1` 不能解释成服务不存在。下一步应确定数据库就绪 group 的生产者、完成事件和它等待的容器/偏好设置请求，而不是直接跳过恢复或伪造成功回复。

日志中有数据库 csstore 与偏好 plist 的 ENOENT、139 条扩展 bundle unit 查询失败、73 条 persona type6 查找失败。缺少初始缓存可能触发重建，不能仅凭 ENOENT 认定目录坏了或文件系统格式不兼容。containermanagerd 已实际执行上游 no-personas 修正，并进入运行循环/请求处理；system 变体的配置存在，未入样不能直接记成失败。数据库何时保存成功、这些路径是否跨重启持久存在，以及 system 变体是否处理过请求，仍需有针对性的证据。

存储另有一条具体缺陷候选：`hfs_clonefile: cluster_write failed - 34`。核对实际源码，这是 HFS 的 `hfs_relocate` 内部搬移路径，不是应用的 clonefile 系统调用，更不能据名称推断必须改 APFS。实际 UFS Data ioctl 未处理 `DKIOCISSOLIDSTATE`，默认 ENOTTY；HFS 通过此属性设置 HFS_SSD，已设置时会拒绝该搬移路径。因此应补齐真实固态设备属性，并为剩余错误记录文件、偏移和调用上下文。当前日志没有这些上下文，尚不能证明这一个错误导致 LaunchServices 未就绪。保持现有 UFS/System/Data 布局，无需重刷 userdata。

性能方面，仍是 2880MiB/单核。排除首轮绝对计数器原点影响后，7 个约 6 分钟区间的 top 忙碌率为 99、98、99、99、99、99、99%；末段 kernel_task 34.5%、mediaserverd 24.6%。原始上游统计没有额外的主机时钟/PMU校准，不能用这些数值推导频率或多核提速倍数。末尾 free 1745MB、available 2642MB，没有内存耗尽证据。三次完整画行是 0.733–0.740 秒，明显小于整段启动等待，不能把几十分钟全部归为屏幕拷贝。跨修订比较不是受控 A/B。

R8 同时开启 `drain_uart_sync=1` 和多项服务 OSLOG。HakiOS 自身说明大量控制台日志会拖慢系统，实际晚期可见日志线程在 write/os_log 路径；同步输出的开销需要降低，但其占比尚未测量。launchd 早期时间戳的消息很晚才输出，不能用落盘行的先后次序推导服务重启，也不能从它们推断时钟错误。

HID 另有 1 次事件服务重连、89 条无效属性请求和 15 条虚拟服务不存在。backboardd PID 在采样中保持不变，未据此证明进程重启。虚拟 HID 生命周期应随下一联合包修正/核验；物理触摸和 A530 仍未通过，不能把 HID 错误直接认成这条 LaunchServices 等待链的根因。

下一修订的顺序：先通过上游既有诊断追数据库 ready group 的完成条件及相关 IPC；核对容器动态路径、偏好设置回复与数据库保存结果；补齐 UFS Data 的固态属性；减少广泛同步刷屏并保留 USB 尾部和关键事件；同一联合包观察 SpringBoard 主队列、屏幕所有权与 HID。只加运行时间不能检验或修正这些问题，现有证据也不足以承诺再等一会儿就会进入桌面。

本次新增诊断报告和私有分析数据，运行代码与冻结 R8 镜像未改。全部原始日志、反汇编和完整符号保留忽略目录；同名 JSON 给出索引和核验范围。R8 备份分支指向 `1abbf8c99fc32c56cf135311adc8a6091d1fadcb`，保留不动。不重试 R8/R7/R6/R5，不执行 QEMU，不重刷 userdata。
