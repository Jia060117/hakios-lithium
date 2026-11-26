# HakiOS MIX 第16轮：卷头读取通过，HFS挂载节点错误

2026-11-15 20:24:31临时启动镜像 `89b3dd7e62d9c1f1e06ac8ce899543122f6a7308bb9c9ae0d28f2567145a86c4`。同次照片、原图SHA256、转录、冻结输入和源码证据见 [JSON](hakios-device-r16.json)。

照片确认完整HakiOS内核和诊断程序执行，读取真实userdata前8192字节并核对match=1。上游libmini入口修正有效，本轮未出现panic；硬件计时120172ms、7325次PPI27中断后正常定时返回，结束摘要保留。

HFS挂载返回-15（ENOTBLK）。冻结诊断程序的fspec为字符节点/dev/rufs_userdata；完整HakiOS已创建块节点/dev/ufs_userdata。实际XNU的VFS挂载路径要求VBLK，字符节点在进入HFS挂载实现前即被拒绝。下一轮仅修正诊断调用，原始节点保留用于只读卷头读取，不改通用内核或文件系统的类型检查。

START和RESULT未显示另有确定原因：诊断输出遗漏标签后的空格，loader按带空格的前缀匹配。没有据此虚构RESULT行或文件读取结果。HFS挂载、18H17三个文件和iOS14进程均未验证，P2未通过。

累计HakiOS临时启动16次。本次仅归档照片及只读源码证据，没有新boot或分区写入；P1保持通过，未运行旧7195或QEMU。
