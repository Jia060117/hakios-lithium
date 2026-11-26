# R18 display audit — host only, 2026-11-18

The current task retains the complete R15/R16 payload, including 750x1334, bootcpu=1 and cpus=1. Historical R17 changes the render canvas to 1125x2436 and is retained as a frozen comparison; it does not satisfy this task. No phone commands are authorized for this preparation.

## Runtime evidence rechecked

Saved input: reports/local/20261118-042809-642502-ios16-r16/console.log.

| Lines | Observed value | Consequence |
|---|---|---|
| 91–94 | DT before 3/3/2436; after 2/2/1334; render750x1334; panel1080x2040; row3240; bpp24 | R16 changed the registry successfully. |
| 121 | logical0 MPIDR0x80000001 affinity0x1, bootcpu1/cpus1, match1 | The existing fast CPU mapping is verified for R16; preserve it. |
| 1716–1719,1977 | backboardd MG scale3, display preferredScale3, main750x1334 scale3 | The consumer does not use the updated registry scale. |
| 3089 | UIScreen250x444.667, scale3; no native status-bar provider | Confirms a real logical profile mismatch. |
| 4925–4938 | UIScreen250x444.667 scale3, native750x1334 nativeScale3; MG width750 height1334 scale3; ScreenDimensions unavailable | R16 is insufficient; an unavailable MG key must not be presented as a measured size. |
| 5472,7311 | Same UIScreen profile in all three deferred samples | This is not only a transient initialization value. |

3240/3=1080 for BGR24 scanout. There is no demonstrated 810-pixel buffer. The separate BGRA32 render surface has aligned row3072 (capacity768 pixels, visible750). These two buffers need their own formats and strides. A single shared stride would corrupt memory or colors.

The actual conflict is 750 physical render pixels / 3 pixels per point = 250 logical points while the device identity describes a 375-point-wide phone. Internal Dock/grid layout can exceed that narrow logical width. Changing only the final panel scaling cannot repair those internal layouts. Exact visual causality and the post-fix Dock/label/bounds criteria remain unverified.

The existing compositor maps the complete render rectangle to the panel, with x ratio1.44 and y ratio2040/1334. This slightly distorts aspect ratio but does not inherently crop the outer source edges. Keep this mapping during the profile correction so the experiment changes one cause.

## Preservation

Backup branch: archive/ios16-display-before-r18-20261118. External dirty-source.zip SHA256 3b0cfbdcbad265dbaa4baedfa041e8765dceb4392ebcfaf67d74b1fdd408b6b6. Immutable baseline-r15.img, baseline-r16.img and baseline-r17.img copies were verified under E:/IOS-MIX1-artifacts/ios16-d22ap/display-before-r18-20261118. No proprietary inputs are added to Git.

The same backup folder contains `r18-host-source.patch`, an export of the R18 source, build and verification changes against the saved pre-R18 tree. `git apply --reverse --check` passed against the current repository. The export contains no image or firmware bytes. After any source rollback, use the corresponding historical preparation and build commands to resync the WSL tree.

## Size chain and R18 change

The source line numbers below refer to R18. Paths beginning with `xnuloader`, `xnu`, `xnu-src` or `userland` are under `/home/jia/ios-mix1-build/hakios-lithium-ios16` in Ubuntu.

| Stage | Source and observed behavior |
|---|---|
| Physical scanout | `ports/hakios-lithium/firmware/lithiumPkg/Include/Lithium.h:6` gives base `0x83401000`, width 1080, height 2040 and stride 3240. `firmware/lithiumPkg/Drivers/LithiumDisplayDxe/LithiumDisplayDxe.c:79` publishes `PixelBltOnly`. The UEFI BLT converts its four-byte input pixels to BGR24 scanout. |
| Loader video and framebuffer DT | `xnuloader/loader.c:4636` selects the Lithium scanout. `:3332` writes its dimensions, stride and 24-bit depth to boot video. `:3668` publishes the physical address, length, width, height and stride under `/lithium-fbcon`. |
| Loader display identity | `xnuloader/loader.c:3442` resolves the existing payload before writing the tree, `:3481` emits `/chosen/display-scale` and `:3560` copies the `/product` properties. `ports/hakios-lithium/diagnostics/display-loader.inc:47` resolves the exact implicit `750x1334` profile and logs the original and selected values. `:70` replaces only the existing `artwork-scale-factor` and `artwork-device-subtype` properties on the product node. |
| Flattened tree to IORegistry | XNU `xnu-src/xnu-8796.121.2/pexpert/arm/pe_init.c:403` initializes SecureDT with the flattened tree. `iokit/Kernel/IODeviceTreeSupport.cpp:422` reads the properties into registry objects. Replacing a registry property later during framebuffer start leaves the flattened property bytes intact. |
| XNU render mode | `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:579` checks the physical depth and stride, `:611` reads boot video and `:616` takes render width/height from the boot arguments. `:543` publishes the display mode and `:1940` returns the render size to userspace. `:620` and `:649` log registry values immediately before and after the existing profile code. |
| IOSurface and composition | `MiniIOMobileFramebuffer.cpp:797` requests the default BGRA surface. `xnu/files/iokit/Kernel/MiniIOSurface.cpp:588` takes explicit row bytes when present, with an aligned fallback. The actual R16 backboardd primary surface was BGRA `750x1334`, row `3072` (`console.log:2021`). The compositor uses the actual surface stride at `MiniIOMobileFramebuffer.cpp:1240`, scales the rectangle at `:1173`, and writes the physical three-byte pixels at `:865`. |
| backboardd and SpringBoard | The closed userspace consumers are evidenced by `console.log:1716` and `:4925`, with their actual MG and IOSurface values at `:4933` and `:2021`. `userland/src/inject_sb.c:1034` selects the R18 diagnostic, `:1384` retains the native status bar branch for the corrected profile, and `:2284` schedules three samples after the main queue enters. `ports/hakios-lithium/diagnostics/display-userspace-r18.inc:37` calls real `MGCopyAnswer`; `:66` reads real `UIScreen` bounds, native bounds and scales. `:93` compares the readings against the expected logical `375x667@2`, native `750x1334@2` profile. |

