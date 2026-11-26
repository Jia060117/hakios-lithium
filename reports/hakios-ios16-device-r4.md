# iOS16 R4 真机结果：白苹果与物理图形输出

2026-11-16，完整 HakiOS 63fc2b4633bac3af8e57dadab9f5a8d4cb940438 / XNU8796.121.2，用户提供的 iOS16.5 / 20F66 / D22AP / arm64。R4 已结束；用户照片确认白苹果，结束摘要明确显示 TIMER DEADLINE REACHED。阶段四尚未完成，真实 SpringBoard 桌面仍待验证。

## 冻结输入和本轮修正

实际包为 `artifacts/hakios/lithium-ios16/phase4-r4/lithium-hakios-ios16-r4.img`，16,228,352 字节，SHA256 `8a3230840e8b5636be18fa92e13cef781811e45f057632072a95b6ea2a1bad6d`；内核 SHA256 `ad5ac24a71d183dcb66628771d0cc55c08292beb5bee0beab8218a5356cd137d`。完整内核、真实 D22AP loader、Mu 构建、46 项指令检查、真实控制台/摘要 ASan/UBSan 和 16 项实际包检查通过。

本轮在实际 BSD `_cnputc/_cnputs` 采集控制台，关闭屏幕文字后继续传输；移除 kmoutput 重复采集，关闭全局 systrace，独立保留 usertraps；结束摘要区分 timer、panic 和未知原因。2026-11-16T22:13:46.602238+08:00 临时启动一次获 Fastboot 接受，累计 35 次 HakiOS 启动，其中 iOS16 为 4 次。本轮没有重复刷写 userdata，已消费的 R4 入口不重试。

## 图形与系统证据

最终 USB 控制台包含原始 launchd PID1、Early boot complete、System 与 OS/App cryptex 挂载、Data 实际写入读回，以及 45 项 init 检查通过。SpringBoard[8]、backboardd[13]、containermanagerd[14] 均有运行记录。

日志行号基于本地 `snapshot-final/console.log`：

- 第 1116 行，backboardd 得到有效系统容器缓存路径；本轮未重现旧 iOS14 的 nil 路径崩溃。完整容器 RPC 未独立验证。
- 第 1176、1188、1421 行，BootUI 选择 Apple Logo，ProgressUI 加载系统 `apple-logo@3x~iphone.png`。
- 第 1220、1437 行，backboardd 创建真实 750×1334 BGRA IOSurface；第 1226、1243、1441 行，显示驱动提交 swap 1、2、3。
- 用户提供的录像截图显示手机物理屏幕上的白苹果。结合实际图形表面和提交记录，可确认 iOS 用户空间图形输出已到达面板。
- 第 1290 行，backboardd 与 SpringBoard[8] 的 System shell 连接建立。但后续仍有 No explicit screen owner 和 activity assertion 等待，尚无桌面帧证据。

CoreAnimation 的 Software renderer / Failed to initialize Metal or OpenGLES 日志之后仍有表面创建和帧提交，不能仅凭其中的 Simulating crash 字样判定进程已退出。当前是软件绘制，A530 加速未通过；物理触摸未通过。

## 启动慢的证据与下一停点

下表是 backboardd 自报阶段时长，未用外部秒表核验，也不代表全部都是硬件计算时间。HID open 包含在 HID 总计内，不能重复累加。

| 阶段 | 日志自报耗时 |
| --- | ---: |
| CoreAccessories 初始化 | 211.406 秒 |
| HID system open | 171.522 秒 |
| HID system 总计 | 176.407 秒 |
| Window server 初始化 | 113.207 秒 |
| Backlight 初始化 | 17.8114 秒 |
| backboardd main 至 backlight ready 总计 | 523.118 秒 |

BootUI 的 activity assertion 获取另报告 262.966 秒；第 1289 行仍等待 main screen digitizer，elapsed 为负值，需核对时钟使用、服务匹配与实际构建输入，尚不能断言计时器故障。第 1246 行的早期两帧统计为 8,920ms、驱动内每帧 4,304,368us；它包含调度、日志与绘制，尚不是纯像素转换耗时或稳定帧率。实际运行仍为单 CPU、2,880MiB 窗口。最后日志约有 1,956MB free / 2,671MB available，没有内存耗尽证据；完整 6GB 与多核仍待实现/验证。

下一修订应核对完整上游的 HID 服务注册与重连、BootUI activity assertion 和 SpringBoard 屏幕接管，并分段测量配件/HID 初始化及软件绘制/面板复制耗时。只有明确实际等待来源后再修正；不以删除服务、跳过初始化、重分区或单纯延长时限代替诊断。

## USB 与结束状态

最终快照位于忽略目录 `reports/local/20261116-221346-357368-ios16-r4/snapshot-final`。原始帧 405,501 字节，解码控制台 206,173 字节 / 6,229 帧；使用本轮冻结接收器按 997 字节重新分块解码，与保存的控制台逐字节一致，缺口、重复、不完整帧均为 0。控制台 SHA256 `8c36fc8b168c679cbf6d43328725fbf2e2d573f6eab975c4fdac077d4be0aaed`，原始帧 SHA256 `97d6f5beabfbb84e4f999c71ad2d5e813f8cc2391c549162c3ecf3a81ed70aa9`。

USB 从 Darwin 内核横幅覆盖到 launchd 与图形启动，但于 2026-11-16T22:44:25.712945+08:00 以 `capture_failed / [Errno 32] Pipe error` 结束，没有返回尾部/deadline 标记。已接收部分完整可复核，完整日志捕获尚未通过；不能把控制台字节数与屏幕 RAW BYTES 直接相减为丢失量，loader 返回还会追加 msgbuf 等内容。

用户结束照片明确显示 `LITHIUM IOS16 RETURN RAW BYTES 257260`、`RETURN: TIMER DEADLINE REACHED`、`NO PANIC TEXT IN RETAINED LOG` 和 `SUMMARY RETAINED UNTIL REBOOT`。因此本轮可以确认定时返回；未见 panic 正文不等于证明一切异常均不存在。

原图保存在本地 `photos` 子目录：结束摘要 SHA256 `a74ba2ebc919d20f8c2f9883efc0b83e785b228736d3ad31a59589d9e3a57f10`；白苹果录像截图 SHA256 `2cc486483b4b9854177b0bba0fc6e540ab028d766c225974fdef3ec7631164bf`。照片、USB 原始日志和 Apple 二进制不加入 Git。

本次结果归档未修改运行源码、构建新包、查询/启动手机或刷写分区。阶段四继续，下一修正包完成并冻结后再安排上机，无需用户守屏。
