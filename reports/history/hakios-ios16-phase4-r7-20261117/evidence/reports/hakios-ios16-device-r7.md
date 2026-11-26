# iOS16 R7 真机联合诊断结果

R7 最终观测（2026-11-17）：内核正常定时结束与成功USB排空正文已捕获；用户确认UEFI结束摘要，桌面未通过。最终252881字节/6699帧，原始帧重解码逐字节一致，缺口/重复/残帧/设备lost均0。多线程8轮；SpringBoard同锁/同owner关联5次，跨度1440.424秒，owner等待_LSDReadService数据库同步回复。第七轮lsd曾等待容器代码签名缓存失效同步回复；实际IPC对端与循环依赖根因未定。画行3次，墙钟[3497790, 5438626]微秒、线程CPU[2713141, 3469201]微秒。top参数截断、长printf尾部在USB前丢失、容器管理器名称漏采已确认，因此未宣称所有日志完整。保持UFS、2880MiB/单核；不重试R7/R6/R5，不重刷userdata。全6GB、SMP、A530、物理触摸及桌面未验证。见reports/hakios-ios16-device-r7.md/json。

冻结构建提交`5b70bd5`，仅临时启动一次；后续电脑端解析未改运行包、未重启或刷写手机。USB在正常timer与drain正文后断开，Pipe error本身不代表panic。准备记录见[R7准备报告](hakios-ios16-r7-prepared.md)。

用户终态照片确认UEFI摘要：`RETURN TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`、`TIMER BOUNDARY DRAIN 1 TARGET 252831`，目标值与同次USB日志一致。照片RAW BYTES为362773，USB控制台为252881字节，二者记录范围不同；差值不能直接当作USB丢失量，也不能据照片断言所有daemon日志完整。照片原图及哈希留在私有证据目录。

SpringBoard PID8主线程0xbd等待CoreServices锁`0x206d6a4b8`。重复采样读到锁字`0x2a02`，与等待值一致；只读正常IPC线程转换指向同任务owner线程0xeb。owner通过NSXPC等待同步回复，用户原20F66缓存精确调用点/selector/类元数据对应`_LSDReadService`、`getServerStoreNonBlockingWithCompletionHandler:`。

同锁/owner关联5次，观测跨度1440.424秒；不能由离散采样证明期间持续持锁、实际IPC对端PID或循环死锁。主线程采样仍未见主队列或运行循环进入，CPU累计从第6个主线程样本后不变。

lsd保存的上下文先后涉及bundle信息/本地化读取、注册、数据库回收以及NSXPCListener启动；第七轮工作线程0x11a通过容器代码签名缓存失效接口等待同步回复。backboardd线程0x13b和runningboardd线程0xf0也有相同CoreServices客户端调用点。它们是依赖线索，实际服务端和因果关系仍需核对。TH_RUN显示最后保存的入核上下文，不能把它当即时PC或CPU利用率。

第八轮lsd主线程在`_LSInstallProgressService performJournalRecovery`内等待；工作线程0x24e经`restoreInactiveInstalls`、0x252经`LSDatabaseBlockingFetchClient getServerStoreBlockingWithCompletionHandler:`在同一地址`0x131d6bf34`上等待，ulock_op=1、word=0；这不是SpringBoard的unfair-lock owner证据。0x11a已换到CoreFoundation同步XPC栈，不能认定先前容器失效调用仍持续阻塞。下一步重点是数据库就绪、安装恢复队列和相关同步服务依赖，尚未证明循环死锁。

软件显示的有效整幅画行样本：

|提交|墙钟秒|线程CPU秒|非CPU秒|
|---|---:|---:|---:|
|1|3.497790|2.713141|0.784649|
|2|5.438626|3.469201|1.969425|
|3|4.181447|2.928223|1.253223|

线程CPU占墙钟约64%–78%，准备区间仅约0.4ms，画行存在实际CPU成本。非CPU包含调度、阻塞、中断及测量开销，不能全部归因于调度。三处微小准备区间的1微秒CPU/墙钟倒挂样本已排除。绘制算法与像素未改，没有宣称已优化或实现GPU加速。

首次基础图层在内核计时1129788ms；末尾内存{'free_mb': 1734, 'files_mb': 904, 'programs_mb': 65, 'wired_mb': 174}。mach_absolute_us采用绝对计数器原点，仅比较差值，不能直接当本次内核运行时长。

已确认三项采集限制：loader参数尾部截断导致top未开；256字节printf缓冲使长取栈尾部在USB封包前丢失；containermanagerd的17字符长名与短名精确匹配失败，漏采其线程。USB传输核验通过不代表原始printf、全部daemon或UEFI摘要完整。详见[采集审计](hakios-ios16-r7-diagnostic-audit.md)。

电脑端解析器与计时解释通过14项检查，新增独立USB原始帧/冻结接收器双重审计通过13项检查。私有原始帧、栈、映射和哈希保留在忽略目录，同名JSON给出索引。

下一修订先拒绝完整loader参数截断、修短记录和长进程名筛选；在同一个联合包中追踪LaunchServices监听/数据库请求与容器管理同步回复的实际对端和进度，同时评估保持像素一致的BGR24打包写入。核验上游既有实现和数据库状态后再选功能修复，不伪造成功回复，不再单纯延长时间。阶段四仍未完成。
