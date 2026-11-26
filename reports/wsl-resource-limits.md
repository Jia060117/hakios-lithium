# HakiOS 任务资源限制

2026-11-16更新。用户确认电脑硬件问题已修复，并明确批准WSL使用16GB内存、12个逻辑处理器。原资源观察及旧路线恢复记录逐字节存于 [重构前归档](history/hakios-refactor-20261114/reports/wsl-resource-limits.md)，其中旧命令停止使用。

已备份并修改本机 `.wslconfig` 为processors=12、memory=16GB、swap=2GB，交换文件保持D盘。重启WSL后实测12逻辑处理器、MemTotal=16379364KiB、swap=2097152KiB。主机约32GB物理内存、24逻辑处理器。配置副本见reports/local/20261116-approved-wsl-before.ini。

资源值统一来自 [配置](../config/hakios-resources.json)。活动入口scripts/run-wsl-limited.ps1默认hakios-check，覆盖HakiOS审计、版本审计、准备、配置、依赖、支持组件、诊断、内核和打包；旧kernel/native-package/root-transfer在访问WSL前拒绝。

systemd任务统一使用ios-mix1-hakios unit，MemoryMax=14G、MemoryHigh=12G、MemorySwapMax=1G、CPUQuota=1200%、TasksMax=1024、User=jia，环境JOBS=12、MAKEFLAGS=-j12、MAKEJOBS=--jobs=12。已从实际运行的版本审计任务读取cgroup和环境并核验，见 [实测](wsl-resource-limits.json)。固定unit拒绝并行大任务；Python入口核验实际VM和cgroup配额。内核、依赖、Mu固件和主机APFS工具的活动构建入口同步使用配置并发。

本轮按用户确认的计划顺序执行版本审计、隔离checkout同步、上游配置/驱动清单检查及完整依赖核对。新包准备继续受来源、P0输入和加载契约门槛约束。无QEMU或手机操作；旧7195路线保持冻结。
