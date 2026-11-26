# HakiOS MIX 第15轮：诊断入口栈对齐异常已定位

2026-11-15 20:05:58北京时间临时启动，镜像SHA256为 `4b2d13d2f2455dd91ea4d2a4a7444a980e388d15b301cad9ce5e4b279cc48c9f`。原图哈希、冻结输入、转录与故障反汇编见 [JSON](hakios-device-r15.json)。

照片确认UEFI UFS stage=6、只读userdata HFSX卷头检查及HakiOS内核执行；硬件计时10580ms、734次PPI27中断后，诊断PID1发生SIGBUS，panic摘要保留。异常为EXC_BAD_ACCESS / EXC_ARM_SP_ALIGN（code 0x103），ESR=0x9a000000（EC 0x26），SP=0x16fdffe88，FAR=0x16fdffe48。

PC=0x100003508精确对应冻结诊断程序C入口 `__start` 的第一条指令 `stp x24, x23, [sp, #-0x40]!`。SP除以16余8，压栈有效地址等于SP-0x40，与FAR一致；尚未执行第一个open syscall。没有START/HEADER/MOUNT/RESULT行，HFS挂载及18H17文件核对未通过。这是诊断启动集成缺口；本轮证据不支持将故障归因于UFS/HFS、APFS分页或iOS14 launchd。

固定上游 `userland/libmini/start.S` 在调用C代码前保留原参数栈指针并对齐SP到16字节，`mini.c` 提供正常main启动。后续将诊断程序改为main并按上游userland/build.sh的libmini编译与链接契约接入；内核、loader和只读存储链路保留。修复后的运行结果仍须真机验证。

累计HakiOS临时启动15次，本次仅整理原图和只读反汇编，没有新启动或分区写入。P1保持通过，P2及iOS14兼容未通过；未运行旧7195或QEMU。
