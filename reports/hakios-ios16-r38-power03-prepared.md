# R38 power03：修复供电采集的MMIO撤销契约

2026-11-20（Asia/Shanghai）。修正版已实际构建、冻结并逐层读回，**尚未上机**。当前手机仍按用户照片停留在旧包panic摘要；本阶段没有设备查询或启动。

实际故障是上一包新增供电采集的映射释放，不是PLL配置失败。修复包括：按真实PAGE_SIZE归一化物理allocationbase，完整round extent，独立保存寄存器dataoffset；使用`ml_io_map_unmappable`从kernel_map创建可释放映射；使用原allocation与完整extent调用`ml_io_unmap`。未删/放宽pmap断言，没有改全局页表策略。

实现：`cpu-mmio-window-r38.h`和`cpu-power-r38.inc`。Hume专项核对发现第二项API配对问题，主代理独立读取machine_routines.c:1649/1668/1682–1683和arm/io_map.c:95–99/120–132确认后修正。第一中间包power02保留为superseded，不启动。最终包只有power03/frequency07。

真实helper经Clang ASan/UBSan4组测试通过，主代理独立复跑；10个边界、10个清理场景、8个无效输入及原错误负控，覆盖4K/16K页、offset2000/f000、跨页、overflow/NULL、zero-map不unmap、邻接分配完整。此测试是helper及mock清理契约，不冒充XNU pmap运行或上机修复通过。实际完整HakiOS增量link和实际包读回通过，未重复无关总回归。

最终镜像SHA256 `188029554650ebb903e4be98ad798a2f02c6ab9c8644d3a784de872a6cb745b4`；内核SHA256 `1dcfa5d72f478161f3c0e8647e62b269eeaf421701432fca27c9cf95f3b3403a`。

完整参数保持上一包一致：

```text
bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 cpu_clock_diag=1 cpu_power_diag=1 cpu_freq_pmu_setup=0 cpu_sanity=1 cpu_freq_diag=1
```

本包只读APCC/供电/fuse及旧性能基准，PMUsetup0，不连接固定PLLcore，不写时钟/电压，不访问物理UFS。正常timer结束后走已经验证的RAM自动Fastboot流程；panic仍保留摘要，不能承诺异常强制复位。

主机命令：

```text
python3 -B scripts/tests/test-r38-mmio-unmap.py
python3 -B scripts/build-hakios-cpu-r38.py --revision clock-power03
python3 -B scripts/package-hakios-cpu-r38-clock.py --power-fix
```

构建使用既有14GiB/12CPU的systemd任务，实际日志见`evidence/cpu-r38-20261120/main/kernel-clock-power03.log`。新源的可回退补丁与旧源备份见`evidence/cpu-r38-20261120/page-unmap-fix/`。原索引与R20及旧冻结保留，69/38计数不变。

用户已授权的下一步只需其手动Fastboot就绪后单次启动本包，无需重新审批。频率恢复和四核调度目标仍未完成，不以此次采集修正代替目标验收。