R18 supplies scale 2 and artwork subtype 1334 at the loader boundary. R16 only changed these values in IORegistry later. This corrects the disagreement at the source. The reason the iOS 16.5 MobileGestalt consumer retained scale 3 in R16 is **未验证**. If R18 logs show 2 in both the loader and kernel but 3 in backboardd, the remaining investigation is the userspace scale source or cache. Full-panel composition remains the selected map because the two buffer rectangles already map edge to edge.

Changes are recorded in `ports/hakios-lithium/patches/0018-lithium-early-display-profile.patch` and its two diagnostic includes. The R18 host checks are in `scripts/test-hakios-ios16-r18.py` and `scripts/test-hakios-ios16-r18-package.py`. Sagan independently traced the flattened DT and registry boundary. The main agent checked the cited sources and the R16 console, implemented the correction and verified the build. Pascal's delegated audit/test task terminated with a model API error. The main agent implemented and ran the host tests.

## Build and frozen image

From `F:\IOS-MIX1` in PowerShell 7:

```powershell
$tasks = 'hakios-r18-prepare','hakios-r18-review','hakios-deps','hakios-kernel',
    'hakios-support','hakios-r18-userspace','hakios-r18-build-check','hakios-r18-freeze',
    'hakios-r18-regressions','hakios-r18-package','hakios-r18-package-checks'
foreach ($task in $tasks) {
    & pwsh -NoProfile -File scripts/run-wsl-limited.ps1 -Task $task
    if ($LASTEXITCODE -ne 0) { throw "Failed: $task" }
}
```

The packager rejects overwriting an existing revision. The released local image is `artifacts/hakios/lithium-ios16/phase4-r18/lithium-hakios-ios16-r18.img`, 16,269,312 bytes, SHA256 `8d5aa292bb747f3e5022de7a9fa5adee3bb9422d8fa296f4b6a376ca59a973bd`. It is a private local boot artifact.

The complete HakiOS kernel, Mu, D22AP loader and 413 userland jobs built. Source checks, 46 instruction scans, the complete host regression chain, 10 flattened DT cases under ASan/UBSan and 16 actual package/boundary checks passed. The R18 checker read the XNUP inside the compressed boot image, confirmed the actual R15 command line byte for byte, and verified the R15/R16/R17 baseline copies. The initial verifier mishandled the alignment after XNUP. It now checks the actual symbol span and its AArch64 NOP padding. Repackaging kept the compressed region's uncompressed bytes and EFI loader identical. The differences in the FAT volume serial and directory time fields changed the outer image hash. The hash above identifies the validated image.

```text
bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1
```

## 真机验收（未验证）

- 到主屏后用另一台设备垂直正对屏幕拍一张，保留屏幕四边、顶栏、完整 Dock 和底部留白。标签不清晰时再补拍底部两排图标和 Dock。验收以 Dock 四个图标完整、标签不被下一行遮挡、内容不越过屏幕边界为准。
- 回传 `LITHIUM DISPLAY DT`、`LITHIUM DISPLAY GEOMETRY`、backboardd 的 `StartWindowServer: MG scale` / `initial main screen size`、backboardd 的 `MiniIOSurface: ... created surface`、`LITHIUM DISPLAY MG`、`LITHIUM DISPLAY UISCREEN`、`LITHIUM DISPLAY PROFILE`、`LITHIUM DISPLAY WINDOW` 和 `LITHIUM RUN A`。若已有加载器控制台日志，附 `LITHIUM DISPLAY LOADER` 两行。内核 USB 捕获从内核阶段开始，加载器行可能只出现在早期控制台。
- 预期加载器值从 `3/3/2436` 改到 `2/2/1334`，内核 `phase=before` 和 `phase=after` 都应读到 `2/2/1334`。预期主屏逻辑尺寸 `375x667@2`，原生尺寸 `750x1334@2`，`match=1`。R16 的 `ScreenDimensions` 是空答案，关键对照是实际 `main-screen-*`、backboardd 和 UIScreen 三者是否一致。
- 当前未执行任何设备查询、启动或分区操作。R18 的显示结果以及本轮实际 CPU 在线状态均未验证。单核 `affinity=0x1` 来自 R15/R16 真机日志，R18 复用了同一参数与 CPU 映射代码。
