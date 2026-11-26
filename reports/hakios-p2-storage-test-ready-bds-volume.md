# P2 加载器卷枚举修正包

2026-11-15：针对[第13轮照片](hakios-device-r13.md)中的 `FIND VOLUME / EFI_UNSUPPORTED`，已完成BDS修正、完整Mu构建、HakiOS构建核对、静态检查及冻结包独立解包。[机器记录](hakios-p2-storage-test-ready-bds-volume.json)包含104项冻结证据；新包尚未实机启动。

冻结镜像：`artifacts/hakios/p2-storage-ready-bds-volume/lithium-hakios-test.img`，15,724,544字节。SHA256：`4c954331d209d50c1b51d595fa8e8e69a54e9db24886c1584ff0cac6b9a0db36`。

原因是FAT镜像的MBR含覆盖整盘的分区记录。启用完整Mu PartitionDxe后，文件系统安装在新建子句柄；旧BDS的设备列表在连接驱动前取得，未包含这个子句柄。现先连接UFS和RAM盘，再重新枚举SimpleFileSystem，仅选择RAM盘MediaId、512字节块且只读的卷，继续校验HAKIOS.EFI完整长度和哈希。另区分查找卷、打开卷、打开文件、读取、校验、LoadImage和StartImage停点。

4项ASAN/UBSAN主机协议模拟直接执行实际BDS查找函数，覆盖新子卷、原父卷文件系统、无RAM文件系统和OpenVolume失败，并检查不打开排在前面的物理UFS FAT及句柄数组释放。7项UFS传输模拟及APFS双读取器校验通过。实际内核、loader、probe、UFS DXE、PartitionDxe、BDS DXE共6个二进制通过ARMv8指令检查，3个DXE哈希与FV构建报告一致。冻结Android/gzip/FAT/XNUP逐层解包核对通过。

活动内核仍是完整HakiOS固定提交63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / 8796.121.2。内核、诊断PID1及APFS根盘内容与第13轮相同；此次源码修正集中在Mu的板级BDS。真实只读HFS挂载、18H17版本/dyld/launchd文件长度与FNV核对仍待实机完成。

下一轮使用 `python scripts/boot-hakios-lithium.py --boot`，先核验Fastboot身份、解锁及userdata容量。命令行仍为 `bootcpu=1 cpus=1 serial=3 -v ufs=2 lithium_storage_test=1 loader_return_after=120`。约120秒测试加25秒加载停留，结束摘要持续保留；方便时提供一次结果即可，无需守屏或秒表。

通过条件沿用[首个P2测试契约](hakios-p2-storage-test-ready.md)：LOADER READY、HEADER及三个文件match=1、MOUNT/UNMOUNT成功、RESULT PASS且正常定时返回。失败按具体停点记录，不推定UFS/HFS或iOS兼容通过。第13轮原包和照片保持冻结，P1三次合格不重测。当前累计临时启动13次，本次修正准备没有新增设备启动或分区写入；手机最后照片停在R13的UEFI失败画面。
