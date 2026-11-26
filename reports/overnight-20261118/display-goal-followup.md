# 显示目标续查与完成性审计

日期：2026-11-18。此次自动续跑取回的活动目标仍为显示尺寸链路修复、
Dock四图标完整、标签不重叠、内容不越界。上一轮外设主机开发取得了源码
和验证成果，但没有推进该显示目标的真机验收；不能将它当成目标完成。

本次没有更改显示运行源码，也没有构建、查询或操作手机，没有执行QEMU。
保留用户当前无人值守限制。没有接受/修改旧照片内容，没有把离线推算当成
桌面画面。原照片临时路径已不存在，仓库相关R16目录也未找到该照片副本。

## 本次新增的可复核证据

执行：

```powershell
wsl.exe -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/audit-hakios-ios16-display-goal.py
```

结果：退出0。原始结构化证据为 `display-goal-audit.json`。检查器只读本地
包/源码/保存日志，短暂将包内FAT/EFI放在WSL临时目录核验，自动清除；
不产出启动包，不包含固件字节，不调用设备入口。

1. 重新校验R15/R18冻结Android镜像的哈希，并解压、核对其中的XNUP。
   R18的实际cmdline仍与R15逐字节相同，保留`bootcpu=1 cpus=1`、
   `fbwidth=750 fbheight=1334`、UFS/USB/软件渲染/返回计时全部参数。
2. 直接从**镜像实际内嵌FAT**重新读出`HAKIOS.EFI`，与冻结loader哈希和
   文件逐字节匹配，含`LITHIUM DISPLAY LOADER phase=before/after`标记。
   不是只检查旁边一个未确认被打包的EFI文件。
3. 当前WSL的`xnuloader/loader.c`和`userland/src/inject_sb.c`仍匹配R18
   合同的两个源码哈希。当前port相对冻结R18仅有4个已解释的外设准入
   新文件；所有display include与R18保持相同。R18包不是后来默认关闭
   外设内核的包，两者哈希不能混用。
4. 重新解析R16已保存console，实际三个样本均为：

```text
sample=0/1/2
UIScreen bounds=0,0,250,444.667 scale=3
native_bounds=0,0,750,1334 native_scale=3 valid=1/1/1/1
```

对应原日志行4925、5472、7311；DT行91–93显示registry已经从
`3/3/2436`改成`2/2/1334`，MG行4936仍为scale3。
`ScreenDimensions`在行4933明确不可用，不能填入一个推算值冒充实测。
R16确实存在用户态消费结果与registry配置不一致，不能记作修复通过。

## 并行续查与主代理独立复核

Lovelace完成有界只读调查，交付`display-scale-source-review.md`，已结束；
独立研发耗时未记录，不用等待工具的时长替代。该子代理只写自己的报告，
没有修改共享文件，无合并冲突，未退回重做。主代理抽查后接纳其有边界的
结论，不将私有framework取值机制记作已经查明：

- 原R16日志行1315已经是`STATUSBAR scale=2 subtype=1334 native_profile=1`；
  行1449才记录MobileGestaltHelper启动，行1716的backboardd却报告
  `MG scale:3 display preferredScale:3`。主代理独立读取这些原行确认。
  因此不能将问题直接解释为“用户态启动早于registry修正”；也不能把
  helper启动日志当作所有进程第一次初始化MG答案的证据。
- 主代理抽查`upstream/hakios-xnu-kernel/userland/src/mg_test.c:11-46`，
  确认为调用MGCopyAnswer并打印返回值；抽查当前WSL
  `userland/src/inject_sb.c:1382-1386`，确认status-bar fallback只在本地
  registry检查通过时返回。它们没有证明MG的scale答案来自哪个内部来源。
- 主代理核对`ports/hakios-lithium/diagnostics/display-loader.inc:47-80`
  和`patches/0018-lithium-early-display-profile.patch:26-39`，确认R18候选
  将profile选择移到初始DT发布前。原始DT、私有profile、持久缓存或helper
  优先级仍只是待判别的可能性；当前不删除缓存，不增加另一套scale替代。
