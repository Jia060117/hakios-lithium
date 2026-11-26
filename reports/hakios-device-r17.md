# HakiOS MIX 第17轮：卷头通过，HFS挂载 EINVAL

2026-11-15 21:04:19北京时间临时启动冻结版本block-mount，Fastboot接受。镜像SHA256：`95524201bb79ccae4e6864f9dc27fe48bd3186972a19020ab6dfecf67b64bde6`。同次输入、命令、原图与转录SHA256、冻结源码依据见[JSON](hakios-device-r17.json)。归档前核对119项冻结证据和本轮实际启动副本，全部一致。

照片确认完整HakiOS内核和诊断程序执行，读取真实userdata前8192字节并核对match=1。START与RESULT行现在正常显示，分隔空格修正确实有效；本轮未出现panic，120177ms、7433次PPI27中断后正常定时返回，结束摘要保留。两次采样停滞0次、最小增量3359，最后timer样本为110177ms / 6844 IRQ。

挂载返回EINVAL（规范化mount_rc=-22），并明确显示RESULT FAIL STAGE 2。照片可见的是MOUNT RC后面的22；冻结板层字体只绘制字母和数字，负号及等号不绘制。负号依据同包syscall封装和FAIL stage 2分支推断，不把推断字符混入可见文字转录，也没有声称读取了原始RAM日志。

冻结诊断的fspec已是上游块节点/dev/ufs_userdata，原始卷头仍从/dev/rufs_userdata读取。R16的ENOTBLK本轮未再出现，但EINVAL存在多个VFS/HFS来源，当前照片不能定位具体内部分支，也不能据此断言卷损坏。HFS挂载、只读挂载标记、三个18H17文件和iOS14进程仍未验证，P2未通过；P1保持已通过，不再重测。

累计HakiOS临时启动17次。最后物理状态证据为这张结束摘要，未确认回Fastboot。本次仅归档与更新记录，没有构建、设备查询、新boot或分区写入，未运行旧7195、QEMU或子代理。屏幕证据等待已解除，用户暂停仍有效；438草案仍未集成或编译。用户明确继续后，先定位本轮EINVAL，再推进存储与系统集成。
