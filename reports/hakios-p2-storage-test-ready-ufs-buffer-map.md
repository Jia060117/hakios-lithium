# HakiOS P2：UFS文件读取映射修正包

R18真机已确认HFSX只读挂载成功，日志初始化修复生效。随后kernel data abort的PC落在memmove目标写入，LR精确对应ufs_request读取拷贝；普通ufs_strategy直接使用buf_dataptr，同一个完整上游驱动的数据卷策略已使用buf_map/unmap。

修正只针对LITHIUM_ARMV8_0：沿用该上游接口为普通块设备cluster读目标建立内核映射，读取后释放映射；读取失败同样释放，映射失败不提交读取。继续使用完整上游UFS、VFS和HFS，没有替代通用服务，也没有改变写入授权。

原/修正实际ufs_strategy主机测试复现未映射目标差异，11项映射、错误、边界、只读及数据路由检查通过，ASan/UBSan通过。按正常HakiOS同步驱动、依赖、构建和打包流程生成新内核。最终冻结二进制中的映射/释放调用从1组增至2组，普通读取的map→read→unmap顺序已核验；内核驱动源与依赖记录逐字节一致。

冻结镜像SHA256：`bf23f302f1e1e6a13582fd672d3f23b0b133daf169e55871890058f11493afee`。133项证据、ARMv8检查、Android/gzip/FAT/XNUP独立解包见[准备报告](hakios-p2-storage-test-ready-ufs-buffer-map.json)。R18原图、旧冻结包和真实卷日志修复证据保留，现有卷仍按原卷加两个已核验日志块的契约使用；本次准备没有分区写入。

下一轮只验修正后的文件读取闭环：HEADER及三个18H17文件match1、MOUNT readonly1、UNMOUNT rc0、RESULT PASS及正常返回。约120秒加25秒加载停留，摘要持续保留；P1不重测。文件校验即使通过，也不代表已执行iOS14 dyld/launchd、验证系统兼容或图形。累计HakiOS启动仍为18次，准备没有新boot或旧7195/QEMU执行。
