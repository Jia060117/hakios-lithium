# HakiOS MIX 第18轮：HFS挂载通过，UFS读取拷贝异常

同次照片确认 HEADER read8192/match1、MOUNT rc0/readonly1，日志初始化修复已越过R17的EINVAL。镜像和完整内核与R17逐字节一致，真实卷此前只修复两个日志元数据块。

约13247ms、972次PPI27中断后发生kernel data abort，返回摘要保留。PC=0xfffffff007a4dd58为冻结内核memmove+0x48的`stp x12,x13,[x0]`；LR=0xfffffff00748b1b8精确接在ufs_request读取路径调用memmove之后，指向从loader共享缓冲区写入目标缓冲区时异常。故障目标地址、ESR及FAR未从本照片捕获，不能把saved state指针当作故障地址。

实际编译源普通ufs_strategy将buf_dataptr直接交给ufs_read；同一个完整上游驱动的数据卷路径已用buf_map/buf_unmap处理没有内核地址的cluster页面。下一步沿用该上游接口补齐普通读取的映射生命周期，不改HFS检查或另造文件系统服务。

HFS真实只读挂载已验证；三个18H17文件、卸载、RESULT PASS和iOS进程尚未通过。P2仍未通过，P1保持3/3。原图、转录、冻结反汇编及完整源证据见[JSON](hakios-device-r18.json)。本次归档没有新boot/分区写入；前置维护8192字节写入已独立记录。累计HakiOS临时启动18次，未运行旧7195或QEMU。
