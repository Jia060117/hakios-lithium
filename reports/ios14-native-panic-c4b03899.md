# 第十一轮：根卷已挂载，SHA256_Init 空函数表异常

2026-11-14。冻结目录 `reports/local/20261114-152016-948715-native-xnu`。
内核SHA256 `c4b03899cba1fe486e7e27770ffcd302a40c4ebe2c5e09797ff571439c54f8c1`，
启动镜像SHA256 `5dad1e4abf6ca3534645ca6b3a8f9b8588e5ea41ab4e6bcaf430860c36a7e9ca`。

照片 `native-xnu-init.jpg` SHA256
`510ec38d4b3da166cc7a70134f55cb9502c6006813476fec8ecb4958a879a873`，
`native-xnu-root-mounted-panic.jpg` SHA256
`873a78413dda28e90281afb16ed44dcc80ebaabf1460fe6f91dd8a1f8eada0d3`。
第一张构建时间15:10:40与冻结内核一致；第二张有本轮新增的只读日志提示。

## 实机进展

第二张照片明确显示：

```text
LITHIUM HFS: clean fresh journal; initialization deferred on read-only root
hfs: mounted iOS14Root on device b(2, 0)
LITHIUM ROOT: vfs_mountroot succeeded
LITHIUM ROOT: root vnode installed; continuing BSD initialization
```

因此只读HFS日志修复通过实机验收，根卷挂载和root vnode安装均已成功。
尚无launchd、dyld或iOS用户空间启动证据，N3其余验收及N4仍需推进。
第十一轮没有新写入userdata，根卷SHA256和首块CRC32 `0x6143e5cf`保持不变。

## Panic与冻结二进制定位

| 项目 | 值 |
| --- | --- |
| PC | `0xfffffff007732818` |
| LR | `0xfffffff007274e94` |
| FAR | `0x30` |
| ESR | `0x96000005` |
| x8 | `0x0` |
| 故障函数 | `SHA256_Init + 0x10` |
| 指令 | `ldr x0, [x8, #0x30]` |
| 调用方 | `vm_kernel_addrhash_internal + 0x74`，SHA256_Init调用后的返回地址 |

`SHA256_Init`先加载 `g_crypto_funcs`，再读取偏移0x30的 `ccsha256_di`；
当前没有注册corecrypto kext，表为空。调用方对非零、非内核滑动范围地址执行
SHA-256(salt || address)，取前8字节。这是内核地址哈希的实际失败，不能认定为代码签名失败。

## 修复与验证

`ports/lithium/patches/xnu-7195/0023-lithium-native-sha256.patch` 复用固定
ios-sailfish `6f27e41ff249f2680540ebe8ee5ab982c5e641c9` 的同号补丁：
LITHIUM的旧SHA256_Init/Update/Final接口接入XNU现有 `ccsha256_ltc_di`，
使用现有真实digest初始化、更新、压缩和final实现。保留原7195上下文和其他平台的动态provider路径。
SHA384/512不属于这次已观察到的故障范围。

`scripts/validate-ios14-native-sha256.py` 编译实际旧API、XNU digest/compressor源文件，
并提取实际 `vm_kernel_addrhash_internal`。25项检查覆盖：

- [NIST SHA-256测试值](https://www.nist.gov/itl/ai-standards-and-guidelines-group/nsrl-test-data)、
  空串、百万a、填充与页大小边界，和hashlib独立对照。
- 多种分段宽度、非对齐输入、零长度、上下文复制、前后越界哨兵。
- 112字节/8字节对齐的实际SHA256_CTX，108字节provider所需状态，64位位计数进位。
- 地址哈希的零地址、滑动范围快速路径及多种地址/salt组合。
- 非LITHIUM真实provider分发仍计算正确。
- 新内核Init/Update直接调用现有digest；Final从已解析的静态descriptor取函数指针，
  实际指向 `ccdigest_final_64be`；descriptor的compress也核对到真实SHA256压缩函数。
- 冻结第十一轮二进制的空表指令路径被识别为旧失败实现。

验证报告：`reports/ios14-native-sha256-validation.json`。
新内核SHA256 `68eb2900c9cbecdf6cc3af763dbc31ac3fd78fcb59c5516bbb84b86de83526a2`。
主机与静态检查不代表手机已通过此停点；需下一轮照片确认。
