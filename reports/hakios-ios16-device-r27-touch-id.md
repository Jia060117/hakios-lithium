# R27 真机结案：事务前门禁停止，正常 timer/USB 排空

记录：2026-11-19T22:35:25.154448+08:00。本轮只读Fastboot核验5条、临时boot一次获接受；无刷写、分区访问、ADB、Android/Recovery、QEMU或重试。

## 结果

- 实际CPU为logical0/MPIDR0x80000001/affinity0x1/max0，bootcpu=1 cpus=1，match1。
- Stage1成功：26读/3写/8映射、313us；时钟/引脚检查通过，STATE0x11c、VERSION0x20060000、I2C_STATUS0x0c000000。三写仍为apps分支和GPIO87/88，无QUP事务写。
- ID适配层重新检查九项板级字段，GIC入口三项读回通过（当前访问视图INTID138 enable/pending/active的bit10清零）；此观测不证明安全世界所有权。
- 新核心仅执行4次QUP读：HW_VERSION、I2C_STATUS、ERROR_FLAGS、OPERATIONAL；8us内看到OPERATIONAL=0xc0，被当前已知位掩码拦截。QUP写0、未发起I2C、未读取芯片ID。

```text
LITHIUM TOUCH id result status=malformed reason=unexpected_operational_bits raw=0x00 complete=0 plausible=0 clean_entry=0 stop=0 quarantine=1 elapsed_us=8 reads=4 writes=0 polls=0
LITHIUM TOUCH id registers version=0x20060000 state=0x00000000 status=0x0c000000 operational=0x000000c0 core_error=0x00000000 bus_error=0x00000000 config=0x00000000 io_modes=0x00000000
```

**注意：raw=0不是读出的ID；state/config/io_modes的0也是此次核心尚未读取的初始化值。** 不得据此断言控制器ID为0、STATE变为0或配置已清零；同次Stage1真正采到的STATE为0x11c。`malformed`是本模块软件分类，不是已经收到畸形I2C报文。

## 终态与日志

- 原PID1进入RAM诊断保持；180秒timer正常到期，成功drain1及target11706正文已收集。保留屏幕尚未目视确认。
- 51帧，控制台11755字节、原始帧13387字节；独立解码逐字节一致，首偏移0；缺口、重复、残帧、设备lost和源争用丢弃均0。
- Pipe error出现在成功排空正文之后；原始接收器状态保留为capture_failed，审计另行证明已输出内核流覆盖至timer/drain。没有扩称全部固件/daemon原始printf日志完整。
- DATA SYNC error2/lookup与隔离RAM包无/private/var一致，未执行到VFS_SYNC或设备flush，不据此归因UFS。

## 同轮并行来源复核

Planck完成 `evidence/touch-r27-20261119/runtime-op-c0/findings.md`；主代理独立读取固定Linux Git blob第58–68行并核对本轮4读停点。该I2C来源命名bit6为`QUP_OUT_FULL`；bit7在所查I2C定义中未验证。二者不是bits10/11的max-done标志。`0xc0`在RESET下是否正常、是否代表实际FIFO内容仍无证据，不据此放宽门禁。日志`irq_disabled=0`也来自未做终态读回时的false结果，不代表观测到IRQ被启用。

## 下一步

- 先按固定原厂/Linux来源解释OP bits6/7，并核对该QUP版本下的RESET/FIFO含义；保持未知项未验证。
- 在任何事务寄存器写之前，完整采集STATE、CONFIG、IO_MODES、FIFO计数/容量、计数配置、错误/IRQ掩码，再对照原厂初始化。
- 不为通过门禁直接扩大掩码、清未知位或强行复位；若需要QUP SW_RESET，应作为独立修订明确说明，不把它与触摸GPIO复位混淆。
- 本轮芯片ID/触摸未通过，不自动重试R27；R20显示、长期UFS系统及冻结R15/R20/R25/R26保持。

## 代码与回滚

- 主机工作、四个并行代理分工及主代理复核见 `reports/hakios-ios16-touch-r27-prepared.md/json`；本次运行后的源语义复核单独保存在runtime-op-c0目录。
- 主机准备补丁与本结案补丁分别做精确正/反向恢复，真实Git索引未修改。
