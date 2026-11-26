# R35 三项合并配置：已打包，等待Fastboot

2026-11-20，Asia/Shanghai。用户反馈R35 lite开机仍点不动，要求把前述另外两项也加上试。已说明：本轮lite本来就同时含 `applog_ui_quiet=1 ui_lite=1`；按之前quiet/lite/cpu3三项合并解释，新增变量实际为 `cpus=1 → cpus=3`。

合并镜像已用同一个冻结R35内核和R34 APFS重新打包，完成既有打包器的实际逐层读回；没有重新编译内核、重建用户态或重复历史测试。

- 镜像：`artifacts/hakios/lithium-ios16/phase4-r35-ui-perf-combined/lithium-hakios-ios16-r35-ui-perf-combined.img`
- SHA-256：`bfc3a041b7d81f33582dae04ba9894c53b3b44f16e1b5f70ba10b1672500370d`
- 内核SHA-256：`86f36fdf4e047e0f2da18138a39629e584975e4c6689036946ae71195bd924aa`
- 完整参数及来源：同目录contract.json。本包为 `bootcpu=1 cpus=3`，含 `applog_ui_quiet=1 ui_lite=1`；其余沿用已启动lite包。
- 维持原UFS/Data写入、R20显示和真实触摸；未启用UFS自动Fastboot，未写PLL、电压或缓存策略。两个历史慢核是否改善仍未验证。

## 本次预检结果

`reports/local/20261120-r35-ui-perf-combined-approved/commands.json` 保存唯一一次只读 `fastboot devices`。返回码0，但枚举列表为空；未进入product/解锁/电量等后续核验，未启动USB新接收器，**没有发boot或消耗新的临时启动次数**。

计数保持HakiOS61/iOS16为30。没有ADB、刷写、分区、复位或自动重试。保留当前lite运行及其原记录，不从空枚举推断线材、电量、手机故障或开关因果。

用户已被提示手动返回Fastboot。下次用户确认进入后，复用此冻结包，在新独立预检记录下完成对应单次尝试；不覆盖当前空枚举证据、不复用已消费的lite启动意图。
