# R34 UFS 桌面触摸验收（运行中）

2026-11-20，Asia/Shanghai。用户在获知UFS桌面测试会正常写入Data后要求“测试呀，手机已经连接了”，本轮执行一次冻结桌面包的Fastboot临时启动。

## 已观察

Fastboot于 **17:17:31.703118+08:00** 接受本轮唯一一次临时启动。启动前身份、product、解锁、电量及下载窗口五项只读预检通过。累计HakiOS临时启动60次、iOS16为29次。

已收到本次USB串流：实际 `bootcpu=1 cpus=1`、MPIDR `0x80000001`、affinity1、match1；APFS系统根和HFS Data均已挂载，Data journal replay完成。当前是完整UFS用户态启动，不是先前RAM hold。

本次同样完成GPIO89低10ms/高300ms，实读 `ID=0xe7 error=0`，进入63字节报告轮询；`/dev/hakios-touch` 存在。SpringBoard的 `main_queue_enter elapsed_ms=75370` 已出现，backboardd有acquired main display digitizer及 `hakios: input: touch screen service` 记录。这些记录尚不等于可见主屏或点击响应验收。

17:21:04+08:00的同次运行快照：已成功读取至少7800份报告，实际点击/滑动/松手产生至少54条成功入队事件，见type1/2/3，未见触摸read_error或report_error。独立重放该时点已保存的5176帧、1177842控制台字节一致，传输缺口/重复/残帧/device-loss为0。只是运行中连续前缀，不声称已采到结束timer/drain。接收器保持运行。

用户已被告知主屏出现后先点一个图标，再左右滑动；当前保持同次运行与USB连接。界面响应、触点坐标及多指结果等待本次实际操作和观察。

用户随后报告界面响应约需几十秒，短按可能表现成长按，认为已无法继续正常交互测试。本轮因此记为“输入已到backboardd、部分可见响应但性能不可用”，不能记为完整触摸功能通过。日志实证90.658ms原始DOWN/UP在EventFilter中相隔99.698秒处理；性能证据与下一步见 `reports/hakios-ios16-r34-input-performance.md`。同次运行与USB接收保留，没有为此自动重启。

随后在同次保存日志中确认backboardd完整读取了队列记录：

```text
hakios: input: sample type 1 finger 0 at 41930 47898
hakios: input: sample type 3 finger 0 at 41930 47898
hakios: input: sample type 1 finger 0 at 54006 47802
hakios: input: sample type 2 finger 0 at 51093 47609
```

子代理Einstein定位实际构建源码 `userland/src/inject.c:959-963`：这段日志在完整read成功后输出；主代理独立读回上述实际行及985/856的digitizer调用，确认它能证明backboardd已读到DOWN/MOVE/UP。`IOHIDVirtualServiceClientDispatchEvent` 的返回值未被检查，所以不扩大为界面已响应。该consumer仅打印前4条记录，后续没有sample行不代表停止读取。源证据见 `evidence/touch-live-20261120/events/r34-ufs-consumer-note.txt`；未据此新增代码、重建或重启。

## 冻结包

- 镜像：`artifacts/hakios/lithium-ios16/phase4-r34-touch-live-ufs-final/lithium-hakios-ios16-r34-touch-live-ufs.img`
- SHA-256：`47740664f56fcf021096493444cc96934223f49ce0dfc099920bfb73cfc426ed`
- 内核SHA-256：`799bdcec2e72fc6f3a0d34c810431254687ba5ef772566e851a97956fa60650c`
- 显示沿用R20-display的1080×2040 BGR24，360×680 points，scale3。

实际payload参数：

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1 touch=1 touch_log=1 touch_poll=1
```

冻结参数的内核上限为10800秒，主机USB接收上限12600秒；本轮交互验收不需要等待该上限才开始。未启用UFS自动返回Fastboot。

## 操作范围与保留

五项只读预检后一次临时boot；没有分区刷写/擦除、ADB、Android/Recovery、QEMU、固件下载或自动重试。系统按原有UFS配置正常读写Data，不能把“没有刷分区”写成“没有文件系统写入”。启动后只读取已保存的主机USB日志，不发额外设备查询。

原始记录：`reports/local/20261120-r34-touch-live-ufs-approved/`。本报告是运行进度，尚非最终timer/drain或全程日志结案。RAM短测的7200份报告/197事件结果属于前一独立运行，不挪作本次UFS点击验收证据。
