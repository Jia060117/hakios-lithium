# 第四轮：内存初始化推进与屏幕VA保留冲突

2026-11-13，真机临时启动，内核ae93c92257f7b1143c547806769f1e93399f66675e99953266222d7b3c8a3e85；镜像91d15900094a461a1f2438daadf595061bc7e24fe11972801acedd732d1e7dcd。

本轮修正第三轮诊断：切换TTBR1前使用启动线性PA/VA转换，并用AT检查后才读取L2；切换后保留TLBI前后的AT/PAR对照，再测试像素和迁移控制台。电脑反汇编还发现Mach-O汇编器把分号视为注释，旧的多指令字符串只有AT实际生成，ISB/MRS未生成；现改用换行，并新增实际二进制检查。一个有此缺陷的电脑构建7c2a3adc未上机，只有修正后的ae93c922进行了第四轮启动。

验证记录：ios14-native-alias-validation.json，三个AT后均有ISB和MRS PAR_EL1；DSB/TLBI/DSB/ISB位于最终别名探测之前。完整扫描1619730条可执行指令，没有禁止的LSE/PAC指令。扫描是输入约束，不是硬件运行证明。本轮没有运行QEMU。

实机照片保存于reports/local/20261113-141240-066430-native-xnu/native-xnu-zone-panic.jpg，哈希见同目录result.json。屏幕循环换行使顶部BUILD标签不可见；内核构建时间14:11:29、新增诊断顺序与冻结产物对应。

- VM table PAR：0xff00000101dc4b00。
- VM screen L2：0x0060000082000605。
- TLBI前后screen PAR：均为0x4400000083401b00，F位为0，PA为0x83401000。
- VM24、09：像素写入及最终显示别名已执行。
- 11、13、15：TTBR0撤销、pmap_bootstrap返回、arm_vm_init返回。
- 16、19：BGR24串口输出接口和中断平台初始化返回。
- vm_page_bootstrap：30725个free pages、2043个wired pages；之后输出zone submap、metadata和zone leak detection disabled。

PANIC为pmap_ttne的Attempt to demote L2 block，target_level=3，addr=0xfffffff0038ec000，tte=0x0060000082000605。panic caller=0xfffffff0072f1858，对应冻结反汇编中0xfffffff0072f1854的bl _panic的返回地址；不是新的数据访问异常PC/FAR。

故障VA位于[0xfffffff002000000,0xfffffff004000000)屏幕L2映射，TTE与屏幕读数一致。osfmk/arm/pmap.c的非KTRR pmap_virtual_region只保留low globals的首个32MiB；osfmk/vm/vm_kern.c的kmem_init据此保留kernel_map区域。相邻的屏幕块未登记，因此zone分配进入该区后向pmap请求L3，触发已有的块映射保护断言。

修复0016扩展现有low globals保留区至64MiB，同时断言页面配置和内核镜像边界。这只是VA保留，不占用额外64MiB物理RAM，也不刷写手机分区。下一轮须看到保留区日志及超过本次zone停点的真实输出，才能确认修复有效。

TLBI前后PAR相同，不证明第二轮停机根因是旧TLB。第四轮已解决可见症状、推进至新阶段；timer中断、调度稳定、BSD和iOS用户空间仍未验证。
