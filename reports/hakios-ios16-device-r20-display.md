# HakiOS MIX R20-display 真机结果

日期：2026-11-19

R20-display 临时启动已被 Fastboot 接受，未执行刷写、分区操作或 userdata 写入。用户确认已进入 iOS 主屏，并反馈“这个比例算正常了”；照片中状态栏、图标标签、搜索栏和完整 Dock 均在面板范围内，先前的放大和裁切问题已消失。因此本轮显示布局验收通过。

## 运行证据

- 镜像：artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img
- SHA256：3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82
- Fastboot：发送和 Booting 均 OKAY；本轮没有 flash。
- 真实 UIScreen：360x680 points，scale=3，native bounds 1080x2040，native scale=3。
- framebuffer：1080x2040，BGR24，stride 3240。
- SpringBoard：SBHomeScreenWindow 的 frame/bounds 均为 0,0,360,680。
- USB：已枚举并完成身份核验；9,488 帧，2,134,770 字节解码文本，缺口 0，重复 0，设备报告丢失 0；独立重解码与接收器结果一致。
- 照片证据：reports/local/20261119-011748-321-r20-display-preflight/r20-display-screen.jpg，SHA256：d62fcae3c002a7812944154e45e401b380f4c9f7031ff3397d3aa6c32ee0c5c8。

## 解释与范围

日志中的 main display width 750 height 1334 framebuffer width 1080 height 2040 是沿用的旧诊断输出；实际有效的 WindowServer、UIScreen 和 SpringBoard 窗口均已经是 1080x2040 / 360x680@3。SpringBoard 曾输出一次无状态栏 provider 告警，随后选择 _UIStatusBarVisualProvider_Split1080，照片显示状态栏正常。

本轮只确认显示布局。USB 日志在主屏证据取得后由主机停止，未捕获 10800 秒定时返回及 drain；GPU 加速、物理触摸、多核和长时间稳定性仍需分别验证。手机保持 R20-display 运行，没有重启。

证据目录：reports/local/20261119-011748-321-r20-display-preflight/
