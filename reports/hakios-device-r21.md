# HakiOS R21：动态进程收到SIGKILL

第21轮同次照片确认完整HakiOS内核和上游init执行，真实HFSX只读根切换rc0，原始共享缓存头匹配，原始18H17 dyld的438映射count3/rc0。df与mount均被SIGKILL终止，DYNRESULT FAIL stage6；信号来源尚未确定，timeout0不能排除父进程wait错误后的清理kill。120198ms/10116次PPI27后正常定时返回，无panic，摘要保留。P1和R20存储验收继续有效，P2整体未通过；累计仍21次HakiOS临时启动，本次仅归档照片，无启动或分区写入。见reports/hakios-device-r21.md。

原图和可见文字转录均已归档并记录SHA256；核验同次144份冻结证据。屏幕忽略标点，EXIT 1的源码含义是exit=-1，表示被信号终止，不能算作正常退出码1。缓存头匹配不代表运行时每页签名已验证，438返回0不代表动态程序已完成。

下一步核对实际HakiOS等待、签名与进程退出路径，保留等待错误、父进程清理原因和上游内核退出reason；不据此改文件系统或取消签名验证。新镜像准备完成后才需要用户返回Fastboot。

同次输入及测量见[JSON](hakios-device-r21.json)。
