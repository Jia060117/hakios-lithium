# R36 — 三核下保持真实触摸初始化和轮询

主机候选已完成，尚未上机。R35 combined 的触摸被旧单核准入条件拦截，
本修订修复这一集成问题，不宣称慢核、调频或桌面性能已修好。

新增默认关闭的 `touch_smp=1`。初始化和每次轮询先查询 logical0 是否在线，
保存原线程绑定，绑定并迁移至 logical0，实际核对 affinity=0x1 后才访问控制器；
成功、早退和核对失败都恢复原绑定。Stage1 增加仅供 live SMP 的显式上下文，
旧诊断及默认单核限制保留。I2C 事务、GPIO 值、坐标和事件协议不变。

关键文件：
- `ports/hakios-lithium/peripherals/touch-cpu-scope.h`：绑定/核验/恢复。
- `peripherals/kernel.inc`：仅显式 live SMP 初始化入口。
- `peripherals/touch-live.inc`：初始化和每次 poll 包装。
- `peripherals/touch-stage1.inc`：显式 SMP 上下文，旧包装保持。
- `peripherals/boot-options.h`：严格解析，默认关闭。

实际完整 HakiOS 活动内核增量编译链接通过；未重建未改的 Mu、loader、用户态或任务。
首次编译曾因 BSD 下 processor_t 不透明及私有函数声明失败，已改用 processor_info，
根据实际 XNU 源码核对五参数签名，并重新完成实际编译；失败日志保留。
Nietzsche 运行当前源码的 68 项专项（scope24/parser30/Stage1 14），
Clang18.1.3、warnings-as-errors、ASan/UBSan 通过。主代理独立核对其引用源码哈希、
初始化/每次 poll 的恢复路径及实际内核输入；没有复跑历史总回归。
Harvey 提取三核记录；主代理复查实际保存日志，慢核仍约65倍差距，原因未验证。

构建命令：
```powershell
pwsh -File reports/local/run-touch-smp-r36-build.ps1 -Task prepare
pwsh -File reports/local/run-touch-smp-r36-build.ps1 -Task kernel
wsl -d Ubuntu -u jia -- python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-touch-smp-r36.py
```
专项复现命令见 `evidence/touch-smp-r36-20261120/test/review.md`。
实际整包及各层写回读验通过，命令行预留41字节。

镜像：`artifacts/hakios/lithium-ios16/phase4-r36-touch-smp/lithium-hakios-ios16-r36-touch-smp.img`
SHA256：`d4f2d0ee4f4be24952d7439b17834d0d159fa76e78da40bec59622b6623b5669`
内核 SHA256：`b36b86bd944f1a6bd871d8e175c1e65941edab00b0fb9fcf17a00247bd9d9daa`

```text
bootcpu=1 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd,SpringBoard loader_return_after=10800 usertraps=1 lithium_sb_watch=1 sb_delay=8 epoch=1794940756 ustacks=SpringBoard ustacks_at=180 hfsdiag=1 cpu_sanity=1 hfs_bench=1 touch=1 touch_log=1 touch_poll=1 applog_ui_quiet=1 ui_lite=1 touch_smp=1
```

合并配置保持 quiet/lite/cpus3，仅新增 touch_smp=1。
UFS 正常启动会写 Data，lite 偏好可能持久化；不刷分区。UFS 自动返回仍关闭。
预期日志为 `LITHIUM TOUCH smp context=init` 和 `context=poll`，
两者 logical0/affinity1/bound1，然后 ID0xe7、ready、DOWN/MOVE/UP 和用户实际点击。
主机测试不证明硬件迁移/时延、三核收益、缓存命中或调频。
本阶段没有新的设备命令，计数保持62/31。当前R35运行保留，不能自动重试旧启动入口。

风险顺序：其余两核仍非常慢，可能拖慢关键 UI 线程；其次绑定迁移的运行时开销；
软件合成耗时仍在。若核绑定或ID失败，保留精确日志，不猜硬件原因。
四核仍需先解决 phys0 被 UEFI/UFS worker 占用，未擅改存储交接或PLL/电压。
回退补丁与 checkpoint 与本报告同名；现有冻结包和 Git 索引保持。
