# R16 显示配置修订交付

已完成源码备份、修订、完整 HakiOS/Mu/D22AP/用户空间构建及冻结检查。本轮没有查询、重启或刷写手机；当前手机继续运行 R15。**Dock、标签和实际屏幕边界验收均未验证。**

## 结论和修复范围

`3240` 是 BGR24 物理面板的每行字节数，`3240 / 3 = 1080`。没有观测到 810 像素宽的缓冲；810 来自将三字节像素误按四字节计算。BGRA32 渲染面另有 3072 字节对齐 stride，对应可见宽 750、行容量 768，二者不是物理面板行格式。

R15 实测 backboardd 接收 `750×1334 / scale=3`；供给 D22AP 身份解析为 artwork subtype 2436。其 `250×444.667` 点只是尺寸除法，R15 没有真实 UIScreen.bounds 记录。此配置与屏幕类别不一致是具体修正对象，尚不能将全部照片缺陷归因于它。

R16 对 Lithium 当前准确的 `750×1334` 且未显式指定 fbscale/fbsubtype 的配置，复用上游设备树设置路径，采用 scale=2、artwork subtype=1334，目标逻辑尺寸为 `375×667`。显式参数及其它画布沿用上游含义。设备树确认为该配置时，SpringBoard 不再安装 iPhone X 的 split status-bar fallback，而由 UIKit 选择原生 provider。物理面板、stride、BGRA→BGR24、CPU/UFS/RAM 和完整画面到面板的映射保持原行为。

原映射把整个画布映射至 1080×2040，不会按几何公式裁掉左右边缘，但仍有约 6.2% 的纵横比例差异。此轮保留映射以单独验证显示 profile 修正，等比居中尚未实施；若仍需消除拉伸，应在尺寸日志通过后另作修订。

只读尺寸链路及 R15 行号见 `reports/hakios-ios16-display-audit.md`。

## 关键改动和当前行号

- `ports/hakios-lithium/diagnostics/display-profile.h:11`
- `ports/hakios-lithium/diagnostics/display-kernel.inc:17`
- `ports/hakios-lithium/diagnostics/display-userspace.inc:66`
- `ports/hakios-lithium/patches/0016-lithium-display-profile.patch:16`
- `scripts/hakios_r16.py:8`
- `scripts/prepare-hakios-ios16-r16.py:9`
- `scripts/test-hakios-ios16-r16.py:78`
- `scripts/test-hakios-ios16-r16-package.py:8`

实际活动源码（WSL）：

- `/home/jia/ios-mix1-build/hakios-lithium-ios16/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:621`
- `/home/jia/ios-mix1-build/hakios-lithium-ios16/userland/src/inject_sb.c:1384`

其余脚本改动仅注册 R16、验证精确 before/after 哈希链并延续已有构建与回归门禁。没有去掉功能或放宽未知改动检查。实际函数 ASan/UBSan、显式 profile 保留、DT 属性缺失/格式异常、R15 原文反向重构、既有 1650 项像素对照通过；46 项编译指令检查、413 项用户空间任务、16 项实际包检查及 R16 payload 参数逐字节读回通过。主机检查不等于真机通过。

备份分支 `archive/ios16-r15-before-r16-20261118`；外部 dirty-source.zip SHA256 `58e9f526233f3eabbcdb14f492772255144eba884c6de4adffd47351dac89f6a`。分支保留原提交，外部压缩包保留 R15 未提交源码和实际编译树差异；R15 冻结镜像未覆盖。Apple 二进制和供给固件没有加入 Git 或共享材料。

## 冻结镜像与全部启动参数

私有镜像：`artifacts/hakios/lithium-ios16/phase4-r16/lithium-hakios-ios16-r16.img`，16269312 字节。

SHA256：`bd466615ab19ccd44eb23ec87273a23a499681d954d64e6b88c23676ffb8f94b`。

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

该 payload 参数与冻结 R15 **逐字节一致**，尤其 `bootcpu=1 cpus=1`。加载器另外补充 debug、rd、动态分配的 ufs_shared_pa 和实际 hakios_kver；完整运行参数以新日志 `LITHIUM BOOTARGS` 为准，不能沿用上次动态地址。

```powershell
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-prepare
.\scripts\run-wsl-limited.ps1 -Task hakios-deps
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-review
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-regressions
.\scripts\run-wsl-limited.ps1 -Task hakios-kernel
.\scripts\run-wsl-limited.ps1 -Task hakios-support
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-userspace
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-build-check
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-freeze
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-package
.\scripts\run-wsl-limited.ps1 -Task hakios-r16-package-checks
wsl -d Ubuntu -u jia --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-r16-package.py
```

以上命令已完成。真机准备好 Fastboot 后，使用受保护入口临时启动新包一次：

```powershell
python scripts/boot-hakios-ios16.py --boot
```

该入口检查新冻结证据和设备身份、提前启动 USB 接收；不需要重新刷 userdata。此交付未执行这条启动命令。

## 回传日志与照片

保留 `LITHIUM BOOTARGS`、`LITHIUM RUN A`，以及所有 `LITHIUM DISPLAY DT / GEOMETRY / STATUSBAR / MG / UISCREEN / WINDOWS / WINDOW`，加上 `MiniIOMobileFramebuffer: display`、backboardd `main display info` 和实际 IOSurface 创建行。

预期：DT before 3/3/2436，after 2/2/1334；GEOMETRY render=750×1334、panel=1080×2040、panel_stride=3240、panel_bpp=24、auto_profile=1；UIScreen 预期 bounds=0,0,375,667、scale=2，nativeBounds/nativeScale 和 MG 每项须读取真实值。MG ScreenDimensions 若不存在，会明确打印 unavailable，不伪造返回值。日志在主队列运行后分三次有界输出，构造器不会查询 UIScreen。R15 缺失的 UIKit 实测值不能事后补成“修复前实测”。

请用另一设备正对手机拍一张完整主屏幕，四边和整个 Dock 都在照片中；若标签不清，再补一张底部两排图标与 Dock 的近照，保持镜头尽量垂直。无需守屏或抓瞬间。

验收：Dock 四个图标完整、标签不被下一行遮挡、界面内容不越过左右及底部。所有这些项目、MG/UIScreen 真机值、原生状态栏 provider 结果、物理触摸均**未验证**。首次图层或白苹果不是本次验收通过。
