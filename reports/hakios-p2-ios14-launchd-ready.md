# 原始iOS14 launchd的PID1测试包

完整HakiOS8796沿正常构建流程，内核与R24字节一致；原始18H17 launchd、dyld/cache与原始服务二进制不修改。复用上游bindfs选取notifyd/cfprefsd和诊断任务，复用上游ramfs与iOS14原卷元数据生成Data目录，上游rc的exec保持PID1并交给/sbin/launchd。系统HFSX物理只读，本轮无需修改分区。

镜像SHA256：`3e58cf6cef7c28ea952d273a12bc3e961a6600a06d78c5535dcf1cd8737c6c72`。165份冻结证据、独立容器解包、13项进程快照边界和9项结束摘要检查通过。这些检查不算真机launchd验收。

诊断必须由真实launchd启动；60秒后执行原始ps并记录PID1、notifyd、cfprefsd是否存在。BEFORE_EXEC不代表启动成功，进程存在也不代表全部服务API成立。成功要求LAUNCHRESULT PASS与正常定时返回。内核运行120秒，加载另25秒，结束摘要保留；无需守屏、秒表或中途抢拍。

[完整输入与验收边界](hakios-p2-ios14-launchd-ready.json)
