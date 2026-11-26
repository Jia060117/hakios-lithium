# HakiOS lithium 首轮真机测试

2026-11-14 20:06:51（北京时间）执行一次 HakiOS 临时启动。设备身份、解锁状态和 userdata 长度 250710192128 字节通过预检；Fastboot 的发送及 Booting 均返回 OKAY。用户随后报告：**“一boot直接就重启了，进入miui的启动画面了”**。本轮未通过 HakiOS 平台启动验收，实际故障阶段尚不明确。

唯一内核仍为完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU `8796.121.2`。本轮镜像 SHA256 为 `b051e96c41443e119d73efc54fb4e9a2380cd3c0ca7a3047318441c11e4e1768`；内核为 `02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`。完整包、内核/加载器/固件/hello 哈希、命令和来源快照索引见 [机器记录](hakios-device-r1.json)。冻结输入位于 `artifacts/hakios/device-tests/20261114-200651/`，后续构建不得覆盖它们。

| 阶段 | 本轮证据 |
| --- | --- |
| Fastboot 接受临时镜像 | 同次命令返回码 0，发送和 Booting 均 OKAY |
| UEFI / BDS | 未取得屏幕或运行日志证明 |
| xnuloader | 未取得运行证明 |
| HakiOS 内核 / hello | 未取得运行证明 |
| 自动重启 / MIUI 启动画面 | 用户文字报告；Android 启动完成未核验 |
| 后续 USB 状态 | 原授权设备重新出现在 Fastboot，ADB 未连接；回到 Fastboot 的具体方式未知 |

本轮仅执行 `fastboot boot`，没有分区刷写、清除、格式化或重分区。现有 userdata 仍按已安装记录为 iOS HFSX。没有执行旧7195内核或 QEMU。MIUI 启动画面不证明 Android 已完整启动，也不证明已进入 HakiOS。

当前复核确认 XNUP 必须先复制到高 DDR，再复制 FD：FD 的目标窗口确实与尚未搬移时的 XNUP 尾部源地址重叠。原首包已按这个顺序执行；不能将重叠本身写成已定位的故障。原首包保留了 LK 的异常向量，并直到 Mu 的 C 平台初始化才关闭 APSS 看门狗；这两项可能让早期故障表现为重启，但本轮没有 ESR/ELR 或屏幕停点证明具体原因。

下一轮保留同一完整 HakiOS 内核和上游 hello，在板级 BootShim 中提前关闭同一个 APSS 看门狗、设置早期异常停屏，并加入底部检查点：从左到右四个青色块分别表示入口、XNUP 复制完成、FD 复制完成、缓存交接完成；第五个红块表示早期异常。Linux Image 的声明保留长度同时与实测原厂 `0x2433000` 精确对齐。打包检查增加向量对齐、嵌入字节、复制长度及早期信息区边界核对。这些是诊断与加载契约修正，尚未证明解决本轮重启。

P1 的内核与用户程序闭环、真实 timer IRQ 和线程运行10分钟/连续3次均保持未通过；iOS14、GPU、触摸和 SpringBoard 仍未验收。
