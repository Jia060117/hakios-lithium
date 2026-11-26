# iOS16 R10 最终诊断

本轮已正常到时返回 UEFI，用户原图确认 `TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`。启动一次，未重试、重刷 userdata 或修改运行代码；桌面未通过。

三核确实启动，另一个物理核驻留 UEFI 服务 UFS。首次基础图层在内核相对计时 66.602 秒出现；软件全行绘制两个严格有效样本为 0.674914、0.671892 秒，一次少变化画行为 0.015359 秒。5 个时序不一致片段已排除。这些绘制不是 SpringBoard 桌面，不能解释约半小时的初始化等待，也不能将提速全部归因 SMP。

SpringBoard 在 constructor_ready 后的 60 个样本（35.063 秒至 1805.256 秒，跨度 29 分 30.193 秒）主线程栈完全相同，CPU 累计保持 user951411/system11213923 微秒，main_queue、runloop 和该观察器的 commits 均为0。五轮多线程样本均在同一 unfair-lock 上关联到采样 owner 0x18b，跨度 1440.016030 秒；owner 栈指向 `_LSDReadService` 的同步 `getServerStoreBlockingWithCompletionHandler:`。这证明采样停点一致，不证明整段期间为同一个未返回 RPC。

lsd 的 BlockingFetch 与安装恢复工作线程五次等待同一个数据库 ready dispatch group（0x1005f2690），底层 ulock_op=1，不能当 unfair-lock 找持锁者。负责初始化的另一线程0x134仍变化：

|采样|数据库生产者 lsd|容器服务样本|
|---|---|---|
|1|扫描内置系统应用、解析 bundle/目录/realPath|主线程在运行循环，工作线程采样空闲|
|2|查询/创建数据容器，处理 built-in parallel placeholder|MCMCommandSetDataProtection 写元数据|
|3|获取 bundle 的 group container，填充注册记录|本轮容器工作线程和两个进程记录源端缺失|
|4|替换并使容器生效，扫描 executable bundle|MCMCommandReplaceContainer 写元数据|
|5|再次请求应用数据容器|MCMCommandSetDataProtection 写元数据|

第3–5轮最近发送尝试记录有 lsd/0x134 → containermanagerd PID13，仍是有界哈希槽中的 attempt，不能等同送达、当前未完成请求或完整历史。容器运行线程的已保存内核入口为 close，原始缓存方法包含 MCMFileManager writeData、NSDictionary MCM_writeToURL、MCMMetadata writeMetadataToDisk；运行状态时的寄存器不是实时 PC。`fixAndRetryIfPermissionsError...` 是被调用的包装方法，单凭出现在栈上不能宣称已经发生权限错误或无限重试。现有记录未包含具体应用/容器身份、目标路径、返回 errno 与完成数，所以仍不能分辨“大量不同应用的慢处理”与“同一对象反复处理”。

LS 观察器11/11生效，256事件预算耗尽：125 enter、123 leave、2 wait及6个阶段事件。末尾停在 enter，后续 leave/ready 被预算省略；不能将差额2或缺失 ready 当真实未完成计数。独立线程栈仍证明采样时数据库调用在等待。

后续四个360秒区间 top 中 lsd 为49.8%、62.7%、58.0%、68.5%，containermanagerd 为48.7%、38.2%、47.9%、35.1%；汇总144%、113%、119%、111%。按上游算法每个CPU为100%，不是三核满载，也不是负载超过100%的错误。首个top有计时原点偏移，排除。最后 free919MB、available2461MB，无压缩；现有证据不支持单纯归因内存不足或三核全部算力耗尽。

`denied attempt to escape CPU monitor (missing required entitlement)` 在保存流中出现524次。核对实际XNU task_policy.c：调用者请求放宽限额但缺少授权时，内核将其钳制在全局上限、启用监视器并打印该消息；它不同于 Mach 服务查询被拒绝。另有 lsd thread308 CPU用量越界告警，说明它在消耗CPU；是否影响此处吞吐、授权在移植中如何处理需要单独查证。不能仅凭告警把它认作卡点或直接关闭全局监视。

USB最终664633字节/2753帧，原始帧独立重解码与正文逐字节一致，缺口/重复/残帧/设备lost均0。正常 timer、返回边界和成功drain均已采集；随后Pipe error与已退出XNU、照片UEFI终态一致。照片RAW BYTES795828属于固件侧计数，不能拿它减USB长度来计算丢包。

但日志并非无损：`writer_contention_dropped_bytes=10343` 是源端写入缓冲区前丢弃；第3轮只解析出4/6进程、容器1/4线程。另有LS事件预算与未开启的daemon/固件日志范围限制。完整USB传输只证明已进入USB流的内容，不能证明所有生成文本已捕获。

下一修订保留完整HakiOS、UFS、2880MiB及三核，不重刷userdata、不重试原R10。先按不同bundle/容器身份记录数据库生产者完成数与重复数；记录容器查询、替换、数据保护和元数据写入的具体路径、返回值及耗时，核对close/fsync与权限。修正日志保留：高频group事件独立预算，关键完成事件保底，有界保存各核诊断且不阻塞关中断上下文。CPU monitor授权及限额只做证据驱动的上游兼容核对；下一联合包继续核验SpringBoard主队列、运行循环和屏幕所有权。单纯加时、伪造数据库ready或容器成功都不能检验这一链路。

来源：同次私有console.frames/console.log、5轮ustacks与IPC、用户原图；用户提供20F66缓存的导出/函数边界及Objective-C元数据逐帧匹配。私有证据、原图与反汇编留在reports/local，不包含于Git。机器报告见同名JSON。

中段启动日志、前两轮完整取栈、实际 boot-args 及三个指定地址的进一步解析见 [R10 日志证据补充](hakios-ios16-r10-log-evidence-addendum.md)。
