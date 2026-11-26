# iOS16 R7 诊断采集核对

本报告核对本次运行的采集限制，不将诊断缺口记为系统服务故障。

1. loader 命令行容量608字节（含NUL），本轮含loader前缀的请求共631字节，实际为607字节精确截断前缀。末尾 ustacks_tname 和 ustacks_top 被截断，top CPU榜未开启；相关进程、间隔、线程上限与有限次数完整，多线程采样有效。下一修订须在打包前计算完整参数并拒绝截断。
2. 固定XNU的 console_printbuf 默认只有256字节，单行超过255字符时会丢弃尾部，包括换行；只有 drain_uart_sync=1 会中途排出缓冲。本轮未开该上游选项，因此长取栈行在进入USB封包前已经截短。USB零缺口只证明传输流连续，不证明每条原始printf完整。主机现按记录标记和字符偏移拆分粘连行，排除不完整末帧，不跨线程借用owner字段；合成边界及原始R7的17线程/精确owner配对回归通过。
3. upstream ustacks 用 proc_name_address 选择进程，该接口返回最多16字符的p_comm。containermanagerd为17字符，运行短名为containermanager；R7按完整17字符比较会漏选它。服务已经有启动记录，缺少其快照不能说明服务缺失。下一修订复用既有长名getter或精确短名别名。

第二轮采样可关联 runningboardd 的等待线程0xfe与owner线程0xff，等待参数和当前锁值一致、owner解析成功。原20F66缓存映射显示等待涉及libsystem_malloc，owner当时为可运行线程，栈涉及CoreFoundation/RunningBoard；这仍只是一次竞争候选，尚未证明持续持锁、死锁或SpringBoard根因。

R7同次采集已正常定时结束；三次有效画行计时及最终USB传输审计见R7最终报告。桌面、全6GB、SMP、A530和物理触摸均未通过。本次只读源码和本地日志，未构建新镜像、未重启或刷写手机。