- 仅所查HakiOS文件中未发现另一个MG scale override或显式缓存seed；
  这个范围有限的阴性结果不代表已排除所有私有二进制或设备Data来源。

收口复核另确认：原总报告、build-evidence、独立外设复核的哈希仍匹配
`final-manifest.json`；14组既有测试所绑定的29个当前源码哈希全部匹配；
本次显示审计脚本语法、脚本哈希与原日志哈希匹配结构化审计记录。
本轮没有改运行代码，因此没有把这些只读复核说成又一次整核构建或真机测试。

## 原始要求逐项核验

| 项目 | 当前证明范围 | 未完成部分 |
|---|---|---|
| 1 完整尺寸链路 | 源码/保存日志已列出panel、render、独立stride、DT、MG、UIScreen/window；详R18 audit | 私有framework实际取值/cache分支及R18消费者结果未证明 |
| 2 宽度冲突与原因 | BGR24的3240/3=1080；BGRA32 row3072是独立750宽render的padding；R16真实250pt@3 profile不一致 | 最终视觉因果仍需修复前后真机结果，810不是已测缓冲宽度 |
| 3 最小修复 | R18在loader发布flattened DT前选2/1334；完整保留原参数，冻结包和实际EFI重新通过核验 | 该修复的硬件结果未验证 |
| 4 前后日志 | 实际loader字符串和源码中DT/MG/UIScreen/window/profile日志已存在；R16已有实测 | 缺R18完整前后运行序列 |
| 5 Dock完整 | 无充分证据 | **未验证** |
| 5 标签不遮挡 | 无充分证据 | **未验证** |
| 5 边界内显示 | 无充分证据 | **未验证** |
| 6 标注未知 | 本报告/JSON/R18 audit明确未验证 | 不把host pass升级为硬件完成 |
| 交付 | 文件行号、构建命令、完整参数、日志与拍照角度已在R18 audit | 真机验收仍待允许后进行 |

R18源码修复是有证据的候选，不是已经闭环的根因证明。本轮不再调整
分辨率、stride或profile，否则会在现成候选未观察前引入第二变量。

## 当前不能直接恢复上机的另一项条件

`scripts/boot-hakios-ios16.py:24-32`要求当前inputs和可变证据哈希完全匹配
冻结合同。外设主机工作新增4个port文件并更新了一些主机报告，因此
**冻结R18镜像未变不等于当前普通启动入口的provenance已经重新就绪**。
本轮没有改写合同、伪造旧报告、绕过白名单或削弱启动检查。

该入口默认也会查询设备，不能拿“不带--boot”当成本轮许可的离线检查。
本轮没有运行它。后续用户允许设备操作时，还需先按正常流程恢复/整理
R18的源与证据闭包，或审查并冻结一个保留R18显示修正的新集成修订。
本次新增独立离线审计证明冻结字节和显示源码关系，**不发放启动许可**。

## 后续唯一需要的显示判别证据（当前不执行）

- 同次运行的`LITHIUM RUN A ... affinity=0x1 ... match=1`。
- 加载器`LITHIUM DISPLAY LOADER`及内核`LITHIUM DISPLAY DT`前后，
  再对照backboardd MG/initial main screen、SpringBoard的MG/UIScreen/PROFILE。
- 若loader/kernel均2而MG/UIScreen仍3，继续追私有取值源/cache；若都2且
  375x667但Dock仍裁切，则把调查转向实际view布局/identity/safe-area。
  两种情况不能提前选一个当成事实。
- 真正到主屏后正对拍摄，保留四边、顶栏、完整Dock和底部留白；必要时
  补底部两行图标近照。只有该结果能验收三个视觉条件。

活动目标继续保持未完成；当前禁止设备操作，不能用主机报告替代最后验收。
