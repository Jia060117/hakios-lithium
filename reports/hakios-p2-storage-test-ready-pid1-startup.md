# P2诊断入口修正：完整上游libmini启动已接入

针对[第15轮](hakios-device-r15.md)的诊断PID1栈对齐异常，已将诊断C入口改为main，并按固定HakiOS的userland/build.sh编译、链接完整libmini的mini.c和start.S。没有修改通用内核或文件系统来绕过异常。

新Mach-O的实际LC_UNIXTHREAD入口为 `0x100003c0c`，精确指向上游汇编_start；反汇编确认先保留原argc指针并将SP按16字节对齐，再调用___mini_start和main。上游三个libmini源文件按固定提交逐字节核对，新APFS诊断根盘通过apfsck和完整逐文件读回；启动契约验证针对实际链接二进制。

新镜像SHA256为 `89b3dd7e62d9c1f1e06ac8ce899543122f6a7308bb9c9ae0d28f2567145a86c4`，15,728,640字节，冻结路径为 `artifacts/hakios/p2-storage-ready-pid1-startup/lithium-hakios-test.img`。116项冻结证据、ARMv8检查和Android/gzip/FAT/XNUP独立解包见 [JSON](hakios-p2-storage-test-ready-pid1-startup.json)。内核字节与R15一致，SHA256为 `3556a5e8543aae8ab8cfabc41fda08fabc457305f8d99e1c8a852d289ad0dd78`；诊断程序及APFS根盘有改变。R15冻结包和原图保留。

测试仍为真实只读UFS/HFS路径：读取userdata卷头、只读挂载HFSX、核对18H17的SystemVersion.plist、dyld和launchd文件、卸载并正常定时返回。约120秒测试，加25秒加载停留，结束摘要保留，用户方便时提供一次即可；P1不再重复。

已修正R15所证实的入口集成缺口，修正后的PID1和存储路径仍需真机验证。没有执行iOS14 launchd，不能据构建或文件核对宣称iOS14兼容、SpringBoard、GPU或触摸通过。本次准备未临时启动或刷写分区，累计HakiOS启动15次；没有旧7195或QEMU执行。
