# HakiOS R27：原始launchd已启动诊断任务

R27同次照片确认HFSX只读根切换rc0、原始18H17 launchd403488字节match1、ramfs初始化2检查/0失败。标准描述符各步骤完成，LAUNCHSTDIO成功、tty errno19已接受；原始launchd以PID1执行并启动诊断子任务PID3/PPID1。121313ms、19905次PPI27后正常定时返回，无panic；此前退出78在本轮未重现。照片没有LAUNCHPROC或LAUNCHRESULT，notifyd/cfprefsd及完整服务验收仍未通过。观察器先等待60秒、原始ps另有30秒预算，120秒总窗口可能先结束；该轮真实任务开始时间未记录，不能断言只是超时。原图、转录与190项冻结证据已核验，累计仍27次；此次仅归档，无手机操作或分区写入。

`LAUNCHJOB START PID 3 PPID 1`结合冻结的原始launchd交接路径，证明PID1已执行原始launchd并成功派生诊断任务。`BEFORE_EXEC`与`LAUNCHEXEC attempted`本身只表示尝试；本轮结论另有真实子任务标记支持。`original_18H17_jobs=1`是观察器输出的固定诊断标记，不能当作notifyd/cfprefsd启动成功。

`KERNEL TIMED RETURN OBSERVED`表示本轮按测试时限正常结束，摘要不会自行继续执行。`DYNEXIT PID 2 SIGNAL 1`属于PID2的退出记录，不能解释成PID1崩溃；具体PID2身份尚未从本图确认。

验收状态：R26描述符检查阻塞已解除，原始launchd交接与子任务启动已取得真机证据；本轮完整服务检查结论为未完成，不记作PASS或确定服务失败。没有进程快照，也不能据此声称notifyd/cfprefsd不存在。

下一步先核对观察器60秒等待与120秒总预算的关系，补足PS执行、等待及最终结果在摘要中的可见性，再验证两个原始基础服务。P1、R20和R24保持结案。系统继续UFS/HFSX按需读取，无证据要求改分区或文件系统。

距离可操作的iOS桌面仍有基础服务、显示合成/IOSurface、Adreno530或经验证的软件渲染路径、触摸、backboardd和SpringBoard集成等未完成项。P0只闭合了有界方案和构建输入，A530/iOS14图形运行尚未验证，因此当前不能可靠给出到桌面的小时或天数。

[同次JSON与原图证据](hakios-device-r27.json) · [R27冻结包](hakios-p2-ios14-launchd-handoff-ready.md) · [版本边界审查](hakios-ios14-version-alignment.md)
