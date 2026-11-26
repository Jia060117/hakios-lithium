# 原始iOS14界面联合启动准备

按用户2026-11-16要求，服务请求、显示、物理触摸和SpringBoard联合推进并尽量同包观察。现有阶段继续分别记录真实证据，不再逐个小项安排独立真机轮次。P1、R20存储、R24工具和R28进程存在性已通过，不重复上机。

已执行的准备：

- 沿固定HakiOS的mkjobs.py生成18项主要任务及315项按需任务，包含原始backboardd、SpringBoard及其基础服务。按需任务保留上游的硬件排除和触发限制策略；没有把333项全部强制RunAtLoad。原14根卷缺少cfprefsd.system、containermanagerd.system和mobile.usermanagerd三个上游默认配置，未伪造替代。
- 直接使用上游未修改的svc_test.c和surf_test.c，编译两个arm64/iOS14.8目标程序，分别作为独立launchd诊断任务，不阻挡UI启动。实际Mach-O最低版本已核对。LLVM链接使用独立SDK stub副本，仅规范CRLF行尾并链接原始头文件；原SDK及Apple程序不变。旧ios_version_min选项有警告，实际版本由明确platform_version设置并核验。
- 定位MIX BGR24与上游framebuffer仅接受32位的入口差异，生成BGRA32表面到BGR24面板的有界补丁。保留完整上游IOSurface、缩放、混色、队列和用户客户端设计；修正绘制、清屏和调试blit的每像素字节数。实际修改函数的18项主机用例通过ASAN/UBSAN，覆盖未缩放、缩放、滤波、裁剪、行填充、周围哨兵和非法blit输入。补丁可应用于固定源码。
- 从原始18H17共享缓存提取图形/HID/SpringBoard符号，按缓存真实的v2重定位信息解码虚表。确认六个图形虚表入口及SpringBoard启动方法；反汇编确认激活状态位于对象+8，并核对HID回调槽的布局。已生成并编译两个arm64/iOS14.8适配库；原始Apple程序未修改。图形包装器不再读取16.5的C++对象字段，两个构造器都核对18H17缓存UUID后才启用挂钩；未核对的16.5墙纸、状态栏、GPU/着色器及容器persona挂钩不执行。库已放入联合草案并配置沿用上游kern.exec_inject路径。编译仍有上游未调用辅助函数的四处Class/id类型警告，不能称无警告构建。

这是一份集成准备草案，**尚未生成可启动的新镜像**。BGR24补丁尚未激活到完整HakiOS内核，主机检查不代表真机显示通过；两个诊断只编译，尚未在手机执行。

继续落实的工作：

1. 图形库的六个虚表及SpringBoard启动挂钩已完成原始14地址核对并编译，仍需核对IOMobileFramebuffer/IOSurface用户客户端选择器与交换结构，以及在集成启动中验证实际行为。复用上游CoreAnimation软件绘制作为首次UI候选，运行兼容尚未确认；A530硬件加速同步推进，不作为第一次尝试界面的独立验收前提。
2. 将上述显示补丁、联合任务和诊断/进度摘要接入原HFSX交接路径，按正常流程构建完整HakiOS并独立解包、冻结实际测试包。不能用草案直接boot，也不再运行R28单次入口。
3. FT5x46硬件采样仍缺MSM8996 I2C接入；原厂DTS已定位I2C12、0x075ba000/0x38、GPIO87/88、reset89及1080×2040坐标。DTS通用interrupts写105，厂商irq-gpio写125，两者需结合原厂驱动核对，不盲用另一机型。沿上游/dev/hakios-touch及HID集成推进；首次UI尝试允许如实报告物理触摸未接通，不以它先独立通过为前提。合成手势不能算物理触摸成功。

同次启动中分别保留服务回复、显示是否持续更新、SpringBoard状态、真实触摸事件及首个故障。启动预算按集成结果和实际进展设置，不自动沿用180秒；不要求守屏、秒表或中途拍照。成功界面保留供操作，失败/结束摘要保留供查看。

证据边界：svc_test的通知注册/状态往返可检查真实通知接口；其CFPreferences同进程读回可能命中客户端缓存，不能单独证明跨进程cfprefsd回复；os_log不崩溃不能证明logd收到了日志。surf_test证明的目标是表面共享，不能替代可见桌面或物理触摸。

本轮没有设备查询、启动、分区写入、旧7195执行或QEMU。累计仍28次HakiOS临时启动；199项R28冻结证据保持。系统仍在UFS按需读取，现有完整iOS14目标及固定HakiOS内核基线保持。手机最后状态依据R28通过后的结束照片。

[机器可读清单与哈希](hakios-ios14-ui-integration-preparation.json) · [显示主机检查](hakios-bgr24-host-checks.json) · [最后真机结果](hakios-device-r28.md)
