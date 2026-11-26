# R26：原始launchd标准输出交接修正包

完整HakiOS8796沿正常构建流程，内核与R24/R25字节一致。原始18H17 launchd检查isatty(1)时可能拒绝控制台并退出78；本包将stdin/stdout接入/dev/null，交接前实查ENOTTY，stderr保留到控制台。该原因尚未真机确认。原始launchd、dyld/cache及服务二进制不修改。复用上游bindfs选取notifyd/cfprefsd和诊断任务，复用上游ramfs与iOS14原卷元数据生成Data目录，上游rc的exec保持PID1并交给/sbin/launchd。使用上游applog/user_abort_trace/systrace；摘要保留原始错误行、abort栈和最后3条launchd调用。系统HFSX物理只读，本轮无需修改分区。

镜像SHA256：`c652c9fd059d6808e23b03283ce02ebef80531367de9cb158637f3e17d36b543`。172份冻结证据、独立容器解包、13项进程快照边界、3项真实描述符检查和12项结束摘要检查通过。这些检查不算真机launchd验收。

诊断必须由真实launchd启动；60秒后执行原始ps并记录PID1、notifyd、cfprefsd是否存在。BEFORE_EXEC不代表启动成功，进程存在也不代表全部服务API成立。成功要求LAUNCHRESULT PASS与正常定时返回。内核运行120秒，加载另25秒，结束摘要保留；无需守屏、秒表或中途抢拍。

[完整输入与验收边界](hakios-p2-ios14-launchd-stdio-ready.json)
