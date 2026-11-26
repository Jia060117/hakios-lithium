# 按 HakiOS 新计划完成项目重构

2026-11-14。范围是项目结构、执行入口、来源/状态隔离和 P0 首轮审计；没有内核实现或真机能力通过声明。

新活动流程绑定完整 HakiOS `63fc2b4633bac3af8e57dadab9f5a8d4cb940438` / XNU8796.121.2。Ubuntu独立checkout从此完整本地来源克隆，分支codex/hakios-lithium；ports/hakios-lithium映射到devices/lithium，使用common_defconfig、MSM8996与lithium片段。没有引入旧7195运行时。

重构前308个项目文件的路径、大小和SHA256已记录；75份脚本原文逐字节存入reports/history/hakios-refactor-20261114/scripts/*.txt。60个旧构建、补丁、打包、启动、安装和运行时验证入口替换为拒绝执行的桩；WSL旧任务提前拒绝，默认改为hakios-check。旧内核源码/补丁、引导、固件和诊断源码保持原位，通过98个文件的哈希复核。

project.json采用schema2，仅记录活动HakiOS状态、目标、最后观测和可复用输入。旧native/launchd/mount成功标志与完整attempt记录存入重构前归档，不再混入活动验收。userdata已为HFSX和既有操作授权仍明确保留；没有查询设备或重写设备状态。

验证结果：

- Windows路线/冻结检查通过：98份参考源码与75份脚本存档一致，所有旧执行入口为无副作用拒绝桩。没有执行存档或旧脚本原逻辑。
- HakiOS来源审计核对879个原始checkout文件和13项上游声明的依赖提交/哈希。
- Ubuntu限资源准备和检查通过：上游hakios-device.py check lithium、make lithium_defconfig、hakios-config.py check、drivers-check.py。
- 8项回归检查通过：拒绝其他基线/提交/系统目标、P0阻止构建、活动状态不继承旧成功、冻结哈希、拒绝覆盖隔离设备修改、拒绝过期输入。
- 新build-kernel在当前P0/加载契约条件下返回拒绝，没有编译内核。资源入口配置8GB/4CPU/2GB swap、任务6GB/并发2，实际准备与检查在对应systemd cgroup内运行。

最终一致性复核见 [hakios-refactor-verification.json](hakios-refactor-verification.json)：来源审计、隔离工作区和配置检查绑定同一份manifest/设备文件哈希；98份参考源码、75份脚本及6份文档存档一致。归档说明使用INDEX.md，原README保留为同名历史存档。

P0首轮结果见hakios-p0-audit.md；尚缺SDK/工具链和完整加载依赖冻结、ARMv8.0/PAN、iOS14 ABI、A530渲染及合成全路径。没有构建内核、打包镜像、运行QEMU、启动HakiOS或操作手机。项目重构完成，P0保持in_progress，后续按PLAN继续审计，不恢复FactoryData旧路线。
