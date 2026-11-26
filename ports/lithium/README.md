# lithium 的 XNU7195 移植候选

**历史路线，已于2026-11-14退出活动使用。仅允许只读对照；禁止继续修改、构建、打包、启动或作为兜底。下文复现命令不再是执行入口。项目唯一活动基线为完整HakiOS，见根目录AGENTS.md与PLAN.md。**

目标：iOS14.8 / 18H17，原生运行于 MIX1。这里保存面向目标内核的适配；不是可直接刷入的 ROM，也不是已完成的 UniXNU device repository。

基线是 Apple `xnu-7195.141.2` / `776661b72c2db9861865df68d309f6f35faccff4`。它与已取得的14.8原始内核7195.140.44存在小版本差异，仍需ABI核对。优先复用 `joranv1/ios-sailfish` / `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` 的同芯片实现；其8796内核和iOS16用户空间仅作参考。

当前活动补丁系列位于 `patches/xnu-7195/`，由 `prepare-ios14-lithium.py` 生成并验证，由 `build-ios14-xnu.py --machine LITHIUM` 应用到隔离副本。lithium 完整内核已经编译，QEMU 真实执行到 `kernel_bootstrap`，静态指令检查通过；BSD 前的 SHA1 接口缺口正在处理，真机尚未运行 XNU。详细证据见 `reports/ios14-xnu-bringup.md`。旧 `series.candidate` 只保存第一份 GIC 候选的历史，不代表活动系列。

复现源码检查与候选生成：

```powershell
python scripts/audit-reuse-candidates.py
python scripts/prepare-ios14-gic-backport.py
python scripts/prepare-ios14-lithium.py
wsl.exe -d Ubuntu -- python3 /mnt/f/IOS-MIX1/scripts/build-ios14-xnu.py headers
wsl.exe -d Ubuntu -- python3 /mnt/f/IOS-MIX1/scripts/build-ios14-xnu.py kernel
```

以上两条只操作电脑上的隔离产物和报告，不修改上游 checkout，不启动手机。路线和子系统复用表见 `reports/reuse-route.md`；参考驱动已还原到 `out/reuse/sailfish-drivers-6f27e41ff249/`，保留原文件中的版权信息。该快照供适配阅读，不是已编译的7195驱动包。
