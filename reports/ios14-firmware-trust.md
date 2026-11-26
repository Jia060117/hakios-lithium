# 第十四轮平台身份异常与固件信任缓存

2026-11-14。第十四轮照片证明 HFS 根挂载和 AMFI `0x5a` 查询成功，随后出现 `userspace panic: boot task is not a platform binary (fsck, /sbin/fsck)`。

对应启动内核 SHA256 `319bd7dd2fc2ac51164ebfeb68e4b70afbb4fc626e77cee1f6af7178070794b1`，镜像 SHA256 `ebd69b72ff2e7e6b6a0a1f657a249585090632ce56454c7b48e93c04f91739e1`。照片和冻结输入位于 `reports/local/20261114-161750-210922-native-xnu`。

## 故障依据

真实 iOS14.8 `/sbin/launchd` 的 `0x100032d30` 调用 `posix_spawnp`；返回成功之后，`0x100032d68` 调用 `csops(CS_OPS_STATUS)`。失败或返回标志第26位 `CS_PLATFORM_BINARY` 未设置，都会进入 `0x100033100` 并输出照片中的报错。这证明 launchd 的启动任务代码已执行；不证明 fsck 主函数已运行，spawn 可能仍处于挂起状态。

旧适配器仅提供 dyld 查询，没有 vnode 签名策略回调。XNU 的通用 UBC 代码计算 CodeDirectory 哈希并设置 CS_VALID，但固件的 ad-hoc CodeDirectory 原始 flags=2 不包含平台身份。`csproc_get_blob` 还要求进程有 CS_SIGNED；缺少该位时，即使 blob 有平台标记也无法被进程查询到。

## 固定输入和新增适配

使用同一 IPSW 内的 `018-62062-016.dmg.trustcache`，SHA256 `38cfec2c89fa5c186aee2c02e143b9ac449cf18f9e925d60a03c4db45c4b7118`。整个 IM4P 的 SHA384 与 D20AP Erase 身份 BuildManifest 的 StaticTrustCache Digest 一致。IM4P 未加密，payload 为 v1、2181条22字节记录；其中两条是完全相同的重复记录。固定输入校验不是另行验证 Apple manifest 签名链。

新增 `0025-lithium-firmware-trust.patch` 包含缓存原始记录及独立成员查询；现有 AMFI 策略保留早期注册时机，并增加一个 `mpo_vnode_check_signature` 回调。只有已有 CS_VALID、20字节 CDHash 和算法都与普通记录(flags=0)匹配的 blob 获得 CS_SIGNED、CS_PLATFORM_BINARY、CS_HARD、CS_KILL、CS_ENFORCEMENT。特殊 flags=1/2、未知哈希和不匹配算法没有新增权限；未提供 entitlement、凭据或任意路径白名单。

信任缓存中 fsck 的 CDHash 为 `07e565101db5c7bc7232f2c5de915fb812544d03`，launchd 为 `5e1e0f6e9669f156a8977e8e32e6e1a1f9a0f2dd`，dyld 为 `08c9c81d4fc3356a1f476e63ee3686a40c4d723f`，均为 SHA256 普通记录。XNU 通用签名解析、页哈希校验、执行标志传递和无效页处理源码保持原样。

## 验证与边界

主机测试运行实际成员查询和签名回调，并测试2181条缓存记录、43620个修改哈希与独立线性查询结果、全部算法值、空参数、输出边界和原标志保留。另从真实 XNU 提取 `cs_invalid_page`，验证标记后的进程面对无效代码页时拒绝映射并请求 SIGKILL；锁和信号依赖使用模型。使用 ASan/UBSan。

固件侧重新计算 fsck 13页、launchd 98页、dyld 178页的 SHA256，与各自真实 CodeDirectory 内的代码页哈希逐一比较；修改代码会造成页哈希不符，修改 CodeDirectory 会失去缓存成员资格。此检查不等于运行完整 UBC/pager，真实内核挂钩和标志传递还需要最终链接检查与第十五轮照片。

构建和启动链必须绑定本轮缓存验证报告及源码/补丁哈希。最终报告为 `ios14-firmware-trust-validation.json`。当前实机通过的最高阶段仍为第十四轮 launchd 平台身份检查，不能提前将 fsck 或 SpringBoard 标记为成功。

最终构建与打包检查已通过：kernel SHA256 `6e0731e3d3a128a75b5bdff00a343ac3a0acec057b3222cdd8c30266c517420e`，boot SHA256 `7e9bef39d06305e0a0a136a8594ec0d9d4271de67596fa0bac6ad1927d7c5638`，镜像16367616字节，入口PA `0x101328488`。两项MAC回调均已核对最终机器码和操作表；缓存47982字节与固件一致。原37项AMFI/注册检查、HFS51、SHA25625、MD521、DMA、页表和指令扫描通过。尚未临时启动；先完成误入Android后的根卷完整性检查。

根卷完整读回一致后，已于 2026-11-14T17:06:43.661461+08:00 从Fastboot临时启动第十五轮。Sending/Booting均成功，冻结目录 `reports/local/20261114-170643-581033-native-xnu`；屏幕证据待取得。没有重刷userdata。
