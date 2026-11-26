# GPU G1 BIMC计频 01 · 2026-11-26

一次隔离RAM启动`2026-11-26T00:00:57.377425+08:00`，镜像`3f37209da284deb8310342577d0a7ee42877a2f35855947788dc658a1fcc8a0a`。
BIMC本体selector0xad，两次新鲜计数有效`True`，独立公式重算结果`508799377`Hz。
参考为继承的XO/4=4.8MHz，未单独校准参考源；不是GPU核心频率或RPM原AP票值。
本阶段综合通过`True`，配置恢复`True`，生命周期`True`。

| 窗口 | ticks | valid | READY | DONE | count | polls |
| ---: | --- | ---: | --- | --- | ---: | ---: |
| 0 | `0x1000` | 1 | `0x0` | `0x206a159` | 434521 | 27 |
| 1 | `0x10000` | 1 | `0x6a159` | `0x26a0169` | 6947177 | 509 |

| 寄存器 | 原配置 | 最终值 | 恢复核对 |
| --- | --- | --- | ---: |
| `0x362000` | `0x14b` | `0x14b` | 1 |
| `0x362004` | `0x0` | `0x0` | 1 |
| `0x343008` | `0x80030000` | `0x80030000` | 1 |

仅MUX/CTL/XO三个debug配置地址有界写，STATUS从未写入；
XO比较排除只读动态状态[31:28]，不声称DONE/count恢复成原值。
564读/10写，14670µs，2段映射释放2。
rc/error/cleanup_error=`0/0/0`；
dirty/uncertain=`0x0/0x0`。
每窗口20ms/2000poll，正常40ms、总45ms且保留清理预算；不重试。
未发RPM资源票、未改父时钟或PLLTEST、PMIC/GPU电源时钟未写，未读GPU/SMMU/BKE。

正常180秒timer、四路stop/source0/drain及唯一同机Fastboot检查`True`。
USB独立重放242帧/67477raw/59733console；
接收器`capture_failed`/`[Errno 32] Pipe error`保留。
返回核验时间`2026-11-26T00:05:19.349170+08:00`。

Aristotle完成实际core/adapter30个ASan/UBSan专项与ARM64编译；main完成23个参数专项、
完整HakiOS实际构建/镜像读回/上机/新鲜计数与配置恢复独立核对及USB逐字节重放。
计频时序/selector/公式绑定MiCode1492c8e4373b7428bb0accc4f4881e97b36508b9精确下载源码。
原日志未改；派生视图只移除完整温度记录并保留字节偏移。
这不授予未知MX原AP投票，不证明GPU可访问。芯片ID、CP/fence和Metal加速仍未验。
