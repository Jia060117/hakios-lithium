# HakiOS P2：现有 HFSX 日志初始化修复

用户明确要求修复、继续推进，并允许按需要调整整个 userdata。先处理现有卷的具体缺陷：不需要重新分区来验证本次日志问题。

R17 真机返回挂载 EINVAL。完整读回镜像的 HFSX 版本为 5，内部日志 JIB flags=5（InFS + NeedInit），8MiB 日志全为零。实际集成的 hfs-627.100.6 源码在只读非根挂载时先执行 journal_is_clean；零日志头 magic 检查返回 EINVAL，在 journal_create/清 NeedInit 前结束。这是已复现的静态原因候选，R17 照片并未直接捕获内部日志分支。

从完整 HakiOS 的实际集成源抽取 journal_create、size_up_tbuffer、journal_is_clean、checksum 和字节序函数；主机仅适配内存、锁及 IO。上游生成的标准空日志头通过正常检查，零头、校验损坏、脏事务及不匹配块大小被拒绝。7 项检查通过。真实维护程序的 11 项主机 IO 检查通过，含挂载/身份/元数据/非空事务拒绝、重复执行与同步失败保护；ASan/UBSan 通过。不使用 QEMU。

首次维护启动的挂载保护将 devpts/FunctionFS 的挂载点误识别为磁盘来源，在任何写入前拒绝；已修正为只核对每行的挂载来源，并加入真机特殊文件系统布局用例。该次失败日志及只读状态独立保留。

维护包固定原厂 lithium Linux 内核，独立 RAM init，无 userdata 挂载或 Android 服务；root 一次执行固定写入。它仅用于修卷，不作为活动内核或 HakiOS 通过证据。

运行前重新核验 Fastboot 身份、解锁、容量及电池；设备内再次核对真实 sda15 几何、4096 字节块、前8192字节卷头、JIB 完整原字节及整个8MiB日志为空。写入顺序为日志头 offset532480 / 4096字节，fsync并读回后才清 JIB offset528384 的 NeedInit 位（写4096字节块）。其余元数据保持原字节。最终保护整个sda及sda15为只读，独立USB读回核验，再回Fastboot。

HakiOS 复测使用 R17 同一完整冻结内核/镜像。前8192字节未改，不改现有卷头期望值。约120秒，摘要保留；挂载与三个18H17文件匹配、卸载和 RESULT PASS 仍须真机验证。不会重测已完成的P1，也不在本轮集成438草案。输入、哈希与检查见 [准备报告](hakios-journal-maintenance-ready.json)。
