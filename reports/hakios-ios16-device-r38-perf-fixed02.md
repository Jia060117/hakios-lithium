# R38 perf-fixed02 / frequency11 真机结果

**本轮照片确认：primary PLL 已报告 LOCK，严格读回比较将 ACTIVE bit30 当成配置变化而中止，尚未切换 MUX 或启动 XNU。** 2026-11-21 12:15:48（上海时间）Fastboot 接受镜像 `1a34e8be1e008a469828e01109841254791e07fe66292b701e1d914569cc0d1c`，大小 16334848 字节。隔离 RAM、三核、沿用 frequency10 的启动参数及内核。无分区/刷写/擦除/ADB/QEMU 操作，无重试。累计 HakiOS 74 次、iOS 16 43 次。

USB 到 12:18:46 未枚举，0 帧，未收集到正常定时退出或 USB 排空记录。12:19:52 唯一一次 `fastboot devices` 枚举为空，因此未查询 product；没有再发送设备命令。USB 没有覆盖前置阶段；用户后续照片补齐了 PLL 实际写入次数、MODE 与受控中止终态，见下文。实际 CPU MHz 未测。

上一轮的照片已确认 UEFI `LOCK_WAIT` 中发生 `READBACK_ERROR`，10 次初始写入完成，未切换 MUX 或进入 XNU。本轮只新增 `LITHIUM PERF UEFI LOCK` 的原始 MODE 读回证据，仍要求相同的供电、停核、严格掩码及锁定位检查；前置异常保留屏幕，正常进入内核后才走定时 Fastboot 返回。构建、Mock 和包读回见 `reports/hakios-ios16-r38-perf-fixed02-prepared.md`；原始运行目录 `reports/local/r37-attempts/r37-frequency-11`。

原始屏幕已经保存，MODE 原始值已补齐。四核目标仍未完成，phys0 保留为驻留服务核。

## 本轮照片更新（2026-11-21）

用户原图已归档。有效 `LOCK` 行是 `polls=1 valid=1 first=0xc0118000 last=0xc0118000 expected=0x00118000 mask=0x7ffffff8 mismatch=0x40000000 elapsed_us=51`。末行 `core_status=4 phase=6 pll_attempts=10 core_attempts=10 launch_aborted=1`。原件 SHA256 为 `b5de3f455f8bd2853c657ad1771202ff56056856faa6cc33d4986ed86232768e`。

所以现在可确认：第 10 次写后的 51µs 轮询读到了 LOCK(bit31)=1，唯一受旧掩码影响的额外位为 bit30；软件先执行配置相等检查而中止，未执行第 11 次 MUX 写，也未启动 XNU。`core_not_programmed` 是检查中止标签，不能解释为 PLL 未锁定。频率结果 0 仍是流程未完成的默认值，非实测 0Hz；`fault_pa=0x06480044` 是最后一次读地址，非总线 fault 证明。本条取代上文“需当前屏幕确认”的未知状态。

Linux 的 msm8996 PRIMARY `perfcl_pll`（prim_regs，offset0x80000）绑定 `clk_alpha_pll_hwfsm_ops`；该接口把 MODE bit30 定义为 ACTIVE_FLAG，其 enable/is_enabled 都查询它。依据实际读回与同款 primary 实现，下一修订只把 bit30 从固定配置比较中排除，继续独立要求 LOCK。所有供电及停核门禁、其余控制位和寄存器读回、超时和写入顺序保持。不是套用 alternate PLL 的字段。实际 MHz、MUX 切换后状态及性能仍需下一次运行。
