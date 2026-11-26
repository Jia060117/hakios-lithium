# R40 ui03 / ufs02

保留 614.4 MHz 配置档、R20 显示和真实触摸。上一轮用户确认明显流畅改善，并明确手动回 Fastboot。本次修正准入、driver同步及手势就绪诊断，未提高频率。

实际完整 HakiOS 内核、loader/Mu 编译和镜像逐层读回通过；真实有序退出与自动 Fastboot 尚待上机。

镜像 `0927c9d86fb1093fc394cb0c6666a38f281f7c62582458117a34242bda941dd1`，kernel `252e58da2f63fb38da5e0b0fe5bc9c681c838f8f3311a9685f59ec64c1963afb`。

```text
bootcpu=1 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=600 lithium_sb_watch=1 epoch=1794940756 touch=1 touch_log=1 touch_poll=1 applog_ui_quiet=1 ui_lite=1 touch_smp=1 cpu_perf_fixed=1 cpu_perf_ufs=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 cpu_inherited_supply_r39=1 ufs_return=1 return_fastboot=1 return_fastboot_delay=10 lithium_log_retry=1 ui_cycle=1 ui_cycle_ready=1
```
