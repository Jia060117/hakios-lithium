# 项目活动入口

`hakios.py` 是唯一内核流程入口。它核对固定 HakiOS 提交、目标用户空间、来源锁和设备配置；不接受旧内核、替代基线、任意 Make target、模拟器或真机启动命令。

| 命令 | 作用 |
| --- | --- |
| `python scripts/hakios.py status` | 读取活动基线、P0门槛和最后记录的设备状态 |
| `python scripts/hakios.py audit` | 核对上游声明依赖并记录完整原始checkout的文件哈希 |
| `python scripts/hakios.py check`（Windows） | 检查路线、旧源码和存档哈希；不运行上游配置工具 |
| `scripts/run-wsl-limited.ps1 -Task hakios-prepare` | 以固定提交建立完整Ubuntu隔离checkout，同步本机设备层；拒绝覆盖未记录修改 |
| `scripts/run-wsl-limited.ps1 -Task hakios-check` | 核验完整隔离来源，并执行上游设备、config与驱动清单检查 |
| `scripts/run-wsl-limited.ps1 -Task hakios-kernel` | 只有P0闭包证据与本机加载契约就绪后，按完整上游make kernel DEVICE=lithium及config/hakios-resources.json中的并发构建 |
| `scripts/run-wsl-limited.ps1 -Task hakios-version-audit` | 只读比较18H17与20F66原始镜像、程序及配置，核对固定上游；不启动设备 |
| `scripts/run-wsl-limited.ps1 -Task hakios-storage` | 构建P2只读存储诊断，使用上游APFS工具校验小型根盘；不启动设备 |
| `scripts/run-wsl-limited.ps1 -Task hakios-dynamic` | 构建完整上游init/libmini及18H17动态测试分支，核验原始dyld/df/mount/cache输入与APFS根盘；不启动设备 |
| `python scripts/test-hakios-workflow.py` | 10项路线隔离、状态、冻结文件和拒绝覆盖/过期输入的回归检查 |
| `scripts/prepare-hakios-ui.ps1 -Task <任务>` | 联合UI的prepare/abi/configure/refine/methods/summary/workspace/deps/root/support/check/audit/p0/kernel/package/freeze/tests任务；使用同一独占资源配额，大任务顺序执行 |
| `python scripts/boot-hakios-ui.py` | 只读核验745项冻结证据、镜像、授权MIX身份/解锁/容量/电池；不启动设备 |
| `python scripts/boot-hakios-ui.py --boot` | 在既有授权内临时启动冻结的联合UI镜像一次，记录意图，禁止自动重试；无分区写入 |

WSL任务固定单一systemd unit，使用config/hakios-resources.json的16GB/12逻辑处理器/2GB swap，任务14GB/1200% CPU，依赖与内核并发12；不能同时启动两项任务。新增 boot-hakios-lithium.py：默认只读预检，--boot核对授权MIX身份/解锁/分区后只临时启动经验证的HakiOS镜像。

原厂和系统输入检查工具继续保留：probe-device/probe-blocks、extract-stock、capture-live-dt、inspect/decode-ios14、inspect-local-ios14、inspect-ios14-root-files、audit-platform-inputs、analyze-aboot。inspect-native-xnu、inspect-ios14-dyld、audit-reuse-candidates与display_probe仅用于历史材料或输入对照，不是活动内核路径。

其余60个原执行脚本已替换成拒绝入口，不执行原逻辑；原文及哈希见 `reports/history/hakios-refactor-20261114/scripts.json`。旧WSL任务也拒绝。历史命令不能恢复授权，原存档只能阅读。

依赖、支持组件和打包任务分别为 hakios-deps / hakios-support / hakios-package；具体输入和实机步骤见 [首包记录](../reports/hakios-platform-test-ready.md)。

Windows运行 `python scripts/fetch-hakios-inputs.py` 获取并核验固定的8份源码、SDK Git归档与macOS Kernel.framework归档；WSL依赖任务从这些同版本缓存调用正常上游setup/apply，保留Unix符号链接。原始系统DMG的只读边界审计为 `audit-hakios-ios14.py`，不启动系统用户空间。

当前活动配置为 `p2-ios14-ui`，联合包见[冻结记录](../reports/hakios-ios14-ui-ready.md)。由完整上游init/libmini完成UFS根切换、ramfs初始化和原始launchd PID1交接；333项原始任务配置和2项后台诊断同包运行，显示/IOSurface使用原18H17布局，结束保留摘要。物理触摸和A530加速仍未通过。此前动态工具在R24通过、进程存在性在R28通过，原 `p2-ios14-dynamic` 与三任务launchd入口仅保留复现历史；当前联合上机使用上述专用入口。
