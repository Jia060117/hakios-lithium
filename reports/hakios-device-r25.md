# HakiOS R25：原始launchd PID1 SIGABRT

R25同次照片确认HFSX只读根切换rc0、原始launchd403488字节match1、上游ramfs目录初始化2项检查/0失败。PID1交接后原始dyld438映射rc0、精确14.8旧Mach入口生效；70395ms/9355次PPI27后PID1因SIGABRT终止，panic显示no exit reason available、signal6、exitstatus78。原始launchd已进入执行路径，稳定PID1及所选服务未通过；具体主动退出原因尚未定位，照片未显示诊断任务或进程快照。165项同次冻结证据、原图及转录已核验。P1/R20存储/R24动态工具保持通过，P2整体未通过；累计25次启动，本次归档无设备操作或分区写入。见reports/hakios-device-r25.md。

源码已核对：panic 的 exit status 使用十进制，屏幕78即`0x4e`，此前JSON的`0x78`解释已纠正。原始18H17 launchd在标准输出是终端时打印“cannot be run directly”并退出78，当前init将stdout接到控制台，符合一个具体候选原因。该错误行未在本次照片中显示，不能当作已经实测定位。上游加载器已自动添加`rd=md0`。下一包修正描述符并保留上游既有日志；当前证据不足以归因于文件系统格式、UFS硬件或整体iOS14版本不兼容。已通过子项不重复测试。

[同次JSON与证据](hakios-device-r25.json)
