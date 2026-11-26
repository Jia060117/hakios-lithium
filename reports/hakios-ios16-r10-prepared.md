# iOS16 R10 准备完成

R9 已明确 panic 返回 UEFI，完整记录见 `reports/hakios-ios16-device-r9.md`。原输入和报告归档于 `archive/ios16-r9-20261117`（`2112b31`），R9 冻结包及已消费入口保持。

R10 将 `GenericARMCPU::initCPU` 中新增的同步 `IOLog` 改为四槽固定记录：原子发布后保持不可变，无输出、分配、等待或中断状态修改。平台启动后由普通 thread_call 在 30 秒后输出，覆盖上游默认 28 秒延迟启动的核心；输出端也拒绝关中断上下文。保留原来的 console IRQ 保护及 CPU/GIC/调度器调用，不使用 nointr_consio，不关闭 SMP 掩盖问题。

执行实际 R9/R10 initCPU 的 ASan/UBSan 检查：原 R9 被 IRQ 控制台策略拒绝，R10 保留三个模拟核的调度器/信号调用且不打印；覆盖重复初始化、未完成发布和越界槽。主机夹具不证明真实 SMP、GIC 或缓存一致性。完整阶段四回归通过，完整 HakiOS 内核、真实 D22AP loader/Mu、413 项用户空间任务构建通过，46 项指令扫描和 16 项实际镜像检查通过。引导 APFS 的实际程序和目录权限读回通过。

冻结镜像 SHA256 `a6c6d967c52c9eb9a41b12ad0d1c7cf6bee208c1bf2debc90ee4739e5618e2f3`。使用 `bootcpu=2 cpus=3`，2880MiB，UFS 系统和可写 Data；不重刷 userdata。UEFI 驻留一核提供 UFS 服务，所以当前最多三核供 XNU 使用。R8 已在 MPIDR0x100 运行，未测 CPU MHz，不能推断此次才切到大核或已提频。

下一步为同一 R10 包临时启动一次，观察越过 panic、实际每核记录、数据库 ready/容器/偏好设置诊断及图形进展。SMP、服务根因、真实桌面和全部6GB仍待真机。
