# R38 perf-fixed04 / frequency13 单次真机结果

**本轮照片补齐最终快照：全部11项有效，唯一失败项为CLKCTL，实测读回0x40040071，旧谓词要求0x71。最终MODE报告LOCK、MUX保持4，其余PLL字段全部匹配；受控停在UEFI，未启动XNU。** CPU性能和有效MHz尚未测量。

2026-11-21 12:47:15.420059（Asia/Shanghai），Fastboot接受冻结镜像 `fbfd63818fbcdadd03bc7662fe3d3f9c2a184a412518e0d5d957c77ef9beb179` 一次，16334848字节。5项只读预检通过，USB接收器先启动。独立RAM/ufs0/三核/180秒+正常返回；沿用前版core/内核/RAM卷/全部参数，仅新增最终11项存储值输出。本轮没有重建或修改源代码，没有分区/刷写/擦除/ADB/Android/Recovery/QEMU或重试。累计HakiOS76 / iOS16 45次。

USB于12:50:14.194641等待超时，未枚举/0帧，无console/frames文件。没有数据可重放，不能把缺口计数0解释为日志完整。12:51:16.545940，接受后241.125秒，只执行一次fastboot devices，枚举空，未执行product查询，也未重查。无本轮正常timer/drain或Fastboot复位终点证据。仅凭空USB/空Fastboot不能确定UEFI阶段、内核panic或硬件挂死。

请先保留当前画面，拍清 `LITHIUM PERF UEFI snapshot phase/valid/expect_mux/expect_alt`、三行 `LITHIUM PERF UEFI raw`（mode/l/alpha/user；cfg/cfg_hi/test/test_hi；alt/mux/ctl）以及END。只有valid相应位置1的raw可作本次读取，部分快照的未置位字段可能含前一次数据。本包在前置异常时保留屏幕，进入XNU并正常计时退出后才走自动Fastboot；不能把前置未知状态归因于复位失败。

原始attempt目录 `reports/local/r37-attempts/r37-frequency-13`，准备报告 `reports/hakios-ios16-r38-perf-fixed04-prepared.md/json`。原索引、R20和旧冻结保持；频率正常和四核目标仍未完成。下一步只依据本轮final原始值修订，不在缺失原值时更改掩码或电压。

## 本轮原图及精确失败项（后续补证）

原图SHA256 `299b1cda6b1b32ebe0823e6531bd3026218f2a47fdf70dc94f7f4220afeaf605`，已存同次attempt的screen-terminal-original.jpg，转录见screen-terminal-evidence.json。

```text
snapshot phase=8 valid=0x7ff expect_mux=4 expect_alt=0
MODE=0xc0118000 L=0x20 ALPHA=0 USER=0x109
CFG=0x200d4828 CFG_HI=6 TEST=0x1c000000 TEST_HI=0x4000
ALT=0 MUX=4 CLKCTL=0x40040071
```

LOCK位31在这次**最终**MODE中也置位；配置掩码比较无误，MUX最后读回4，各控制字段匹配。只有CLKCTL全值比较失败，差值为0x40040000，即位30及18。不得把MODE的ACTIVE名称套给CLKCTL，当前无对应硬件字段名的证据；不把这次变化宣称为错误标志或已测CPU频率。

之前缺失final值造成的未知状态由本条更新。下一修订仅在expectedMUX4的最终谓词中，允许精确的旧状态0x71或这次完整捕获0x40040071；初始、每写之前和切换前状态仍只接受0x71，其余32位组合仍拒绝。无CLKCTL写入、无电压/PLL参数更改，保留LOCK和全部其他字段检查。这是同机、同bin/revision的受限实验profile接纳实测状态，新增位硬件语义仍未验证，不扩展为通用驱动结论。实际频率/性能、XNU及四核仍待真机验证。
