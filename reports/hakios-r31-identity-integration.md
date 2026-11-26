R31 已完成 SpringBoard 早期 XML 权限接口与服务身份初始化的修正，完整 HakiOS 内核、loader 和启动镜像已重新构建、独立解包并冻结。当前状态是可上机验证；没有将构建或主机检查记为 SpringBoard、服务或桌面真机通过。

R30 两张报错照片显示原始 SpringBoard 在 `+[SpringBoardUI load]` 的同一断言处反复退出，服务同时报告找不到 type 6 persona。原始 `launchd[1]` 已完成 early boot，计时中断仍推进，照片未见 kernel panic。缺少 launchd.plist、NRN cache 和部分旧配置键的日志之后仍继续启动；重复的框架断言是目前明确的退出停点。运行证据保持在 [R30 报告](hakios-device-r30.md)。

只读分析原始 18H17 程序签名与共享缓存确认：SpringBoard 签名包含布尔权限 `com.apple.springboard-ui.client=true`，XML 特殊槽哈希与 CodeDirectory 一致。原始调用链为 SpringBoardUI → BSSelfTaskHasEntitlement → BSSecTaskHasEntitlement → BSSecTaskCopyEntitlementValue → SecTaskCopyValueForEntitlement → SecTaskLoadEntitlements；最后以 csops op7 请求 8 字节长度头，处理 ERANGE，再读取 XML。完整 HakiOS 已有原始 XML 解析与权限查询实现，但 XNU 的 op7 路径要求 OSEntitlements 对象，而 MiniAMFI 尚未建立该对象。因此新增适配在对象缺失或 XML 导出不可用时，复用现有 `csblob_get_entitlements` 和 `csops_copy_token`。保留原始 XML 哈希检查、状态检查、audit token 校验、长度探测和原生对象/DER 路径；不修改 Apple 程序或共享缓存，不跳过框架断言，不把权限查询改为全部允许。这是从源码和原始输入确认的接口缺口，R30 没有记录实际查询返回值，仍需 R31 真机确认它是否解释并消除该断言。

完整 persona 子系统已启用并编译；原始 iOS 14 的 kpersona_find_by_type 使用 operation9/系统调用494。上游 HakiOS 的启动流程没有创建 persona，相关注释与 R30 type6 查询失败一致。新适配仅在 `lithium_ui=1` 时、用户任务创建之前，经完整 `persona_alloc`、`persona_init_begin/end` 建立一个 SYSTEM_PROXY 身份，再用真实 lookup 核验。使用内核分配的 ID 和 KAUTH_UID_NONE 元数据；服务凭据与身份采用继续由上游 spawn/persona 实现处理。已有同类身份会复用，歧义或初始化失败会记录错误；不是在查询接口伪造成功。更完整的用户身份、容器或持久化语义仍待系统启动后的真实调用验证。

17 项主机检查直接执行生产适配函数、原始 SpringBoard XML 数据和实际内核摘要函数，覆盖长度探测、边界、拷贝故障、无权限/校验错误、身份重复与分配失败、引用释放、未观测状态以及结果保留；ASan/UBSan 均通过。完整 HakiOS 按正常依赖和构建流程重新编译，实际 kern_proc、bsd_init、sys_persona 对象及最终内核均确认新增调用。上游构建流程按其既定规则识别本配置的 kext symbol-set 检查输出；没有编译或链接失败。APFS 引导根卷清洁且已逐项读回，335 项任务权限保持正确，内容和字节哈希与 R30 相同。现有 UFS/HFSX 系统根卷继续按需读取。

新镜像为 `artifacts/hakios/ios14-ui-ready/20261116-044722-701044/lithium-hakios-test.img`，SHA256 为 `2af949c5c1a8d958720b28e2e3f6b9a54c9e60cd99c83b43e0116652d33d7efd`。788 项冻结文件、R28/R29/R30 历史证据、旧内核冻结记录和一次性启动入口均已核验。本轮没有手机查询、启动或分区写入，累计 HakiOS 临时启动仍为30次；未执行 QEMU 或旧7195入口。资源检查使用已批准的 WSL16GiB/12CPU 与任务14GiB/并发12，任务顺序执行。见 [就绪报告](hakios-ios14-ui-ready.md)、[独立核验](hakios-r31-readiness-verification.json) 和 [函数检查](hakios-ios14-identity-tests.json)。

下一次通过 `scripts/boot-hakios-ui.py --boot` 核验当前授权设备后，仅临时启动 R31 一次。服务、backboardd、SpringBoard 和显示继续同包观察。`LITHIUM IDENTITY FINAL` 会保留 SpringBoard XML 的 probe/copy 次数、字节数/返回值、persona 初始化与真实查询结果；未发生的调用以 -1 标记。结束摘要保留，无需守屏或秒表。首次窗口15分钟，仅新进展时最多延至30分钟；出现候选界面则按既定策略保留。下一步依据实际结果判断是否越过此断言并定位后续停点。可见桌面、物理触摸、A530 加速与完整 iOS 14 兼容均未通过，FT5x46 物理采样仍未接通。
