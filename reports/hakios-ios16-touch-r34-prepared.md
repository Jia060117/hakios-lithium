# R34：完整触摸读取与输入投递候选

2026-11-20。按用户要求减少重复审查和总回归，本轮直接实现并构建生产路径，只验证新增模块、实际编译和最终镜像。没有手机操作；触摸效果尚未真机验证。

本轮把 `touch=1` 从占位日志改为真实 FT8716/FTE716 输入生产者：

- 使用 lithium 原厂 DTS 的 QUP6/I2C12、地址0x38、GPIO87/88、GPIO89复位与GPIO125报告输入。原厂 DTS 的通用 interrupts 字段为GPIO105，但驱动 `gpio_to_irq(pdata->irq_gpio)` 使用GPIO125；本轮采用GPIO125读回并轮询，不修改GIC路由。
- GPIO89输出低至少10ms，再输出高等待300ms，随后读取0xA3芯片ID。低/高时序来自原厂 `ft5x46_probe` / `ft5x46_hw_init_work`；原诊断路径没有执行这一步。这是值得直接验证的初始化补齐，尚不能宣布它就是R32 NACK的根因。
- 实现每次有界QUPv2组合寄存器读取、错误保留及控制器RESET。63字节报告加两字节开销超过64字节FIFO，因此接收期间持续排空，避免等收满后才读取。
- 将最多10点的按下/移动/抬起批量送到真实 `hakios_touch_post_batch()` → `/dev/hakios-touch` → backboardd digitizer。重复报告不重复点击、消失触点释放、坐标集中映射到0..65535。
- 使用约16ms轮询间隔；先推进可用输入，IRQ中断接入后续再优化。定时结束前停止轮询和释放触点。

电源继承已亮屏环境，不修改L14/LAB/IBB电压或下载、升级触摸固件；原厂代码是有驻留固件的按需升级路径。供电继承是否足够、真实ID和报告数据仍需真机确认。

## 已完成验证

实际完整HakiOS内核增量编译及链接成功，保持8796.121.2/arm64和原功能；上游已有kext symbol-set末尾失败仍按原构建器规则识别，并未通过删除功能处理。最终内核SHA-256：

`799bdcec2e72fc6f3a0d34c810431254687ba5ef772566e851a97956fa60650c`

实际PID1重编译完成，RAM诊断支持 `touch=1 touch_diag=1`。APFS检查为clean，486项文件读回与实际暂存目录一致。QUP新增91个针对性用例、事件新增4组针对性ASan/UBSan测试通过；打包器RAM/UFS合成检查和两份真实最终镜像逐层读回通过。未重复运行原52套历史外围回归，也未重建未改动的Mu/loader/系统库和服务。

构建命令：

```powershell
pwsh -File reports/local/run-touch-live-build.ps1 -Task prepare
pwsh -File reports/local/run-touch-live-build.ps1 -Task kernel
pwsh -File reports/local/run-touch-live-build.ps1 -Task root
```

第一轮root暂存复制遇到原目录的符号链接，已改为保留链接后构建；失败记录和暂存保留。最终包核验见 `evidence/touch-live-20261120/main/package-final-check.json`。

## 两份候选

| 用途 | 镜像 | SHA-256 |
| --- | --- | --- |
| R34 RAM短测：ID/报告/输入队列，180秒后自动回Fastboot | `artifacts/hakios/lithium-ios16/phase4-r34-touch-live-ram-final/lithium-hakios-ios16-r34-touch-live-ram.img` | `d1bbc6bf94defeacacf3ffbbae32bd3e227b70ef74bb1ceda2fb5539f8ad0d3b` |
| R34 UFS桌面：R20原参数加真实触摸 | `artifacts/hakios/lithium-ios16/phase4-r34-touch-live-ufs-final/lithium-hakios-ios16-r34-touch-live-ufs.img` | `47740664f56fcf021096493444cc96934223f49ce0dfc099920bfb73cfc426ed` |

RAM包不挂载或写入物理分区；继承R33隔离Mu与返回机制。UFS包保留R20完整系统/数据参数，正常运行可能写入userdata；本轮没有启动任一候选。UFS自动返回仍不支持，没有擅自开启。

下一次真机仅需一轮RAM短测，同时核验 `live reset`、`live id`、`live ready`、`report`、`event` 或第一条 `read_error`。如果读取成功，可直接使用已经构建的UFS候选检验界面，不再重复仅初始化的诊断轮次。用户授权并确认Fastboot后，才进行对应单次临时启动。

## 分工与保留

Descartes实现QUP传输和91例测试；Huygens实现实际输入队列绑定、坐标/触点状态和4组测试；Avicenna核对原厂复位/固件管理关键流程；McClintock实现精简RAM/UFS打包；主代理完成板级MMIO、后台轮询、PID1与返回停止接入、实际内核/init/APFS构建及双包实际读回。

新增代码为 `peripherals/touch-live.inc`、`touch-live-qup.h`、`touch-live-events.h`；共享集成在 `peripherals/kernel.inc` 和新增 `patches/0024-lithium-touch-live.patch`。各模块源码与测试均由主代理读取并核对接口，实际编译覆盖整条集成路径。R20/R32/R33冻结镜像与原Git索引SHA均未改变。回退补丁见本报告同名 `.patch`，不含固件或其他供应商二进制。
