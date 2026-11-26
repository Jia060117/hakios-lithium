# 第三轮实机 PANIC 定位

记录日期：2026-11-13。目标目前暂停；本次仅接收、定位和保存用户照片，没有编译新镜像或再次操作手机。

照片 BUILD 6088e623eb9eb58d 与本轮冻结内核 SHA256 一致。最后 VM 标记是 VM21 physmap ready; switching TTBR1，随后输出 VM final TTBR1=0x0000000101dc0000、VM screen VA=0xfffffff003401000、VM screen L1=0x0000000101dc4003；没有 L2、VM22、PAR 或 VM23/24 输出。

异常：Unexpected fault in kernel static region。PC=0xfffffff007306c34，FAR=0xfffffff027dc4008，ESR=0x96000006，x23=0xfffffff027dc4000。ESR 为同 EL 的数据读取异常，DFSC=6；固定源码 osfmk/arm64/proc_reg.h 定义为 L2 translation fault。

本轮内核反汇编（instruction-check/00.txt，内核哈希已与冻结输入核对）：

```asm
fffffff007306c18: and x0, x22, #0xfffffffff000
fffffff007306c1c: bl _phystokv
fffffff007306c20: mov x23, x0
fffffff007306c30: bl _lithium_early_hex
fffffff007306c34: ldr x1, [x23, #0x8]
fffffff007306c40: bl _lithium_early_hex
fffffff007306c4c: bl _set_mmu_ttb_alternate
```

故障指令是 0015 新增的 L2 描述符读取；x23+8 与 FAR 完全一致。实际隔离源码 arm_vm_init.c 的 phystokv 优先查询 ptov_table；arm_vm_physmap_init 返回前已填入最终 physmap 的转换表。诊断发生在 set_mmu_ttb_alternate 之前，因此拿到的最终虚拟地址尚未映射到当前启动页表，读取失败。这轮诊断自身引入提前 PANIC，不能据此判定第二轮 VM22 后的屏幕别名根因已找到。

恢复后先修正诊断中的阶段相关 PA/VA 转换：切换前通过仍有效的启动映射读取页表，或把 L2 读数移到 physmap 初始化前。再继续获取最终别名 AT/PAR 和像素写入的真实结果。第三轮未完成 arm_vm_init；第二轮已经到达 VM22 的证据仍保留独立，不混算。

照片与输入冻结目录：reports/local/20261113-013750-178795-native-xnu/。照片 SHA256 和结构化观测见 ios14-native-device.json、ios14-native-history.json、该目录 result.json。手机恢复状态尚无新确认。
