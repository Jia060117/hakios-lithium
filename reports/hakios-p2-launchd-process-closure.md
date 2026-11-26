# HakiOS P2：launchd及所选进程存在性子项结案

R28同次结束照片确认LAUNCHRESULT PASS：原始ps在6487ms正常exit0、signal0、timeout0，无清理kill；首次完整快照同时找到PID1 launchd、notifyd和cfprefsd。观察器在自身入口后8045ms完成检查，不能解释为整个iOS在8秒启动。硬件计时181310ms、PPI27增量24934、3次采样停滞0次，按预定时限正常返回，无panic。检查通过后诊断任务继续等待，日志继续滚动不表示这项检查尚未完成。原图、转录及199项冻结证据已核验，此进程存在性子项结案，无需延长或重复。累计仍28次HakiOS临时启动；本次仅归档，无设备查询、启动或分区写入。

结案范围：原始launchd作为PID1，原始notifyd/cfprefsd在同一有效ps快照中存在；完整服务接口、持续可用性、P2整体与iOS界面尚未验证。

下一步沿完整上游集成核对并验证基础服务的实际Mach请求/回复及依赖，再推进显示与触摸。P2完整服务、图形、触摸和SpringBoard仍未通过；系统继续UFS/HFSX按需读取。R28单次入口已消耗，不再运行。见reports/hakios-device-r28.md。

[真机记录](hakios-device-r28.md) · [哈希与测量](hakios-p2-launchd-process-closure.json)
