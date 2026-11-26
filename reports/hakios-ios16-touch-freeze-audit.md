# R20 触摸迁移：冻结差异溯源

R20-display 是用户指定的活动显示基线。R19 曾上机但显示分辨率不正确，只作历史归档；R20 的主屏验收记录为 1080×2040 BGR24、360×680 points、scale 3。显示验收不包括触摸。

## 三项差异的来源

当前工作区中的三项变动未提交。已用本项目的会话执行记录核实：**三项均来自 Codex 在 0020 触摸批量队列的主机集成工作**。下表时间均为 UTC（北京时间为 2026-11-19 03:25–03:28），开始/返回时间来自工具记录，写入时间来自文件元数据。记录仅用于溯源；没有把历史命令重新执行一遍。

| 文件 | 写入时间（UTC） | R19 冻结 SHA256 | 当前 SHA256 |
| --- | --- | --- | --- |
| `scripts/review-hakios-ios16.py` | 2026-11-18 19:25:47.433 | `7984ebe2824c1c542b164ee06574b9e5f2180db16f0603fc8ba6095b878dc3b7` | `c3b4991e0318ac4f4a688282ee7ad3eef43ee468ca2645f8e249afd84ce680a8` |
| `reports/hakios-ios16-source-review.json` | 2026-11-18 19:26:20.550 | `3f8e64fc995e94157f8c0c9e4947c0233b1650bf93640f1b8108468727aaed48` | `abc82892cf3df12398a3db8f6ba3e622d24ab9d80ae1e79edb96fc772cde3659` |
| `reports/hakios-kernel-build.json` | 2026-11-18 19:28:20.298 | `de8437709fed1193fdcfd71764f52c84c0d6547c0f6f40bc7788dbe4cb8c0299` | `e88d35b7b5038bf678fa32d2c1e058b9f4c40f1c9404b36d5b912a52ba9d9535` |

1. **源码审核脚本**：19:25:47.422，工具调用 `call_EC31UDnhOXZIkGGd8Xa5IOH4` 用补丁改写脚本。该改动使审核能在临时目录叠加 0019 准入钩子和 0020 批量队列补丁，并逐字节比对输入驱动和公开头文件。见 [审核实现](../scripts/review-hakios-ios16.py#L32)。
2. **源码审核报告**：19:26:08.262 至 19:26:20.594，工具调用 `call_kCOj9LBejZA4cwUb8OB7MiBO` 执行 `./scripts/run-wsl-limited.ps1 -Task hakios-r19-review`，退出码 0。入口运行上述脚本并写回共用报告路径（[写入处](../scripts/review-hakios-ios16.py#L213)）。当前报告同时记录了修改后脚本的完整哈希和 0020 审核结果。
3. **内核构建报告**：19:27:54.558 至 19:28:20.347，工具调用 `call_TY3KITvd0Mk4NVNWTm7aunN0` 执行 `./scripts/run-wsl-limited.ps1 -Task hakios-kernel`，包装入口退出码 0 并刷新共用报告（[写入处](../scripts/hakios.py#L279)）。19:28:55 的同会话核查记录了内核哈希和 `hakios_touch_post_batch` 符号；本轮在 WSL 中对实际工作区内核重新执行 `sha256sum`，哈希仍为 `32cec5999d77e9e9bc1642b1e942f35f218e2983892fe3cbd5503684552a13b1`，与报告一致。保存的构建日志含上游 kext symbol-set 错误（内部 make 退出码 2）；外层返回 0 只证明这一候选产物被写出，不能当作完整回归、发布冻结或触摸通过证据。

结论：差异来自后来的一次主机工作覆盖了**活跃工作树的共用路径**。不是旧镜像或旧冻结快照被重写。现行历史校验检测到的是实际差异；在当前目录运行时仍应拒绝，不能更改其判断或把它报告为通过。

## 本轮核验及交接

- PowerShell `Get-FileHash -Algorithm SHA256` 对 R19 contract、149 个快照及其工作树原路径、154 个冻结 port 路径、三个镜像复核，审计命令退出码 0。快照 149/149 匹配，共 1,620,843 字节；154 个冻结 port 路径无差异；工作树差异恰为上表三项。
- R19 contract：`ab5fcf8c87d5a604da1a881e6e44628ec700eb1ff655fcbf3cb2ebe257162904`。
- R15 镜像：`ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`；R19 镜像：`2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a`；R20-display 镜像：`3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`。均与原记录匹配。
- 本轮曾误用临时测试入口启动回归；在 `RUN existing 14 peripheral suites` 后中止（退出码 1），不计为测试证据。原严格入口已恢复至 `799ef5dc8c2546f203c76a2ec2b299e5cbfec0bff6c5fcc65b0a153f8af1c6c2`；WSL 确认没有该次测试的 Python 进程或运行中的临时 systemd 单元。没有改旧冻结或生成新启动镜像，没有查询或操作手机。
- Lagrange 核对冻结副本、提交历史与生成脚本；Galileo 核对 R20 显示证据、测试入口与内核接入缺口。主代理独立重算哈希、查阅具体历史工具调用与返回码并核对 WSL 内核产物。两位代理均未写文件、运行测试或操作设备。

下一步进入触摸资源审查和无复位首读路径。构建前先确认 QUP6 时钟、引脚和所有权证据，保留 GPIO105/125 两个假设；将来新镜像按新修订单独冻结，回归仍需校验工作树来源、产物及历史快照，旧 R19 门禁原样保留。当前无需用户配合真机。
