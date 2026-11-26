# R27：原始launchd标准输出交接修正包

完整HakiOS8796沿正常构建流程，内核与R24/R25字节一致。修正R26新增诊断误要求ENOTTY25的缺陷：接受目标XNU/dev/null未知终端请求返回ENODEV19的非终端语义，并保留open/dup0/dup1/close/tty及exec结果。stdin/stdout接入/dev/null，stderr保留到控制台。R25原始launchd退出原因仍待真机确认。原始launchd、dyld/cache及服务二进制不修改。复用上游bindfs选取notifyd/cfprefsd和诊断任务，复用上游ramfs与iOS14原卷元数据生成Data目录，上游rc的exec保持PID1并交给/sbin/launchd。使用上游applog/user_abort_trace/systrace；摘要保留原始错误行、abort栈和最后3条launchd调用。系统HFSX物理只读，本轮无需修改分区。

镜像SHA256：`d1f3af4e79baef473cb1308cd52e7604ca92a62231f38ea18f80b9057ee6f4fa`。190份冻结证据、独立容器解包、13项进程快照边界、8项描述符检查、4项rc/exec用例和13项结束摘要检查通过。这些检查不算真机launchd验收。

诊断必须由真实launchd启动；60秒后执行原始ps并记录PID1、notifyd、cfprefsd是否存在。BEFORE_EXEC不代表启动成功，进程存在也不代表全部服务API成立。成功要求LAUNCHRESULT PASS与正常定时返回。内核运行120秒，加载另25秒，结束摘要保留；无需守屏、秒表或中途抢拍。

系统继续从UFS/HFSX按需读取，bootstrap小盘与ramfs不包含整个系统镜像。本轮准备没有手机操作或分区写入。上游构建仅接受它明确限定的kext符号集检查缺失，完整内核与R24已运行字节一致。

[完整输入与验收边界](hakios-p2-ios14-launchd-handoff-ready.json) · [版本差异审查](hakios-ios14-version-alignment.md)
