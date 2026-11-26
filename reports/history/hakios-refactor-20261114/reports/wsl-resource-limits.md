# WSL 资源限制与中断恢复

2026-11-13，本机物理内存约32GB、24个逻辑处理器。原配置允许WSL使用24GB内存及24个处理器，交换空间4GB；已改为以下配置，并执行 `wsl --shutdown` 后重新启动Ubuntu：

```ini
[wsl2]
networkingMode=mirrored
dnsTunneling=true
processors=4
memory=8GB
swap=2GB
swapFile=D:\\WSL-Swap\\ios-mix1-wsl-swap.vhdx

[experimental]
autoMemoryReclaim=dropCache
```

配置位置：`C:\Users\Jia33\.wslconfig`。修改前副本存放于 `reports/local/20261113-164423-wslconfig-before.ini`。D盘交换文件实际创建成功，避免继续占用剩余空间约10GB的C盘。

Ubuntu实际验证：MemTotal=8135952 KiB，处理器4个，swap=2097152 KiB。该内存上限针对WSL2虚拟机；Windows中的虚拟机管理和其他程序仍有自身开销。参数和重启规则依据Microsoft官方文档：https://learn.microsoft.com/en-us/windows/wsl/wsl-config 。

项目XNU、UEFI、DarwinBoot和HFS主机构建的默认并发均降至2。额外通过systemd限制当前构建/归档任务：MemoryMax=6GiB，MemoryHigh=5GiB，MemorySwapMax=1GiB，CPUQuota=200%，TasksMax=256。已读取运行任务cgroup，确认这些值生效；任务子进程也属于同一服务组。

可从PowerShell使用持久入口：

```powershell
& F:\IOS-MIX1\scripts\run-wsl-limited.ps1 -Task kernel
& F:\IOS-MIX1\scripts\run-wsl-limited.ps1 -Task root-transfer
& F:\IOS-MIX1\scripts\run-wsl-limited.ps1 -Task native-package
```

按顺序运行，不同时启动多个大任务。入口先验证WSL已识别不超过8GB内存和4个处理器，再运行指定任务。

`native-package` 使用同样的任务资源限制，顺序运行最新内核的指令扫描、页表别名和DMA参数检查，再打包外部HFS根卷启动镜像并离线验证。17:11已运行通过。该入口用于内核重新构建后同步更新启动包，避免构建报告与旧启动包的内核哈希不一致。

本次从已有 `/home/jia/ios-mix1-build/xnu7195-lithium` 恢复增量构建，返回0。没有重新下载或清除源码和构建目录。完整根卷已在16:19完成内容、元数据、fsck和哈希验证；蓝屏中断了向E盘归档。已有Android稀疏镜像解码后逐字节SHA256匹配完整根卷。中断留下的E盘副本未通过前缀比较，保持原文件内容并移入 `E:\IOS-MIX1-artifacts\ios14-d20ap\interrupted-20261113-165139`；从验证过的源重新归档。

归档新增 `--resume`：先验证现有稀疏数据、完整归档或未完成副本前缀；用8MiB缓冲复制，写入 `.partial`，完成、刷新和哈希匹配后才发布最终文件名。测试通过：既有完整归档复用、两个中断恢复场景，以及拒绝损坏前缀且保持文件不变；原稀疏内容篡改、DONT_CARE、截断和尾随数据拒绝检查也通过。结果见 `ios14-root-transfer-validation.json`。

Windows系统日志记录了2026-11-13约16:00和16:24两次错误后重启，分别为 `0x1A`（参数1=`0x3F`）与 `0x154`。Microsoft将前者定义为分页读入的CRC不一致，将后者定义为内核内存存储组件异常。因此目前不能确定根因只是WSL占用过大，也不能宣称蓝屏已根治。Windows当前分页文件位于E盘；磁盘查询均报告Healthy，但未进行转储调试或硬件诊断，也未修改Windows分页文件配置。

代码定义依据：https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x1a--memory-management 与 https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x154--unexpected-store-exception 。

资源观察明细位于忽略目录 `reports/local/wsl-resource-observation.jsonl`。首次完整稀疏验证及前缀拒绝运行约62秒，服务峰值约5.0GiB、swap峰值0；监测时Windows剩余物理内存约10.3GiB。本轮操作均为电脑端配置、构建和文件恢复。

恢复结果：16:59完成全部归档和E盘原始/稀疏副本读回SHA256验证，返回0；任务运行7分14秒，服务峰值约5.0GiB，swap峰值0。原始根卷17179869184字节，SHA256=`396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`；稀疏镜像9383190364字节，SHA256=`d9d1bb002bb53259c718ff2b1ca7f6132609ee420e2a619711ef130c11807751`。14605个稀疏块的所有解码字节已验证，跳过块为0。详见 `ios14-root-transfer.json`。

结束后16:59:58监测：WSL工作集993472512字节（约0.93GiB），Windows空闲物理内存18636689408字节（约17.36GiB）。本轮任务期间未发现新增蓝屏记录；ADB已确认lithium且boot_completed=1。没有对手机执行新启动、刷写或清除操作。
