R30运行中三张照片确认原始launchd[1]完成early boot并进入system bootstrap，随后原始SpringBoard在+[SpringBoardUI load]、SpringBoardUI.m:56触发NSInternalInconsistencyException；PID11、20、21反复abort，桌面初始化失败，继续等待不能消除相同断言。照片中的0x1c9469ebc减同次cache slide0x23f68000，准确落在原始18H17的+[SpringBoardUI load]+0x1a0；原始代码首先引用com.apple.springboard-ui.client权限标记，但尚未观测具体运行时查询结果，不能将缺失entitlement写为已确认根因。同时多项服务报persona type6查询失败。缺launchd.plist/NRN cache后仍继续early boot，不可当作此次明确崩溃的直接原因。三个timer样本50.069、110.137、170.161秒均stalled0，照片未见kernel panic、结束摘要、服务诊断结果或图形界面；运行结论仍限于这些同次证据。本次仅归档与只读原镜像分析，无手机查询、启动或分区写入，累计仍30次。R30入口已消费。下一步优先核对SpringBoard早期进程权限查询与上游集成，再处理persona兼容；保留现有UFS布局和完整HakiOS基线。

以下为该轮启动时记录，运行状态以上述照片为准。

2026-11-16T04:09:33.061024+08:00，R30权限修正联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。复用R29同字节完整HakiOS内核与loader，系统仍由现有UFS/HFSX按需读取。服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。累计30次HakiOS临时启动；本轮未写分区。R30入口已消费，不自动重试。

镜像SHA256：`e9a2f7d5b2d8fe97e0964f324a12275bec61dbb9ee45b7502679968505c21104`。
