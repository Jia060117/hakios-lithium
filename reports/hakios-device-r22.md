# HakiOS R22：旧Mach消息被guard策略拒绝

R22同次照片确认真实HFSX只读根切换rc0、原始缓存头匹配和438映射rc0。df与mount均SIGKILL，父进程wait_errno0/cleanup NONE/kill0，内核退出均namespace23/code20000003000000c8。实际HakiOS源码解码为Mach port GUARD/INVALID_OPTIONS，target为消息号200；唯一匹配非零target分支是旧mach_msg消息头的兼容策略拒绝。df23067ms、mount3388ms，均非超时；120200ms/10103次PPI27后正常返回，无panic。下一步为原始iOS14.8旧Mach消息入口增加有界兼容，不改文件系统、不取消签名校验。P1/R20存储保持通过，P2未通过；累计22次启动，归档未启动或写入。见reports/hakios-device-r22.md。

153份同次冻结证据、原图及转录已核验。原因码中的0xc8由mach_msg头的msgh_id编码，因此是消息号200；其它INVALID_OPTIONS分支的target为0。namespace23为GUARD，而非codesigning或jetsam。完整动态程序和iOS14服务仍未通过。

[同次JSON](hakios-device-r22.json) · [实际源码分析](hakios-r22-guard-analysis.json)
