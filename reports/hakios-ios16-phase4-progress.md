# iOS16 联合调试变更记录

唯一活动内核是完整 HakiOS 63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2，用户空间是用户提供的 16.5/20F66/D22AP/arm64。阶段四已获授权，尚未完成。R5已定时返回，首帧后完整30分钟仍未见桌面；最终诊断见reports/hakios-ios16-device-r5.md。R4 已确认原始 iOS 用户空间绘制白苹果、三次真实图形帧提交与物理屏幕显示，SpringBoard 已与 backboardd 建立连接。真实桌面、物理触摸、A530 加速、完整 6GB 和 Data 跨重启持久读回仍待验证。

| 修订 | 变更与实际结果 |
| --- | --- |
| R5 已定时返回，桌面未完成 | 首帧1095537ms后完整观察30分钟，约49分钟终止。USB309344字节/6890帧重解码一致，无帧缺口，仍缺返回尾部。六次图层提交、System shell连接已见；activity assertion已返回，虚拟HID已有对象，但无明确屏幕所有者。末尾free约1750MB。下一修订定位SpringBoard初始化/主队列与屏幕交接，不单纯再加时；报告已归档，未操作设备。 |
| R1 | 采用上游双 APFS 引导卷；修正扩大后的 XNUP 源数据被过早 info 初始化覆盖。完整内核构建、加载边界和实际启动包检查通过，临时启动一次获接受。照片显示已返回 UEFI，stackshot 二进制掩盖正文；USB 接收端在 LANGID 查询时退出，0 字节日志。 |
| R2 | 以 XNU v4 panic 头的明确长度读取正文，保留原因、任务和回溯，显示限定行数；原始日志和 stackshot 留在 RAM。接收端保留描述符错误、精确核验 serial，修正异常结束状态和资源释放。完整 HakiOS、loader、Mu 构建及实际函数 ASan/UBSan、46 项指令扫描、16 项真实包校验通过。临时启动一次；照片确认 init 的 namespace9/code1 装载失败。USB 精确身份和端点配置成功，0 帧后 Pipe error，传输未通过。 |
| R3 已返回 | libmini 静态程序恢复上游 cctools，动态库继续使用已准备的 LLD。完整用户空间、内核、loader 和 Mu 构建及真实静态程序/APFS 读回检查通过。临时启动一次，镜像 SHA256 0fd0502ac4537c92802429fdba90f40357233da2121206d2bceec85a0c6e33ee。USB 实际收到连续日志，初次冻结快照 49,118 字节/1,583 帧，无丢失或重复，原始帧解码与控制台前缀完全一致。System、OS/App cryptex、UFS Data 挂载，Data 写入/读回和 45 项 init 检查通过，原始 16.5 launchd 成为 PID1 并进入 early boot。用户及照片确认返回 UEFI；最终 USB 120,934 字节/2,473 帧完整解码，无丢失/重复。原始三项关键进程已启动，上游容器补丁已执行；终态 Pipe error，返回原因与服务响应/桌面仍未确认。 |
| R4 定时返回、白苹果已显示 | 接入实际 BSD 控制台，移除重复采集，关闭全局 systrace 并保留 usertraps；返回摘要明确分类 timer/panic/未知。完整 HakiOS、loader、Mu 构建与检查后临时启动一次。USB 206,173 字节/6,229 帧重新解码完全一致，缺口/重复/不完整帧均 0；从内核横幅覆盖至图形启动。ProgressUI 加载原始 Apple Logo，backboardd 创建 IOSurface，显示驱动真实 swap 1/2/3；用户录像截图确认面板显示。SpringBoard System shell 连接已建立，仍有 BootUI 所有权与 digitizer 等待，无桌面证据。UEFI 照片确认 TIMER DEADLINE REACHED、保留日志未见 panic 正文；USB 最终 Pipe error 且未覆盖返回尾部，完整采集未通过。 |

R2 的 `SIGILL` 不能单独解释成 CPU 缺指令。当前 XNU `bsd/sys/reason.h` 明确定义 namespace9 为 OS_REASON_EXEC、code1 为 EXEC_EXIT_REASON_BAD_MACHO。冻结 R2 的静态 init/stage0 实际携带 MH_DYLDLINK、LC_MAIN 和 `/usr/lib/dyld` 依赖，而同包 md0 引导卷没有该文件。恢复上游静态链接器生成 LC_UNIXTHREAD 后，R3 已执行 stage0/init 并交接原始 launchd；此前装载停点已解除。

R3 日志采集已结束；快照从 init 开始，包含原始 launchd 的第一条日志，但不包含全部早期内核日志，不能记作完整内核日志已捕获。2,880MiB 内存窗口及单 CPU 是实际运行配置，全 6GB、多核、桌面和物理外设验收仍未完成。

没有删除旧适配、执行 7195/QEMU、加入 Apple 二进制或重复刷写 userdata。阶段三和 R2 的冻结报告均单独归档，之前的已消费单次启动入口保留。

R3 已结束并由用户及照片确认返回 UEFI 保留摘要。USB 最终 120,934 字节/2,473 帧重新解码与日志逐字节一致，无丢失、重复或不完整帧；原始 launchd、SpringBoard、backboardd、containermanagerd 已启动，上游容器补丁已执行。屏幕摘要 RAW BYTES 252074、PANIC CAUSE NOT RECOVERED，末尾仍是 systrace 文件查询。USB 约30分钟后 Pipe error，未捕获早期内核及返回尾部，不能据此确认定时正常结束或 panic。当前修复重点是补齐控制台采集、减少全局 syscall 刷屏和明确返回原因；不重试已消费 R3，不重复刷 userdata。桌面、容器服务响应、物理触摸、GPU、全6GB与跨重启持久读回仍待验证，阶段四未完成。


R4 冻结镜像 SHA256 `8a3230840e8b5636be18fa92e13cef781811e45f057632072a95b6ea2a1bad6d`，已消费单次入口，不重试。最终结果见 `reports/hakios-ios16-device-r4.md/json`；原始照片和日志仅本地保留。


R4 已结束，当前最后屏幕证据为 UEFI 保留摘要；本次归档未再次查询或启动设备。下一修订重点是配件/HID 初始化等待、BootUI activity assertion 与 SpringBoard 屏幕接管，以及软件绘制/面板复制分段耗时。backboardd 自报 CoreAccessories 211.406 秒、HID 总计 176.407 秒、窗口服务 113.207 秒，main 至 backlight ready 总计 523.118 秒；这些内部计时尚非外部秒表核验。当前单 CPU、2,880MiB，末尾约 1,956MB free，不能把所有等待归因为硬件老旧或内存不足。下一包完成并冻结后再上机，不要求守屏，不重复刷 userdata。
