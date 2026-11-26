# A530 G2 TRUST直接访问实验01：未验收

对应实际运行`gpu-g1-power-clock-04`，2026-11-26 03:02:56 +08:00接受一次RAM临时启动。
本轮自动返回Fastboot失败：03:07唯一设备核验为空，用户报告停在总结界面。

实验请求是在已验G1流程里加入`gpu_trust_zero=1`，计划对`0x00b3d000`执行读、写0、读。
实际保存流只有combined begin，没有TRUST或组合阶段终态，因此不能确认实际执行到哪一步，
也不能判断是MMIO访问停滞、异常返回、后续清理失败或其他原因。当前未获得总结页原图。

已收216帧/60536raw/53624console独立重放一致；没有正常timer、四路stop、源丢终计数或drain证明。
接收器由主机核验后的stop请求结束，状态host_capture_complete，不是设备正常断开证据。
软件回调返回前后的时间检查不提供对不返回MMIO指令的抢占恢复能力；现有源检查不能证明
本轮发生了这种情况。不能把2000µs软件预算或180秒内核定时器宣称为所有故障的自动恢复保证。

保持04失败记录和03独立成功记录；不重复直接TRUST实验，不放宽返回条件。
下一步先保存总结页的panic/caller/retained reason文字，按实际原因修复。
PAS13仍为另一条未上机验证的候选，不把本轮未完成等同于TZ已拒绝或整条GPU路线不可行。
本轮未验证CP非安全运行、fence、绘制或Metal界面加速。

完整运行数据：`reports/hakios-ios16-gpu-g1-power-clock-04.json`；
原始流：`evidence/gpu-metal-20261123/package/runs/gpu-g1-power-clock-04/`。
