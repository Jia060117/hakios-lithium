# HakiOS R28：原始launchd与所选服务进程检查通过

R28同次结束照片确认LAUNCHRESULT PASS：原始ps在6487ms正常exit0、signal0、timeout0，无清理kill；首次完整快照同时找到PID1 launchd、notifyd和cfprefsd。观察器在自身入口后8045ms完成检查，不能解释为整个iOS在8秒启动。硬件计时181310ms、PPI27增量24934、3次采样停滞0次，按预定时限正常返回，无panic。检查通过后诊断任务继续等待，日志继续滚动不表示这项检查尚未完成。原图、转录及199项冻结证据已核验，此进程存在性子项结案，无需延长或重复。累计仍28次HakiOS临时启动；本次仅归档，无设备查询、启动或分区写入。

| 屏幕字段 | 本轮读数 | 含义 |
| --- | --- | --- |
| LAUNCHRESULT | PASS，attempts=1 | 这项检查首次尝试完成 |
| DYNRUN PS | exit=0，signal=0，timeout=0 | 原始ps正常退出，未触及时限 |
| DYNTIME PS / DYNBUDGET | 6487ms / 30秒 | ps执行时间低于预算 |
| LAUNCHPROC | PID1、NOTIFYD、CFPREFSD均present=1 | 同一完整快照发现三个目标进程 |
| LAUNCHOBS done | job_ms=8045 | 观察器自身入口至完成的时间，并非整个iOS启动时间 |
| KERNEL TIMED RETURN OBSERVED | 硬件181310ms | 测试按程序定时结束，无panic标记 |

冻结观察器在PASS后执行`for (;;) usleep(1000000)`保留进程，其他定时日志可继续产生，直到内核测试时限结束。不能仅凭日志滚动判断还有启动工作没做完。本轮检查已成功，不需要为了该子项延长等待；后续更多服务可能需要另行测量预算，不能由此认定所有启动任务三分钟内完成。

观察器时间来自gettimeofday，硬件总时长来自19.2MHz计数器；两者未建立共同启动时间原点，不能用time_sec推算观察器在硬件时间轴上的启动时刻。本照片也未测量完整iOS启动耗时。

验收范围仅为原始launchd作为PID1及notifyd/cfprefsd在同一原始ps快照中的存在性。没有验证两个服务实际Mach接口、持续可用性、完整P2或SpringBoard。PID2的signal1退出不是PID1退出，具体进程身份未知，不能解释成某个服务崩溃。

本图未显示LAUNCHROOT、文件身份或LAUNCHJOB START的PID/PPID行。它们的执行按冻结包必经成功路径推断，不复用R27数字；保留的屏幕只有解析结果，原始ps行与RAM日志没有导出。

下一步沿完整上游集成核对并验证基础服务的实际Mach请求/回复及依赖，再推进显示与触摸。P2完整服务、图形、触摸和SpringBoard仍未通过；系统继续UFS/HFSX按需读取。R28单次入口已消耗，不再运行。见reports/hakios-device-r28.md。

[同次JSON与原图证据](hakios-device-r28.json) · [进程子项结案](hakios-p2-launchd-process-closure.md) · [冻结准备记录](hakios-p2-ios14-launchd-observer-ready.md)
