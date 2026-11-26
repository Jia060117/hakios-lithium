# HakiOS R23：mount成功，df触及父进程时限

R23同次照片确认完整HakiOS执行、HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。MACH14 platform2/sdkE0800/id200/legacy1确认旧Mach入口兼容分支生效；原始mount在5878ms正常exit0、signal0、timeout0。df在30148ms触及30秒测试时限，父进程TIME_LIMIT/kill1/rc0，内核namespace2/code9与该SIGKILL一致；导致df尚未完成的具体等待或首次加载成本仍未确定。120208ms/10968次PPI27后正常定时返回，无panic。原图、转录和163份同次冻结证据已核验。P1/R20存储保持通过，P2整体未通过；累计23次启动，归档无设备操作或分区写入。见reports/hakios-device-r23.md。

原始程序及内核签名策略保持不变。照片仅保留摘要，未取得df当时的原始系统调用日志；不能据此宣布df死锁、UFS故障或完整iOS14兼容成立。下一步保留同一完整HakiOS兼容内核，核对并调整有界动态测试预算、保留上游df系统调用日志，再决定是否需要进一步服务适配。

[同次JSON](hakios-device-r23.json)
