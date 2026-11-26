# P2只读挂载调用修正包

[R16](hakios-device-r16.md)已经验证诊断启动和内核读取userdata卷头，HFS挂载因传入字符节点返回ENOTBLK。现已将fspec改为完整上游提供的块节点/dev/ufs_userdata，卷头读取继续使用/dev/rufs_userdata；挂载仍为MNT_RDONLY，物理BlockIO继续拒绝写入。没有修改通用内核、UFS桥或HFS实现。

START和RESULT的标签后补齐空格，确保loader现有前缀匹配能保留它们。statfs64核对只读HFS成功后追加MOUNT rc=0 readonly=1。四项主机检查执行实际诊断代码和loader摘要函数，使用模拟syscall与明确的合成文件，确认正确块节点/只读参数、实际输出保留、注入ENOTBLK失败、可写标志拒绝、文件不匹配拒绝；ASAN/UBSAN通过。它们不证明真机HFS挂载或iOS兼容。

镜像SHA256：`95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6`，15,728,640字节。冻结路径为artifacts/hakios/p2-storage-ready-block-mount/lithium-hakios-test.img。119项冻结证据、ARMv8检查和Android/gzip/FAT/XNUP独立解包见 [JSON](hakios-p2-storage-test-ready-block-mount.json)。完整内核字节与R16一致，SHA256：`3556a5e8543aae8ab8cfabc41fda08fabc457305f8d99e1c8a852d289ad0dd78`。loader源码及除COFF时间戳外的所有二进制字节与R16一致；其新时间戳使loader哈希及Mu校验标签改变，比较证据已记录，未笼统宣称整个包字节一致。R16冻结包和原图保持原样。

下一次测试约120秒，加25秒加载停留，结束摘要保持到重启。验收要求同次HEADER和VERSION/DYLD/LAUNCHD均match=1、MOUNT rc=0 readonly=1、UNMOUNT rc=0、RESULT PASS且正常定时返回。无需守屏或中途拍照，P1不重复。即使文件检查通过，也不表示已经执行iOS14 dyld/launchd或验证SpringBoard、GPU和触摸。

本次准备未boot或写分区，累计HakiOS临时启动仍为16次；未运行旧7195或QEMU。当前手机最后画面证据为R16结束摘要，进入Fastboot后依既有授权核验本机、解锁、容量和电池再临时启动一次。
