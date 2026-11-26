# iOS16 R11 同次运行观察

2026-11-17T21:13:46.667360+08:00 临时启动一次，Fastboot接受。代码提交`44ec363`，冻结输入和验证见[准备报告](hakios-ios16-r11-prepared.md)及[六项问题](hakios-ios16-r11-investigation-plan.md)。未重刷userdata，未重试R10，未执行旧7195或QEMU。

截至2026-11-17T21:20:56.697745+08:00仍在运行，接收器与每45秒只读观察器在后台保存同次快照，结束时保留终态。当前冻结快照`reports/local/20261117-211346-406347-ios16-r11/snapshot-212056-697745`，568408字节/3477帧，帧解码与正文一致，缺口/重复/残帧均为0。**运行尚未结束，不能声称整轮完整，也不能由USB无缺口推断源端无丢弃。**

## 已取得的新证据

- 三个XNU核心实际online，原始launchd已启动；首个基础图层在内核60808ms提交。没有当前手机画面目视证据，不能写成物理桌面通过。
- 已启动目标的新增方法采集点：SpringBoard 1/1、containermanagerd 4/4、lsd 5/5；LS阶段/group观察器11/11，安装计数周期重现。未见拒绝安装。独立system容器job未见启动，不能把这些记录当作两类容器job都通过。
- SpringBoard查询已明确为 **`com.apple.mobilenotes`**，该次调用仍活动，主线程尚未进入主队列或提交桌面。
- 最新lsd统计：`LITHIUM PROGRESS pid=28 op=0 name=bundle begin=144 end=144 fail=1 distinct=144 repeat=0 total_us=39116547 max_us=1136481`；`LITHIUM PROGRESS pid=28 op=1 name=container begin=399 end=399 fail=140 distinct=399 repeat=0 total_us=30564750 max_us=927600`。distinct和完成数仍增加、当前键repeat=0。证明这一观察区间正在处理不同应用/容器，不能认定卡在同一项重试。
- 容器查询中观察到nil返回；有些调用create=0且没有NSError，不能把累计nil数直接当作致命错误。DisplayCal.app的一次bundle初始化返回MIInstallerErrorDomain/35，随后继续处理其他应用；它并未停住整个扫描。下一步需结合具体调用语义和后续完成条件解释。
- 首轮实际文件调用统计已到达：`LITHIUM FS snapshot dropped=13 full=0 stats_full=0 end_lost=1 replaced=0 ticks=116736054054`。活动记录容量未满，但有13次争用丢弃、1次结束丢失；该项不是无损全量追踪。lsd已记录的close最大约9.8ms，write_nocancel最长约0.835s；这只能描述已记录调用，尚不能排除其他调用/下层存储等待。
- 未见setSeedingComplete完成记录；本轮仍在进行，不能把“截至此时未见”写成最终永不ready。用户态状态表目前未见容量满或身份不可用，后续仍需看终态。

## 接下来的判据

保持本轮继续采集。观察扫描是否完成、哪一具体对象的活动调用不再变化，以及数据库group/ready完成后等待是否返回；将同PID/TID的用户方法与文件调用/上游多线程栈配对。不会根据目录不存在的探测错误、原始errno或单次相同栈直接宣称根因。日志与快照保存在本轮本地目录；不发起第二次启动。

当前真实桌面、最终根因、长期SMP、全部6GB、GPU和物理触摸仍未通过。
