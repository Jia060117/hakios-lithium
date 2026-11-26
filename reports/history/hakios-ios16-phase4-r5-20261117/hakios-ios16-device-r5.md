# iOS16 R5 最终观测与桌面停点排查

2026-11-17：R5 已定时返回 UEFI 保留摘要，仍未进入可确认的 SpringBoard 桌面。延长窗口确已生效，不能再把增加等待时间作为主要排查手段。系统后期仍启动服务并提交图形帧，没有整机死锁的明确证据；具体阻塞函数仍缺 SpringBoard 主线程栈。

本轮完整 HakiOS 固定基线为 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2，用户空间为用户提供的 16.5/20F66/D22AP/arm64。临时启动一次，主机未重复刷写 userdata，运行中允许 UFS Data 正常写入。镜像 SHA256 `4ad8e3db74baf802ed5b25d86c22027cc87ae0067664136ae644164790ba1ede`；内核 SHA256 `ad242b83929483cd38105c06e1a485a34984536a76e40b44990656670401c1f3`。已消费的 R5 入口不得重试。

## 等待窗口与返回原因

- 启动获接受：2026-11-16 23:24:16（UTC+8）；USB 终止：2026-11-17 00:13:10，主机记录跨度约 48 分 54 秒，含引导及采集时差。
- 最终控制台第 59 行确认初始 1800 秒、首帧后 1800 秒、独立上限 3600 秒。第 1183 行记录首次基础图层在内核计时 `1095537 ms`，约 18 分 15.5 秒；第 1570 行确认延长。预计截止点约 2895.5 秒，首帧后确实保留了 30 分钟。图层里程碑本身不证明桌面或物理屏幕内容。
- 用户照片可读 `RETURN: TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG`、`RAW BYTES 372414`、`LITHIUM SESSION ENDED`。这是定时返回证据；保留日志未见 panic 正文不能外推成绝对无故障。

## 当前可确认的停点

原始 launchd 已完成 early boot（第 935–936 行）。SpringBoard PID8 与 backboardd PID13 建立 System shell 连接（第 1260–1262 行），随后报告 `Bootstrap system shells ready`（第 1299 行）。但最后 BootUI 仍为 `No explicit screen owner`（第 1303 行）、`Current bootUI is an Apple Logo`（第 1309 行）。未见 SpringBoard 完成屏幕所有权交接、撤掉启动遮罩或提交桌面内容的证据。

不能说一直卡在 activity assertion：该请求第 1307 行已返回，耗时 199.837 秒，随后确实添加并绘制了白苹果遮罩。未完成的是屏幕接管/桌面交接，尚不能仅据此断言哪个服务死锁。

SpringBoard 注入库的初始化日志已出现（第 992–1012 行），激活与跳过设置助手 hook 均报告接入。但上游 `userland/src/inject_sb.c:2254` 安排的一秒主队列回调，其后续 `hakios: battery: SOC=` 输出本轮未出现。应优先定位 UIApplication 初始化、主队列调度或回调内同步调用；缺日志不能独立证明主循环从未进入。

上游已有 `touch_lat=1` 主循环/线程采样，R5 未开启；watcher 又要等上述主队列回调执行后才安装，并仅采样 25–2000ms 的活跃循环停顿。因此直接开启该参数可能仍漏掉早期初始化或长期等待，应复用现有取栈代码补充初始化期、低频有界的观测。

## 其他候选因素

