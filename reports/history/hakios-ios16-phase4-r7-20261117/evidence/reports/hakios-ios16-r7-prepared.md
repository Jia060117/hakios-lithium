# iOS16 R7 上机准备

R6 已捕获正常返回尾部，但 SpringBoard 主线程在 ControlCenterServices → BaseBoard → CoreServices → __ulock_wait 连续等待，未进入主队列/运行循环。R7 用同一次联合启动关联工作线程、锁地址和服务等待，同时区分软件绘制的 CPU 时间与非 CPU 时间。

- 复用固定 HakiOS 的完整 ustacks 多线程 walker 和 top 统计，加入 SpringBoard 与 backboardd、lsd、containermanagerd、runningboardd。首次内核180秒，随后每6分钟，共最多8次；每进程最多96线程/14帧，每轮最多8进程。
- 保存原始 x0/x1/x2/x3/x16 和线程状态。仅解释等待中的 ulock syscall；用正常 copyin 读取锁值，仅 unfair-lock 32位格式尝试只读 owner-port 转换，输出 owner_tid、同任务标志、错误及等待参数与当前锁值是否一致。采样并非全系统原子快照，锁值可能变化，尚未证明持有者或死锁。
- 显示前8次提交记录准备、画行与最多8段的 wall_us/cpu_us/noncpu_us。非 CPU 时间包含调度、阻塞、中断和采样开销。继续使用原软件绘制，未作提速声明。
- 保留R6构造期主线程观察、主队列/运行循环里程碑、正常返回前有界USB排空；背景诊断不阻挡桌面启动。

构建修正：源码校验精确保留R6独立致命异常开关；IPC解锁改为8796真实接口 ip_mq_unlock。主机画行测试保留上游未使用px1的原函数，仅豁免 unused-variable 告警，其余告警为错误，ASan/UBSan保留。

完整内核、413项用户空间、真实D22AP loader和Mu均构建成功；实际函数ASan/UBSan、50项画行比较、依赖同步回归、USB主机与实际包检查通过。源码差异只应用于独立活动树，干净上游checkout保留。

下一步：只临时启动一次冻结R7，自动采集USB，关联相同owner_tid的线程栈与服务依赖，比较显示CPU和非CPU耗时。不重刷UFS，初始30分钟，首帧后保留30分钟，总上限60分钟，无需守屏。

风险与未完成：采样时锁可能已改变，寄存器只反映最近一次内核入口，不能将running线程栈视为精确实时PC；2880MiB/单核/软件显示仍维持现状，桌面、全6GB、SMP、A530和物理触摸尚未通过。阶段四仍在进行。

本轮指令检查 46 项、实际包检查 16 项。

镜像 SHA256：`013cdeb8b2ba6c1e72a51f8a41df539c3b28d95838e1f1ad567183cdacaa822d`

内核 SHA256：`40c90b07a0bdf79810fc6cad47f24f2c5cf13259e9d78bbe96817f45c528146e`
