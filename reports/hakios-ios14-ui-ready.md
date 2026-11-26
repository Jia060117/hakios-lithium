R31已修正完整HakiOS中原始XML权限读取的接口集成，并在lithium_ui启动前复用完整persona子系统创建type6身份。原始18H17 SpringBoard签名包含com.apple.springboard-ui.client，XML特殊槽哈希已核验；原始SecTask使用csops op7/ERANGE读取。不修改Apple程序或共享缓存，不绕过断言，不将权限查询改为全部允许。完整HakiOS重新编译，三个实际对象文件确认新增调用已接入。17项实际适配函数和保留摘要的ASan/UBSan检查通过；实际APFS读回确认335项plist0644、目录和程序0755、uid/gid0，R30根卷文件内容保持。同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。镜像SHA256 `2af949c5c1a8d958720b28e2e3f6b9a54c9e60cd99c83b43e0116652d33d7efd`。本次准备未查询或启动手机、未写分区，累计仍30次；R31权限、身份和UI运行结果待真机。结束摘要新增LITHIUM IDENTITY FINAL，保留SpringBoard实际XML探测/复制及persona查询结果。

冻结镜像：artifacts/hakios/ios14-ui-ready/20261116-044722-701044/lithium-hakios-test.img

检查设备：`python scripts/boot-hakios-ui.py`；已有授权下执行一次临时启动：`python scripts/boot-hakios-ui.py --boot`。
