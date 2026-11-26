# 显示验收日志检查器（仅主机）

2026-11-18。显示目标继续未完成；本轮没有查询、启动、重启或刷写手机，
没有更改显示运行代码、payload、内核或分区，也没有运行QEMU。

## 本轮具体推进

新增 scripts/analyze-hakios-ios16-display-log.py，仅读取用户给定的单次本地
console。它不导入项目设备入口，不执行子进程，不联网。默认输出JSON；
可选输出必须是新文件，禁止覆盖输入或已有文件。

- 对照真实CPU字段和MPIDR affinity，不能只相信 match=1。
- 要求初始及修正后DT均为2/2/1334，物理BGR24仍为1080×2040/3240字节；
  不把独立750×1334 BGRA surface的行字节与物理stride混为一谈。
- 分别检查backboardd的MG/preferredScale/初始main size，实际CFNumber的
  main-screen-width/height/scale，三个UIScreen样本及三个主屏window样本。
  ScreenDimensions不可用时保留原观察，不推算一个值来填补。
- 记录匹配750×1334的backboardd BGRA surface及其有效行字节；这只证明
  出现该surface，不能独自证明它被物理面板扫描输出。
- 加载器行可能不在内核USB流中，缺失不直接否定用户态尺寸；若实际捕获
  的加载器或PROFILE数值矛盾则拒绝通过。截断、缺字段和缺样本保持未验证。
- 重复CPU记录或不同内核横幅不允许凑成一次有效采样。R16同次正常日志
  的行1和183会打印相同横幅，因此重复同一横幅只能记作边界未验证，不能
  擅自判成两次启动。文本仍无法认证镜像或排除所有截取/拼接，须另核
  冻结包和采集provenance。

出口码0仅表示必需的数值观察一致，1表示观察值有矛盾，2表示证据不全。
所有分支都保持 visual_acceptance=unverified、goal_complete=false。
即使数值全通过，也必须独立检查Dock四图标、标签遮挡和物理边界照片。

## 已保存R16的负例复核

使用已有 reports/local/20261118-042809-642502-ios16-r16/console.log，
不是新采集。精确复现命令（预期退出1）：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/analyze-hakios-ios16-display-log.py --log /mnt/f/IOS-MIX1/reports/local/20261118-042809-642502-ios16-r16/console.log
```

该日志的CPU、物理geometry和匹配surface通过；DT初始值、backboardd、
MobileGestalt、UIScreen及主屏window不符合目标profile。加载器/PROFILE
缺失保持未验证。完整字段、原行号和输入SHA256留在display-r16-log-analysis.json。
这再次证明R16的消费者结果未修好，不证明R18已成功或失败。

## 并行交付与复核

主代理负责检查器及保存日志复核；子代理Godel只编写独立测试文件
scripts/test-hakios-ios16-display-log.py，不写检查器或共享运行文件。
其测试覆盖合成正确profile、实际R16、矛盾值、截断/缺失/重复样本、
无效selector、非有限数、伪造match、可选loader以及CLI文件保护。
主代理独立执行测试并复核关键断言；命令、实际stdout/stderr、退出码和源码哈希见
display-log-checks.json。合成正例只证明检查器能识别相应文本。

首轮独立测试发现四处分类错误：未完成MG答案、截断window矩形、不可用
UIScreen selector及无效DT属性误分为fail；主代理以三态校验修正为
unverified。其后主代理逐项读取真实分析JSON，又发现“重复横幅即拼接”
这一测试前提不成立；用原日志行1/183纠正了实现和相应断言，并增加不同
版本横幅、完整双份CPU记录的对照。没有为通过测试忽略实际矛盾值。
双方无共享写入冲突，子代理研发耗时未独立记录；主代理复核与测试执行
证据另存JSON，不用测试时长冒充研发耗时。

最终22/22测试通过、零跳过，包括实际R16日志用例；最后一次测试自身
报告1.016秒。修正前的分析和结果分别保留为
display-r16-log-analysis-before-banner-fix.json和
display-log-checks-before-banner-fix.json，只作为失败前提的审计历史，
不作为当前检查器哈希或结论的证据。

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-display-log.py
```

这是独立主机工具变更，不影响内核链接输入，因此本轮没有重复整核构建，
没有借用旧构建结果声称出现新可启动包。R15固定基线与R18候选保留不动。

## 真正剩余的验收

当前仍缺R18同次运行的实际尺寸序列及主屏照片。普通启动入口的源/证据
闭包在外设工作后也已漂移，见display-goal-followup.md；本工具不修补、
替代或绕过它。未来上机前须按正常流程恢复该闭包或冻结经审查的新修订。
本报告不是设备操作授权，不请求现在启动或拍照。
