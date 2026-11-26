# 第十轮 HFS 只读根卷日志问题

2026-11-14。第十轮内核 `efd37ae450521734f4746da2420adef7f05ff76fae725fe518644efb1f6071eb`。
两张照片与哈希保存在 `reports/local/20261114-150036-826563-native-xnu/result.json`。

MD5/UUID异常已越过。屏幕显示 `BSD root: ufsroot0`，随后创建日志的写操作返回
`0x1e`（EROFS），HFS返回22、根挂载返回19；后续照片为同一失败的多次重试。
没有观察到新panic，也没有根挂载、launchd或iOS用户空间成功证据。

## 根因和实测卷状态

- 已安装镜像的首4096字节CRC32仍为 `0x6143e5cf`，SHA256仍为
  `396e0b17d99eae20abda540f35834f95cd2857c858cc1a959f709cc7194ef6b3`。
- 卷属性 `0x80002100`：journaled、干净卸载；不含两种inconsistent标志。
- `lastMountedVersion=0x31302e30`（10.0），因此进入HFS的后期日志初始化。
- JIB位于allocation block129；flags=`0x5`（InFS + NeedInit）。
- 日志位于block130，offset=`0x82000`，size=`0x800000`；读到的首4096字节全零。
- 主机以只读映射，通过既有libdmg实际查询catalog：`.journal_info_block`
  大小4096、extent129/1；`.journal`大小8388608、extent130/2048，均与JIB一致。
- 手机UFS的 `DKIOCISWRITABLE` 返回0；写接口/strategy持续返回EROFS。

Apple [TN1150](https://developer.apple.com/library/archive/technotes/tn/tn1150.html)
明确规定NeedInit表示日志头未初始化，此时不存在有效事务。mkfs的 `-J` 创建了这个状态；
主机离线导入文件没有首次以可写方式挂载该卷，原先只读fsck通过并不会初始化它。

固定hfs-556.100.11的 `hfs_early_journal_init` 和 `hfs_late_journal_init`
在普通只读挂载时检查空日志，但明确排除了MNT_ROOTFS。根卷因此走到
`journal_create`，和当前真正不可写的UFS后端冲突。

## 修复范围

补丁 `ports/lithium/patches/hfs-556/0001-lithium-readonly-root-journal.patch`
只在LITHIUM、HFS_READ_ONLY、MNT_ROOTFS且介质不可写时生效：

1. 核对卷属性、内部日志标志、块大小、容量和日志/JIB边界。
2. 后期路径还要求catalog和JIB的文件位置、extent长度及逻辑大小完全一致；
   缺失、读取错误、移动或大小改变直接失败，禁止进入原有修复/禁用日志回退。
3. 干净、无不一致标志的NeedInit日志保持未初始化；没有有效事务需要回放。
4. 已初始化日志继续调用Apple的 `journal_is_clean`，验证实际校验和及start/end。
5. 早期日志验证失败直接退出挂载，禁止原有“根卷留给后续fsck修复”的回退。

不初始化/回放日志，不设置活动journal，不修改卷头或JIB，不启用UFS写入。
可写介质根卷的原有路径保持有效。本轮根镜像内容、CRC、安装记录未改变。

HFS补丁在临时目录应用，再同步最终文件到独立构建树；参考checkout不修改。
构建报告记录原始/最终源码哈希及补丁哈希，临时启动冻结HFS补丁与验证报告。

## 验证

`reports/ios14-native-hfs-readonly-validation.json` 记录51项测试：
实际早期/后期初始化函数和 `journal_is_clean`、checksum在主机I/O/catalog shim下运行，
启用ASan与UBSan。覆盖新日志、干净/脏日志、两种字节序、错误校验和、无效头、
读取失败、越界、重叠、外部/未知标志及catalog不匹配；只读分支断言零写入、
零create/open且卷头/JIB字节不变。单独确认可写介质仍进入原有create路径。

新内核SHA256 `c4b03899cba1fe486e7e27770ffcd302a40c4ebe2c5e09797ff571439c54f8c1`。
静态反汇编确认早期和后期两处调用新检查函数。主机测试验证控制流和数据检查，
不代替手机上的根挂载验收。下一轮需观察新的只读日志提示、`vfs_mountroot succeeded`、
`root vnode installed`、`load_init_program`或实际错误。

## 第十一轮实机结果

两张用户照片已确认新增的“clean fresh journal; initialization deferred”提示，
HFS挂载iOS14Root、`vfs_mountroot succeeded`及`root vnode installed`均可见。
只读根卷日志修复已通过这次实机验收。后续异常位于SHA256_Init的空provider读取，
详情见 `reports/ios14-native-panic-c4b03899.md`；尚未证明launchd启动。
