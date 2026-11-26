# HakiOS MIX 第14轮：UFS卷头已读通，诊断PID1遇到SIGBUS

2026-11-15 17:50:02北京时间临时启动，Fastboot接受。镜像SHA256为 `4c954331d209d50c1b51d595fa8e8e69a54e9db24886c1584ff0cac6b9a0db36`。同次输入与原图哈希见 [JSON](hakios-device-r14.json)。

照片确认已越过R13的BDS卷查找停点：UEFI UFS stage=6，loader验证了只读userdata几何与HFSX卷头，HakiOS内核运行并收到742次PPI27中断。硬件计时10608ms后发生panic，返回loader并保留22830字节日志的文字摘要。

停点为 `initproc failed to start / EXIT REASON namespace 2 subcode 0xa`。完整HakiOS的 `bsd/sys/reason.h` 定义namespace 2为OS_REASON_SIGNAL，Darwin signal 10为SIGBUS；不是namespace 9的exec错误。这次运行的是自编译存储诊断PID1，并未执行iOS14 launchd。照片未显示其START/HEADER/MOUNT/RESULT行，故诊断程序执行、内核HFS挂载和18H17文件核对均未通过。

当前摘要没有故障PC、FAR、ESR或VM返回码，不能仅凭SIGBUS确定APFS分页、栈、数据映射或硬件故障。先核对上游装载流程和现有user_exception_trace集成；必要时把已有异常信息接入保留摘要，再进行一次有明确判据的测试。

原图已按字节保存并校验SHA256。累计HakiOS临时启动仍为14次，本次证据整理未发出设备命令。最后物理状态证据为panic结束摘要，尚未确认再次回Fastboot。P1保持通过；本轮P2未通过。无分区写入，未运行旧7195或QEMU。
