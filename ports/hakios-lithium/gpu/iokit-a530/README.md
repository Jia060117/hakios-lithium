# A530 native HakiosGPU IOKit glue

真实 `IOService` / `IOUserClient`，不是 host validator。保留 `HakiosGPU`、`HakiosGPUUserClient`、open type0、16-selector wire ABI、handle 作为 client-memory type；不改 userland `gpu.hpp/gpu.cpp/iokit_transport.cpp`。源码与对象均不包含 vendor 固件。

本实现只提供 IOKit 接入，**没有默认硬件后端，也没有可用设备**。动态 g3 BO mapping、真实通用 g4 CP submit/completion、缓存同步和授权回调缺一即 Unsupported；真实同 boot ready 未成立时不注册服务。当前 g3-live 静态 SMMU 草案和 g4-live 固定 marker probe 不满足这个安装合同。

## 安装入口

`backend.hpp` 定义版本化显式 `HakiosA530Backend` 函数表：真实 owner/context/generation、ready/authorize/parameter、g3 map/unmap/sync、g4 submit/last/done/dmaStopped。

- `HakiosA530Publish(provider, &backend, &service)`：成功返回 +1 service，仅在真实 ready 时发布；backend owner 被 retain。
- `HakiosA530Unpublish(service)`：停止新 opens/请求并 terminate；Busy/错误时保留依赖，不做硬件 reset/stop。
- `HakiosA530Reap(service)`：退休后回收关闭 BO；unknown/partial DMA、unmap/complete 失败继续保留。
- `HakiosA530Wait(service,fence,ms)`：供已有显示等待路径桥接到真实 CP completion。

不同时链接旧 `hakios.gpu.cpp`，避免同名 metaclass 冲突。不开启旧 Redwood `hakios_adreno` 硬件路径。本层没有 `HakiosGPUStart/Stop` 旧平台自动 hook；由 main 在真实准备完成后显式发布。只在普通线程使用，后端负责满足实际 logical0/affinity1/window 条件，回调不能回入同一 service 锁。

`mini-surface.cpp` 是可选的真实 `MiniIOSurface`/framebuffer CPU-copy 桥。main 接好 `MiniIOSurfaceDrawn`→`HakiosA530Wait` 并处理已有 stub 符号后，才调用 `HakiosA530UseMiniIOSurface`；否则 SURFACE/SHOW 明确 Unsupported。CPU-copy show 不等于 GPU/Metal 成功。

## 内存和 fence

GEM 使用真实动态 `IOBufferMemoryDescriptor`；USER_MEMORY 用真实 task VA descriptor + prepare；g3 必须为实际物理段分配/发布真实 IOVA。保留 prepare、descriptor、mapping、surface holder 以及 service/backend owner，直到真实有序 fence 完成或独立 DMA-stopped 证据成立。失去 ready 不当成已完成。部分 map/submit 失败锁存 fault，保留 uncertain 页，只允许清理后新建服务；不伪造 fence、不重试硬件 store。

CPU_PREP 按 upstream 等待全局 last fence，使用 continuous-clock 绝对时间、最多5000ms、200us轮询；随后真实 TO_CPU sync。CPU_FINI 保留 no-op，每次 submit 必经真实 TO_GPU sync。FD_OUT 回 -1，没有 fd export。queue 只表示同一有序 ring 的软件 ID，QUERY 没有真实统计则 Unsupported。

规范记录采用精确长度；拒绝未知命令、零长度、无效 BO、syncobj/未知 flags/非零 queue priority 等未实现语义。所有原 `Gpu` 调用方使用的规范记录及 priority0 路径兼容；未把 upstream 对未实现请求的宽松 no-op 当作硬件能力。

## 验证

```powershell
wsl.exe -d Ubuntu -- python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/iokit-a530/compile_native.py
```

使用现有真实 XNU IOKit `.CFLAGS/.CXXFLAGS`、`xnu-clang++`、arm64 iOS16.5 和 `-Werror`，分别编译 native service、MiniIOSurface bridge 和复用的完整 ABI probe（18 records/78 fields/522 assertions），只产出 isolated Mach-O 对象；不调用 make、不链接内核、不访问设备。四份上游 ABI 源编译前后 SHA256 校验，未重复旧 mock 场景测试。

编译不证明 Darwin runtime lifecycle、缓存一致性、CP/Metal 或真实硬件 ready。

准确安装步骤、16-selector 表、g3/g4 当前缺少的 API、显示 stub seam、生命周期审查及全部验证证据见仓库内：

`evidence/gpu-metal-20261126/iokit-a530/INTEGRATION.md`
