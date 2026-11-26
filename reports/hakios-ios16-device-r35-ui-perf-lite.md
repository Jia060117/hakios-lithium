# R35 lite 真机测试（运行中）

2026-11-20，Asia/Shanghai。用户在获知lite会保存减少透明／动态偏好后要求“试试看？”。本轮仅执行一次R35 lite冻结镜像的Fastboot临时启动；正常UFS Data文件系统写入及该偏好设置已说明。

Fastboot于 **18:07:16.936822+08:00** 接受本次启动。启动前五项只读身份、product、解锁、电量和download-size预检通过，随后USB日志接收器成功枚举。累计HakiOS临时启动61次、iOS16为30次。

镜像：`artifacts/hakios/lithium-ios16/phase4-r35-ui-perf/lite/lithium-hakios-ios16-r35-ui-perf-lite.img`

SHA-256：`fda8fd7a18b8f0468c7be483e3080a30a2b9aee97d862fefc077f4c99eced9d3`

内核SHA-256：`86f36fdf4e047e0f2da18138a39629e584975e4c6689036946ae71195bd924aa`

## 实际参数与对照范围

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1 touch=1 touch_log=1 touch_poll=1 applog_ui_quiet=1 ui_lite=1
```

仍为单核phys0x1目标；未启动三核诊断候选，未写频率、电压或缓存配置。相比R34，本轮同时加入UIquiet和现有UIlite，优先验收交互改善，不能把结果单独归因于其中一个变量。10800秒为原有timer上限；交互检查无需等满。

原始USB记录：`reports/local/20261120-r35-ui-perf-lite-approved/`。最初的保存流已到APFS系统根/cryptex挂载；实际CPU缓存观测、ui_lite执行、主屏及响应效果继续从本次运行核对。

18:09:28+08:00的运行快照已确认：

- actual logical0、MPIDR0x80000001、affinity1、bootcpu1/cpus1，logical_match1。
- MIDR0x512f2011，SCTLR0x34d4593d，I/C/M均为1。只证明本快核在该预启动观测点开启对应功能，不是慢核或实时MHz的测量。
- 整数平均2472us、memcpy平均3485MB/s，与R34同快核接近。
- `hakios: SpringBoard: ui_lite=1: Reduce Transparency and Reduce Motion on` 已输出，表示现有设置路径已执行；代码未检查CFPreferencesSynchronize结果，跨启动持久化和视觉收益尚未确认。
- SpringBoard `main_queue_enter elapsed_ms=71445`，相对其constructor开始计时，不是从Fastboot接受开始的总启动耗时。
- 触摸ID0xe7、至少4200份成功报告，未见触摸read_error/report_error。
- 已保存1932帧/446148控制台字节的连续前缀独立重放一致，无缺口、重复、残帧或device-loss；接收器仍运行，尚无最终timer/drain。

用户已被提示对比桌面翻页、后台卡片、打开应用再返回桌面的停顿。没有收到本轮可见效果反馈前，不宣称性能改善通过。R35 lite同时启用两个减负路径，因此即使改善，也不能单独归因于quiet或lite其中一个。

## 边界

本轮六条Fastboot命令为五项只读预检及一次临时boot；无刷分区/擦除、ADB、Android/Recovery、QEMU、额外设备查询或重试。UFS自动Fastboot仍未启用。R20/R32/R33/R34/R35冻结字节保留。

本文件是运行记录，不是UI改善或完整timer/drain验收结论。保持同次手机运行与USB接收，后续根据主屏翻页、后台卡片、点应用返回桌面等实际反馈更新。
