R3 真机结果（2026-11-16）

R3 已结束并由用户及照片确认返回 UEFI 保留摘要。USB 最终 120,934 字节/2,473 帧重新解码与日志逐字节一致，无丢失、重复或不完整帧；原始 launchd、SpringBoard、backboardd、containermanagerd 已启动，上游容器补丁已执行。屏幕摘要 RAW BYTES 252074、PANIC CAUSE NOT RECOVERED，末尾仍是 systrace 文件查询。USB 约30分钟后 Pipe error，未捕获早期内核及返回尾部，不能据此确认定时正常结束或 panic。当前修复重点是补齐控制台采集、减少全局 syscall 刷屏和明确返回原因；不重试已消费 R3，不重复刷 userdata。桌面、容器服务响应、物理触摸、GPU、全6GB与跨重启持久读回仍待验证，阶段四未完成。

UFS System/OS/App cryptex、可写 Data、45 项 init 检查和 PID1 launchd 均有本轮证据。SpringBoard[8]、backboardd[13]、containermanagerd[14] 的 running 仅证明进程启动；没有确认服务 RPC 或图形帧提交。backboardd 报生物识别设备缺失，未见对应退出。容器补丁记录 hakios: container manager: no personas；不能将 persona 查找提示直接认定为 iOS14 旧故障复发。

USB 接收终态 capture_failed / Pipe error 如实保留；已收到的数据独立复核通过。此前 49,118 字节初始快照继续保留，最终快照和两张照片仅放在 reports/local 忽略目录，哈希见 JSON。照片不能证明无 panic；约30分钟和已配置返回期限只支持定时返回假设，未捕获的终尾不作成功验收。
