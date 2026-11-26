# 误入 Android 后的 iOS 根卷完整性检查

2026-11-14 17:05完成。用户说明只看到小米/MIUI标志或启动动画，然后返回Fastboot。

已从手机 userdata 读取完整的17179869184字节（16GiB）HFSX根卷，SHA256为 `396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`，与安装源完全一致。无需恢复或重刷。检查范围是整个已安装HFSX卷，不包括userdata剩余未用于此卷的空间或其他分区。

临时镜像保留对应原厂内核，仅RAM ramdisk运行init、ueventd、adbd和自编译AArch64块读取程序。没有recovery UI、Android服务、自动挂载或格式化服务。实测 `/dev/block/sda15` 为userdata，起始512字节扇区786432，长度250710192128字节；磁盘sda和分区sda15的只读状态均为1。`/proc/mounts` 未出现任何块文件系统。

读取程序仅允许已核验userdata几何范围内的只读读取；init以临时root权限设置BLKROSET，adbd实际以uid/euid=2000运行。没有安装永久root、刷写boot/recovery、重建分区或改动userdata。

卷头4096字节与原镜像相同，CRC32 `0x6143e5cf`，HFSX标识 `48580005`。从手机读回镜像独立提取 `/sbin/fsck`、`/sbin/launchd`、`/usr/lib/dyld`，各自SHA256与原固件记录相同。

电脑端任务中断曾在本地副本尾部留下约284MiB未完整落盘的数据；重新读取手机对应位置与原镜像一致。将本地副本修整到已核验的6702497792字节前缀，重新核对续传边界后继续。最终哈希覆盖完整16GiB，未把中断或部分读取当作成功。

机器可读结果：`ios14-root-integrity.json`。实机USB、卷头、关键文件、临时镜像及命令日志：`reports/local/20261114-163633-root-readback`。随后通过诊断ADB返回已核验Fastboot，并发送第十五轮XNU镜像；XNU启动结果仍需屏幕照片。
