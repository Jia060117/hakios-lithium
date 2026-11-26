# 18H17共享缓存调用参数适配草案

针对原始18H17 dyld和三映射cache选择syscall 438、而8796该项为nosys的已定位差异，已准备一项有界参数适配。syscall签名恢复旧六参数契约，参数中的公开映射结构仍由8796原有shared_region.h提供。适配程序保留dyld已选定的地址、slide及slide-info，交给完整HakiOS当前_shared_region_map_and_slide；文件/root/签名检查、映射、pager、重定位和清理继续由上游实现负责，没有恢复旧7195内核或搬回其整套共享缓存实现。

草案限定本机编译宏LITHIUM_ARMV8_0，保留旧ABI完整1024映射上限。实际8796公开结构被用于主机编译，7项ASAN/UBSAN检查覆盖三映射字段、完整映射数上限、空/过大/溢出输入、部分copyin失败、分配失败、上游相同或冲突slide检查、底层拒绝透传及分配释放。主机调用被替身拦截，不是XNU运行或dyld兼容验证。

补丁对当前完整HakiOS XNU树git apply --check通过，活动树和内核哈希保持原样。草案位于reports/local/ios14-shared-region-438-draft；证据和源哈希见[JSON](hakios-ios14-shared-region-438-draft.json)。当前尚未集成到正常构建入口、编译新内核或打包启动，也不覆盖R17冻结包。

R17存储结束摘要仍待确认。存储结果明确后再按HakiOS正常构建流程接入此有界兼容补丁及完整上游init的HFSX布局适配，执行原始18H17动态程序；SpringBoard、GPU和触摸仍未验证。此次没有新boot、分区写入或QEMU执行。
