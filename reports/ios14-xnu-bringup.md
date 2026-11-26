# XNU7195 / lithium 实机启动进展

更新：2026-11-13。按用户最新指示，已停止 QEMU 主线。第一验收为 MIX 真机 XNU 自身入口和初始化日志，第二验收为正常引导 iOS14。路线见 PLAN.md。

## 当前源码构建

Apple 源码固定7195.141.2 / 776661b72c2db9861865df68d309f6f35faccff4；ios-sailfish固定6f27e41ff249f2680540ebe8ee5ab982c5e641c9。上游保持独立，修改只应用到 WSL 隔离工作副本。

完整 DEVELOPMENT_ARM64_LITHIUM 已编译并链接，实际加入 BearSSL SHA1、HMAC-SHA256 PRNG、单启动 CPU驱动及同代 libpthread454.120.2服务。SHA1的88组独立摘要检查通过；这些服务的完整硬件行为仍待真机运行。

新增0013补丁为真实 XNU 物理入口提供白色 BGR24条带；arm_init、CPU初始化、页表切换、pmap_bootstrap、serial和interrupt平台提供编号文字；panic在CPU调试状态建立前输出。控制台使用MIX实测1080×2040、stride3240、PA0x83401000。早期TTBR0和最终低全局区旁的Normal-NC别名都覆盖真实屏幕，避免撤销TTBR0后继续写物理地址。

首轮实机内核SHA256：38a7cfa8a32290ac3922e7e02a799e7446039e87582b00eddc25df6d3355cd14。构建记录见ios14-xnu-lithium-kernel.json；静态指令记录见ios14-xnu-lithium-instructions.json。完整构建、静态检查和打包均不证明真机执行。

## 实机加载路径

采用原厂启动链 → 小型ARM64 shim → XNU，复用参考项目的start、ADT、payload、loader和console；不把UEFI或原厂Linux恢复作为前置门槛。首轮单核、512MiB内核管理RAM，不携带iOS根镜像。输入范围、段地址和哈希见ios14-native-boot-build.json；启动记录见ios14-native-device.json。

上方RAM窗口[0x100000000,0x120000000)来自实际FDT；固定保留区、动态池分配窗口和已有XBL/aboot PT_LOAD均不冲突。并未取得完整bootloader堆分配图，运行失败仍须结合真实日志定位。

## iOS14材料

用户本地IPSW支持iPhone7/8，选定iPhone10,1 / D20AP。实际kernelcache为7195.140.44 / RELEASE_ARM64_T8015，ARM64 subtype0，237个预链接组件；LZFSE解压和Mach-O结构检查已完成。其完整SHA256、原始设备树与OS根镜像成员见ios14-local-inputs.json。旧n71ap输入保留独立，不能混用机型。

根镜像仅已识别，未挂载、未开始launchd或SpringBoard。必须先取得真实XNU日志，再推进根文件系统和iOS14进程。

## 历史边界

旧QEMU结果曾辅助识别SHA1/PRNG/CPU/pthread源码停点，现仅保留历史文件；不会再运行，也不作为实机启动证据。旧“SHA1待补”“内核构建待完成”状态已失效，当前问题是实机进入XNU及其真实停点。

## 首轮真实结果

2026-11-13 01:15（Asia/Shanghai），fastboot boot已接受镜像a2df440f37c55b4b3e0930ddcc21cd48cf11747f76348bcd65227294ba009026。用户照片内BUILD=38a7cfa8a32290ac，与记录的内核哈希对应；XNU自身01—05/07日志可见，最后为entering arm_vm_init。N1已取得实机证据；N2页表/physmap初始化尚未完成，没有iOS用户空间。用户随后已长按电源回到Android，ADB重新核验可用。第二轮仅补VM01—VM22细分检查点与日志显示修正，用来定位此真实停点。
