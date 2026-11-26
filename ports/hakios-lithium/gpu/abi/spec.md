# HakiosGPU ABI 主机快照 · 2026-11-23

主线：**A530 → hakios-metal A5xx → HakiosMetalCA**。本包建立接口契约和最小兼容检查；**A530 硬件提交为 `unsupported`**。没有设备访问、upstream 修改或硬件能力验收。硬件实现、HakiosMetalCA 调用方均不在本次四文件读取范围内。

源码锚点只来自以下文件；完整 SHA-256、字节数和行数见 `evidence/gpu-metal-20261123/abi/source-manifest.json`。下文 `K:行号` 等均指这些真实源码行号；逐条证据、全部字段类型/偏移见同目录 `spec.json`，有界原文摘录在证据目录 `source-excerpts.txt`。

| 缩写 | 相对仓库根的实际路径 | SHA-256 前 12 位 |
|---|---|---|
| K | `upstream/hakios-xnu-kernel/drivers/gpu/hakios.gpu.cpp` | `2d3ba9f37a45` |
| H | `upstream/hakios-xnu-kernel/hakios-metal/include/hakios-metal/gpu.hpp` | `ea45cbe172ea` |
| G | `upstream/hakios-xnu-kernel/hakios-metal/src/gpu.cpp` | `d16c01c4c94c` |
| T | `upstream/hakios-xnu-kernel/hakios-metal/src/iokit_transport.cpp` | `48067c25eff3` |

## 服务与封包

- 服务类/名称 `HakiosGPU`，property `kIOUserClientClassKey` 的值为 `HakiosGPUUserClient`；`IOServiceMatching("HakiosGPU")`、open type `0`。该 key 的字符串定义不在四文件内；这四文件没有发布 GPU 型号/能力 properties。启动 client 必须 ready。证据 K:303–323、465–472；T:67–80。
- selector 就是下表数字，不是编码后的 Linux ioctl。固定宽度整数、原生小端 64 位布局；记录内地址是调用进程 VA。`SUBMIT` 的数组同步 `copyin`，不能把地址当文件偏移。证据 K:71–161、990–994；G:168–176。
- 内核只检查输入非空、长度 ≤72、已有 buffers；整体清零后复制输入，没有逐 selector 精确长度检查。仅成功时复制 `min(输入长度, 输出容量)`。Metal 采用同址输入/输出，**不核验 returned 长度**。本检查器要求规范完整长度，是有意收紧的主机 profile。证据 K:1220–1232、1319–1325；T:22–39。

## 请求与结构

下表为规范完整记录字节数，不代表内核实施精确长度检查。`*` 表示 H 没有枚举该请求；`CPU_FINI` 的单字段结构和 `QUEUE_CLOSE` 的 u32 来自 Metal 调用约定，内核两者均 no-op。定义证据 K:72–161；H:22–65；G:133–138、196–199。

| Selector | 请求 | 字节 | Selector | 请求 | 字节 |
|---|---|---:|---|---|---:|
| `09` | GEM_CLOSE | 8 | `40` | GET_PARAM | 24 |
| `41` | SET_PARAM* | 24 | `42` | GEM_NEW | 16 |
| `43` | GEM_INFO | 24 | `44` | CPU_PREP | 24 |
| `45` | CPU_FINI | 4 | `46` | SUBMIT | 72 |
| `47` | WAIT_FENCE | 32 | `48` | MADVISE* | 12 |
| `4a` | QUEUE_NEW | 12 | `4b` | QUEUE_CLOSE | 4 |
| `4c` | QUEUE_QUERY* | 24 | `7d` | USER_MEMORY* | 24 |
| `7e` | SURFACE | 40 | `7f` | SHOW* | 24 |

关键偏移（其余完整列入 JSON，所有 78 个字段都有编译断言）：

- `Timespec(16,a8)`：signed seconds@0、nanoseconds@8。
- `Param(24,a8)` / `GemInfo(24,a8)`：两个 u32@0/4、u64 value@8、len@16、pad@20。
- `GemNew(16,a8)`：size@0、flags@8、handle@12。`CpuPrep(24,a8)`：handle@0、op@4、timeout@8。
- `SubmitCmd(32,a8)`：type@0、submit_idx@4、submit_offset@8、size@12、pad@16、nr_relocs@20、relocs@24；`SubmitBo(16,a8)`：flags@0、handle@4、presumed@8。
- `Submit(72,a8)`：flags@0、fence@4、nr_bos@8、nr_cmds@12、bos@16、cmds@24、**signed fence_fd@32**、queueid@36、in_syncobjs@40、out_syncobjs@48、nr_in_syncobjs@56、nr_out_syncobjs@60、syncobj_stride@64、pad@68。
- `WaitFence(32,a8)`：fence@0、flags@4、timeout@8、queueid@24；最后四字节为尾部 padding。`Surface(40,a8)`：surface@0、handle@4、size@8、offset@16、rowBytes@20、width@24、height@28、pixelFormat@32、planes@36。

## 必须保留的行为

