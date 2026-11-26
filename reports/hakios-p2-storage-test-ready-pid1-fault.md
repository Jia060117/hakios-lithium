# P2 诊断进程故障记录包：已具备真机测试输入

2026-11-15：针对[第14轮](hakios-device-r14.md)的诊断PID1 SIGBUS，完整HakiOS内核、Mu固件、loader及只读存储实验包已构建并冻结。镜像15,724,544字节，SHA256为 `4b2d13d2f2455dd91ea4d2a4a7444a980e388d15b301cad9ce5e4b279cc48c9f`；路径为 `artifacts/hakios/p2-storage-ready-pid1-fault/lithium-hakios-test.img`。机器记录和110项冻结证据见 [JSON](hakios-p2-storage-test-ready-pid1-fault.json)。

活动内核仍是完整HakiOS固定提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。复用上游已有 `user_exception_trace`，通过 `systrace=1` 启用；仅为MIX诊断PID1将异常类型、VM返回码、故障地址和PC/LR/SP/ESR/FAR写入上游返回日志，并将APFS pagein错误及上游exec/系统调用错误显示在保留摘要里。文件系统读写、异常处理结果及诊断程序均未改动。本包用于取得缺失的故障证据，SIGBUS根因尚未确定，也未宣称已修复。

新内核SHA256为 `3556a5e8543aae8ab8cfabc41fda08fabc457305f8d99e1c8a852d289ad0dd78`，日志确认两处修改的内核文件实际编译和链接。6个二进制通过ARMv8指令检查；Android/gzip/FAT/XNUP独立解包核对通过，原始Image为22,224,496字节，与早期保留区相隔315,792字节。APFS双读取器校验通过，诊断根盘及PID1字节与R14相同。5项ASAN/UBSAN检查直接执行实际摘要函数，确认异常记录不会被后续栈/崩溃日志替代，截断或无NUL日志不越界，原有成功和失败摘要仍显示；既有7项UFS传输和10项路线隔离检查通过。

准备期间发现中断后存储构建JSON全零、APFS电脑端读取工具为空；已保留损坏记录、从实际完整HakiOS源码重建读取工具，并重新生成及验证根盘。JSON记录在替换前执行flush/fsync。R14冻结包、原图及P1冻结证据重新核验一致。

下一轮命令为 `python scripts/boot-hakios-lithium.py --boot`，须重新核验本机Fastboot身份、解锁、userdata容量和电池启动条件。命令行：`bootcpu=1 cpus=1 serial=3 -v ufs=2 lithium_storage_test=1 systrace=1 loader_return_after=120`。约120秒测试，加25秒加载停留；结束摘要保留到实体按键重启，用户方便时查看一次即可。

若失败，读取 `LITHIUM P2 FAULT/STATE/ABORT/PAGEIN` 并将PC对应到本包冻结反汇编后修正具体条件。若通过，仍须同次确认HEADER、VERSION、DYLD和LAUNCHD的match=1、只读HFS挂载及卸载成功、RESULT PASS和正常返回。R14仅证明UEFI读卷头与新基线内核执行；HFS及iOS14兼容未通过。本包尚未实机启动，累计临时启动仍为14次，P1无需重复测试。
