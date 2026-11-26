# iOS16 R9 真机结果：启动期控制台 panic

2026-11-17 17:52:18 +08:00 临时启动一次，Fastboot 接受，无主机 flash。冻结镜像 SHA256 `9a5736f0c1557b9b17a4cb8c8dfd52d2a5601e2a180f218225f50957ca027db8`；冻结内核 SHA256 `d3c047c8433855d235c505ad98fa15c12d4408921b287d16dfad9c7fb57a6587`。

用户原图明确显示 XNU panic 后已返回 UEFI 保留摘要，RAW BYTES 34293。原图校验值 `c43688736e4c64049be3ee3dad179f06833de2f93a10c75665d6d2638286abce`，原图及局部转录保存在忽略的本地目录，不加入 Git。不能继续解释为加载慢或等待不足。

`panic(cpu 1 caller 0xfffffff0073e5120): Console I/O from interrupt-disabled context @serial_console.c:168`。任务为 kernel_task / PID 0 / 35 threads。原始地址映射到冻结内核的 `__cnputs`；可见回溯为 debugger/exception 帧，尚未覆盖完整违规调用链。

确认的代码缺陷：R9 在 `GenericARMCPU::initCPU` 内增加 `IOLog`，该回调明确以关中断状态执行；实际完整 XNU 的 `console_io_allowed` 在 early boot 后禁止此类控制台输出。这一缺陷与照片保护条件吻合，但仅凭局部回溯不宣称已恢复完整调用链或排除其他 SMP 问题。

USB 两次接收尝试均未见候选枚举，0 帧 / 0 正文字节。延长接收器在照片证实终态后由主机定向停止；保留停止前原始状态，独立记录主机停止原因，不伪造自然完成。完整 USB 采集、SMP、launchd/服务和桌面本轮均未通过。CPU 1 报 panic 不等于三核全部在线，CNTFRQ 不是 CPU MHz。

R9 新增的 UFS 固态 ioctl、数据库/IPC 有界诊断及日志开销调整仍保留为待运行验证的候选。下一修订只修复初始化日志上下文：在每核固定记录中保存信息，再由允许打印的线程输出；不放开关中断打印，不以关闭多核掩盖缺陷。UFS、2880MiB、现有 userdata 不变；不重试已消费 R9。
