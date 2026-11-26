# HakiOS lithium 第七轮真机测试

2026-11-14 21:29:33（北京时间）核验授权 MIX 的身份、解锁和 userdata 容量后临时启动，Fastboot 返回 OKAY。前三张用户照片直接确认完整 HakiOS 内核与上游最小用户程序运行：

```text
Hello from user space on Darwin!
I am process 1, uid 0, console fd 0.
kernel.ver=2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty
BSD root: md0, major 1, minor 0
```

`dlil_init` 显示初始化线程至少被调度一次；第二、三张照片分别有一条、两条 memory 日志，free/available 469 MiB、wired 42 MiB。用户另确认“仍在该画面，日志继续增加”。这证明本轮用户程序和周期回调取得进展，不能单凭此宣称实际timer IRQ计数、精确睡眠时长或10分钟稳定验收通过。上游回调名义上每60秒输出memory；照片拍摄时间及真实周期未独立核验。

随后用户报告“突然崩溃了好像，花屏了”。第四张照片显示 `CORE 0 is the one that panicked`、`STACKSHOT SUCCEEDED`、`END OF LOG`、`LITHIUM TEST ENDED USE POWER VOLUME DOWN FOR FASTBOOT`，说明panic后已走回loader并停屏。具体panic原因行未拍到；没有观察到自动重启。用户后来明确确认已用电源＋音量下返回Fastboot并保持连接，供第八轮复测。

本包参数为 `cores=1 serial=3 -v hello_wait=600`。核对上游 `userland/src/hello.c`：它循环600次调用一秒select，随后主动退出PID1；上游明确注明这会panic并结束测试。因此测试到期退出是首要解释，尚未由同次原因行证实，select返回及实际等待时长也未捕获。返回日志把panic/stackshot字节逐字输出到文本控制台，可能造成乱码；具体显示损坏原因待下一包验证，不据照片认定GPU或DDR故障。

镜像 SHA256：`0cec5c9c5ac2921e24438959e50a55a99a34f64477655d7eb39f10c86d3f730c`。固定HakiOS提交 `63fc2b4633bac3af8e57dadab9f5a8d4cb940438`，XNU8796.121.2；内核 SHA256：`02357c2cdb8b6d3cf700071adf3a77d2987667bc2a238bfc5383bdb907cd6698`。两处交接栈清理限定到 `[0x41590000,0x415d0000)` 已通过真机交接。UEFI/loader完成由本轮内核运行及返回路径推出；未把第六轮照片当作第七轮直接证据。

四张原照片与源码对照已保存到本轮local日志及冻结目录，路径/哈希、证据类型和验收限制见 [机器记录](hakios-device-r7.json)。原启动输入不变。构建、ARMv8指令/栈契约检查、来源审计与9项回归此前通过。

hello经mockfs作为 `/sbin/launchd` 运行，只验证平台。屏幕iBoot版本字段和AMFI/Sandbox stand-in日志不证明实际iBoot或iOS14启动；`MiniIOMobileFramebuffer: no usable boot framebuffer (1080x2040, depth 24)` 仍是图形限制。P1的10分钟×3次、iOS14、A530 GPU和触摸均未通过。七轮没有分区写入、旧7195执行或QEMU。