| 范围 | 源码行为与边界 | 行号 |
|---|---|---|
| params | Metal 发送 pipe=1；H 列出 ID `1..10,10h..13h`。内核仅委托外部 `hakios_gpu_parameter`，实际支持和值未知。`chipId()` 截低32位，失败返回0；H 的 A635 注释不是 A530 证据。 | H:73–100；K:1235–1237；G:22–41 |
| GEMnew/info/close | Metal 按16KiB上取整；内核使用 `round_page_64/PAGE_SIZE`，数值定义范围外。上限256MiB，handle1..8192，GPU映射步长4096。新分配内存按注释清零，但同 client 缓存复用默认不清零。INFO 0=`handle<<32`、1=IOVA、5=flags、2=不保存名称，其余 Unsupported。close 可延迟释放忙 buffer。 | G:43–74；K:605–665、720–799、947–968、480–535 |
| prep/fini | PREP **忽略 handle，等待全局 last fence**；NOSYNC=4 即0等待，未完成 Busy。超时为 continuous clock 绝对值，限0..5000ms，轮询200us。FINI 成功空操作，Metal 丢弃错误。 | K:174–180、195–222、259–279、1238–1260；G:191–199 |
| submit | 1..8192 BO，1..128 cmd；type≠1被跳过。type1要求有效命令BO、无reloc、offset/size四字节对齐且范围有效；未显式拒绝0 size或全跳过。非命令无效BO可被跳过；presumed/BO flags不驱动重定位。queue/syncobj不实现调度。调用**外部** `hakios_gpu_submit`；fence非0成功、0 Timeout；FD_OUT仅回填-1，无fd。 | K:970–1063、1080–1099、1141–1150 |
| Metal submit/wait | command BO先放 READ，其他uses按handle合并读写位；一条 CMD_BUF，`size=words*4`、uint64进程指针、queueid透传。0 words拒绝。fence只在成功时更新。WAIT future-fence比较失败 BadArgument，未完成 Timeout，queue/flags忽略。 | G:140–199；K:1264–1282 |
| queue/扩展 | NEW仅++id，不证独立队列；CLOSE/SET_PARAM空操作；QUERY写零但忽略copyout失败；MADVISE retained=1。SURFACE重复返回同handle，`ffffffff`仅描述；USER_MEMORY必须整页，readonly回退flag=0x2；SHOW是CPU拷贝面板，不证明GPU。 | K:809–945、1153–1179、1238–1241、1283–1313 |
| map/cache | map type=handle，内核retain/seen=true/options=0。Metal map用Anywhere，忽略要求大小和返回大小；unmap错误忽略。GEM默认descriptor WC，`gpu_cached=1`改cached，**BO flag 0x20000并不选择cache模式**。提交时flush已映射cached GEM、每次flush导入用户页、surface按seed/4ms/seen策略flush，末尾总有to_memory(NULL,0) barrier；FINI无维护。外部flush实现未读取，不证明A530一致性。 | K:332–333、773–783、997–1045、1182–1199；T:41–57 |

IOKit → Metal errno：`Success→0`、`NoMemory→ENOMEM`、`BadArgument→EINVAL`、`Timeout→ETIMEDOUT`、`Busy→EBUSY`、`NotFound→ENOENT`、`Unsupported→ENOSYS`；其余（含 `NotReady/VMError`）打印并返回 `EIO`。H:173–174 的 `ENOTSUP` 注释与实际 T:22–39 不同。四文件未定义数值 IOReturn；本包测试桩的 token 不冒充 Darwin 数值，主机 errno 数值也不能当 Darwin wire 值。

## 复用与验证

产品契约用 `hakios_gpu_abi.h`（C/C++）；检查器用 `validator.hpp`。`validate_record` 只验规范长度，`validate_submit` 接收显式主机数组/BO大小，不解引用 wire VA，也不验证GPU命令内容/所有权；其非type1拒绝策略比内核严格。`execute` / `a530_submit` 对形状有效请求返回 **unsupported**，绝不生成或修改 fence。

从仓库根运行（Windows 使用现有 WSL Ubuntu；Linux 用 python3）：

```powershell
python ports/hakios-lithium/gpu/abi/run_tests.py
```

脚本固定四源 SHA-256，前后复核；生成原文结构体镜像与布局测试，编译 C11 头、x86_64 原生布局及 arm64 Darwin freestanding 布局。链接**原始** G/T 两份 cpp 到主机 mock IOKit，ASan/UBSan 检查 GEM/参数/队列/submit/fence/错误映射/清理。IOKit shim 只能用于测试，不进入产品。

验收：**18 records / 78 fields / 522布局断言；12场景 / 276运行检查；全部退出0**。这不包含内核执行、Darwin runtime或arm64运行。精确 argv、日志及哈希见 `evidence/gpu-metal-20261123/abi/results.json`；全部交付文件 SHA-256 在同目录 `SHA256SUMS.txt`。硬件 backend、真实 A530 submit、GPU完成/缓存一致性、HakiosMetalCA端到端能力均未验。