| 项目 | 本轮证据与边界 |
| --- | --- |
| 整体进展 | 后期 launchd 仍启动到 PID50，内存日志与 swap4/5/6 继续。未见 SpringBoard/backboardd 反复退出重启、SIGILL/SIGABRT 或 panic 正文的明确证据。 |
| 显示性能 | 六次图层提交约 3.29–3.82 秒/次，包含调度，不能当成纯像素复制耗时或稳定帧率。swap3 到 swap4 的约26分钟间隔不代表一帧画了26分钟，可能期间未有新提交或正在等待。 |
| 启动依赖 | backboardd 自报 CoreAccessories 208.584 秒、HID 总计 176.146 秒、window server 150.951 秒、main 至 backlight ready 541.036 秒。内部计时不等于外部秒表核验，不能全部归因于老 CPU。 |
| HID | digitizer 等待及三次虚拟服务重建后，第 2185 行最终返回非空 touch screen/buttons service。不能说始终没有 HID；虚拟服务对象不证明物理 FT5x46 触摸通过。负 elapsed 值需要核对来源，尚非时钟故障定论。 |
| 内存 | 最后约 1750MB free、2646MB available、compressor 0，未见内存耗尽/jetsam 证据。实际仍为 2880MiB，完整 6GB 尚未实现。 |
| UFS/容器 | System/cryptex/Data 挂载、Data 写入读回和 45 项 init 检查通过。backboardd 已得到有效 shader cache 容器路径，未复现旧 iOS14 的 error51/nil path abort。完整容器 RPC 仍未全面验证。 |
| 其他错误 | type6 persona、蜂窝等错误存在，但其他服务仍继续启动；尚不能把任一条直接定为桌面根因。 |

## 上游集成差异

固定上游的 `xl:215` 自动补入 `epoch` 和默认 `sb_delay=8`，R5 自定义启动参数没有这两项。延迟发生在 SpringBoard 注入库构造阶段。上游注释主要解释应用注册顺序，并指出在另一较慢环境增加延迟反而损害触摸接纳；因此缺少8秒延迟不是已证明的根因，不能盲目加长延迟代替依赖就绪证据。epoch/墙钟与负 elapsed 的关系也须分别核对。

多核另有具体缺口：当前 `CONFIG_CPUS=1`、`cpus=1`；设备补丁 `0001-lithium-ios16-device.patch:1885` 的 `cores=4, core_shift=8` 会生成 0/0x100/0x200/0x300，原厂 DTS 实际核地址为 0/1/0x100/0x101。loader 还驻留一核处理 UFS bridge，须先对齐拓扑和驻留核合同，不能直接改四核。此缺口阻挡安全开放多核，但尚未证明是本轮 BootUI 停点的直接原因。

## 证据完整性

最终 USB 冻结快照为 529824 字节原始帧、309344 字节控制台、6890 帧；逐字节重解码一致，缺口/重复/不完整帧均为0。从 Darwin 横幅覆盖至原始 launchd 和图形启动；主机最终 `[Errno 32] Pipe error`，未捕获 UEFI 返回尾部，完整日志采集仍未通过。USB 错误本身不能证明 kernel panic。

原始日志与照片仅保存在忽略目录 `reports/local/20261116-232415-973616-ios16-r5`，最终清单为 `snapshot-final/analysis.json`。原始帧 SHA256 `e4d77b597409e1fbc851d869dcc876f65d943aaad5f4c0b3d95da35abbced016`；控制台 SHA256 `e924819f408cb1e9c0a1dcbca8c1d081c9030997daa30c20e2292fdcd53e473d`；照片 SHA256 `bf6c4188ce36b24c8e2915b2f10ecb1b794da3e1fec5890245db6355a2a54c62`。23:26:59 早期快照单独保留，不能作为最终结果。

## 下一修订建议

1. 在 SpringBoard 初始化期复用上游线程采样，记录主队列回调入口/出口、屏幕所有权与 CoreAnimation 首次桌面提交，分清等待 Mach/XPC、锁或仍在初始化。低频有界输出至现有 USB，不要求守屏。
2. 核对官方参数、真实服务就绪顺序、HID 接纳和时钟来源，针对证据修复；不强行隐藏白苹果遮罩冒充桌面。
3. 分解软件绘制/缩放/面板复制与调度耗时；修正 CPU 拓扑及驻留核合同后推进上游 SMP，逐步利用全部可用 RAM。性能工作不能代替桌面阻塞定位。
4. 新修订完成完整 HakiOS 构建、实际包检查与冻结后再上机，补齐日志收尾；不重试 R5、不重复刷 userdata，不单纯再延长一小时。

本次只做证据归档、源码只读核对和状态报告更新，没有新内核构建、运行代码修改或手机查询/重启/刷写。阶段四仍未完成。
