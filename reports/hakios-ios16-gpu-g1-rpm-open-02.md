# GPU G1 RPM VERSION/OPEN 02 · 2026-11-25

版本协商与远端 OPEN 已观察到；完整通道验收 `False`，协议结果 `-4`。
flags：peer_version `1`、version_acked `1`、
local_open `1`、remote_open `1`、open_acked `0`。
远端 channel `2`。保留RX读取 `18` 条，未完成的具体拒绝原因需对照原始包与源码，不推断TZ拒绝。

一次 RAM 启动 `2026-11-25T22:59:55.451502+08:00`，镜像 `655421090e4977abbaac62a3c07d43d3c3668ded35b4c0a19c50e7b4f19fe613`。
本次 `182` 次读取、`23` 次协议存储、`4` 次 doorbell，
耗时 `521` µs。资源请求、PMIC、GPU 写入均为 0。
映射 `2` / 释放 `2`，lease `0`；
traffic_retained `1` / uncertain `1` 原样保留，无 close、索引清零或重试。

正常180秒timer、四路stop、source丢0、drain及同机Fastboot终点核验 `True`。
USB独立逐字节重放 `250` 帧、`69721` raw、`61721` console字节。
接收器 `capture_failed` / `[Errno 32] Pipe error` 原样保留。
唯一返回核验 `2026-11-25T23:04:52.963358+08:00`，passed `True`。

原始日志未修改。派生解析视图只移除完整且有明确终止字段的温度记录，记录原偏移与全文，
从而复原被 printf 并发插入的 flags/end 记录；没有补猜任何字节。
主代理执行实际构建、逐层读回、单次启动和独立 USB/写地址复核；
Aristotle 的实际 adapter 97 场景 ASan/UBSan 与 ARM64 对象测试已通过。
内核 `7fed12c5cdd1ab5d94fa0b51f11dd61f077d7c0671c9170db1c495c6db049de6`。

这次未确认资源投票、GPU chip ID、CP fence 或 Metal 加速。下一修订依据协议源码补齐拒绝处 RX 证据后继续。

定位：RX最后两字为`0x00030002 / 0x0000000a`，即另一个远端OPEN，rcid3/name_len10。旧core在读取名称前以固定13长度拒绝，因此其服务名仍未知；并非本通道ACK字段错误。下一修订按pinned MiCode多通道语义有界处理这种OPEN。
