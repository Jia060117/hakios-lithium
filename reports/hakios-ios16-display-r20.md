# iOS 16 显示修正候选 R20-display

日期：2026-11-19。范围：R19 日志/照片分析、显示参数源码复核、主机侧镜像生成与独立读回。没有查询、启动或刷写手机，没有使用 ADB/Fastboot 设备命令，没有修改分区、userdata 或活动源码，没有运行 QEMU。

## 结论

R19 的显示缓冲仍为 750×1334，虽然 loader/device tree 报告 scale 2，但同次实机日志中 backboardd 的 MobileGestalt 仍返回 scale 3，SpringBoard 的 UIScreen 为 250×444.667 点、scale 3。照片中的大图标、内容被裁切与这个有效逻辑宽度相符。状态栏还报 No status bar visual provider / rdar://45025538。

内核 MiniIOMobileFramebuffer.cpp 的 fbwidth/fbheight boot-arg 直接控制用户态拿到的显示像素尺寸（104–105、606–607、1826–1833）；drawLayer 按该尺寸映射至物理面板（1076–1108）。SpringBoard 注入按宽度 1080 选择 _UIStatusBarVisualProvider_Split1080（inject_sb.c:1372–1375）。R19 的 750×1334 因此让逻辑坐标和状态栏兼容分支都不匹配。

## 候选改动

独立生成的镜像：artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img  
大小：16,269,312 字节  
SHA256：3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82

只把 fbwidth=750 fbheight=1334 改为 fbwidth=1080 fbheight=2040。保留 R19 中 bootcpu=1 cpus=1、UFS/Data、USB、软件绘制、观察时长、内核、两份 APFS 引导卷、loader FAT、DTB 及其他所有 payload 项。未改文件系统或物理面板格式。候选预期用 scale 3 获得约 360×680 点的 UIScreen，并按 1080×2040 缓冲映射到 1080×2040 BGR24 面板；这是源码推导，尚未在真机验证，也不保证单靠此项就能通过完整屏幕验收。

没有使用 fbscale=2：R19 已显示仅改变 loader/device-tree scale 不会让 MobileGestalt/UIKit 跟随变为 scale 2；上游内核在 fbscale 条件下还会调整显示宽高，加入它会引入第二个变量。

## 验证

- wsl.exe -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-ios16-display-r20.py：通过。输出从冻结 R19 XNUP payload 派生，没有重编内核或用户空间。
- wsl.exe -d Ubuntu -u jia --exec python3 -B /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-display-r20.py：通过独立解包、XNUP 与实际启动参数读回、Android boot header/digest 检查。
- 内核、kernel.ver、root APFS、resident APFS、loader FAT、20 份来源 DTB 和 payload 外的 Image 字节均与 R19 一致；R15 桌面镜像仍保留，R19 原包未改。
- 检查明细：reports/hakios-ios16-display-r20-checks.json。打包/检查工具：scripts/package-hakios-ios16-display-r20.py、scripts/test-hakios-ios16-display-r20.py。

## 仍待验证

R20-display 的真实启动、MG/UIScreen 尺寸、状态栏红条是否消失、Dock/图标标签/屏幕边界、画面锐度与完整分辨率均未验证。测试时优先从日志核对 backboardd 的 MG scale、UIScreen 是否接近 360×680@3、surface 是否 1080×2040，以及 No status bar visual provider 是否仍出现；再以包含屏幕四边、状态栏、完整 Dock 的正面照片验收。

解释日志时要注意：沿用的 R18 诊断代码把 375×667@2 和 DT 2/1334 profile 写成固定期望值。因此 R20 若实际报告 360×680@3，旧诊断行中的 match=0、native_profile=0 属于期望模板未更新，不足以单独判定布局失败。应检查实际 bounds/scale、backboardd 创建的 surface 尺寸、状态栏 provider 错误是否继续出现，并看真机照片。

项目现有 AGENTS.md 对设备操作的无人值守限制仍有效，所以本次只准备了可以检查的候选镜像，没有连接或查询手机。需要用户明确恢复真机测试后，才能临时启动该候选；不需要刷写 userdata 或分区。

## 真机复测结果（2026-11-19）

用户明确恢复真机测试后，R20-display 经只读身份/解锁/容量/电池核验并临时启动一次；Fastboot 的 Sending 与 Booting 均返回 OKAY，没有执行 flash、分区写入或 userdata 写入。用户确认进入主屏，并反馈比例正常。照片显示状态栏、图标标签、搜索栏和完整 Dock 均位于屏幕范围内，先前的放大和裁切问题已消失，显示布局验收通过。

同次 USB 日志确认 WindowServer/main screen 为 1080×2040、scale 3；SpringBoard UIScreen 为 360×680 points、scale 3、native bounds 1080×2040；SBHomeScreenWindow frame/bounds 为 360×680。framebuffer 为 1080×2040 BGR24、stride 3240。接收 9,488 帧、2,134,770 字节，缺口/重复/设备报告丢失均为 0，独立重解码一致。照片和原始 USB 证据见 reports/local/20261119-011748-321-r20-display-preflight/。

日志仍有一条旧诊断文字报告 main display 750×1334，但实际 UIScreen、WindowServer 和屏幕照片吻合 1080×2040 / 360×680@3。另有一次 No status bar visual provider 告警，随后已选择 Split1080 fallback；照片中状态栏正常显示。USB 接收在主屏证据取得后由主机停止，因此正常定时返回/drain、GPU 加速、物理触摸和长时间稳定性不在本次验收范围。真机结果详见 reports/hakios-ios16-device-r20-display.md 与 JSON。
