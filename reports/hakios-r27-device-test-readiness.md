# R27 实际上机测试就绪

2026-11-16。已重新核验授权MIX当前处于Fastboot，设备身份、解锁状态、userdata容量和电池启动条件全部通过。R27启动包15,773,696字节，小于实查536,870,912字节下载上限；当前源与190项冻结证据均一致。没有R27启动意图或启动记录，累计仍26次HakiOS临时启动。

project.json和README中的旧资源字段已同步；WSL当前实测12个逻辑处理器、MemTotal=16,379,364KiB、swap=2,097,152KiB。活动资源配置为16GB/12CPU，任务14GB/1200%CPU、并发12，仍顺序运行大任务。

本轮目标“推进到能实际上机测试”已达到。下一入口为[一次性R27启动脚本](local/boot-hakios-r27.py)，它会再次检查冻结包、设备身份、解锁、容量、电池和防重试记录，再执行一次临时启动。测试为120秒运行、另25秒加载停留，结束摘要保留，无需守屏、秒表或中途拍照。

本次只有设备查询，没有临时启动或分区写入。系统继续从UFS/HFSX按需读取；原始iOS14.8 launchd及所选服务的运行结果仍待真机。P1、R20存储和R24动态工具已通过，不重复其整套验收。

镜像SHA256：`d1f3af4e79baef473cb1308cd52e7604ca92a62231f38ea18f80b9057ee6f4fa`。

[本次实际核验](hakios-r27-device-test-readiness.json) · [修复包与检查边界](hakios-p2-ios14-launchd-handoff-ready.md) · [iOS14/16版本审查](hakios-ios14-version-alignment.md)
