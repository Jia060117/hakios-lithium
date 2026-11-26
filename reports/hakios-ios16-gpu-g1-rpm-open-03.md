# GPU G1 RPM VERSION/OPEN 03 · 2026-11-25

版本协商与远端 OPEN 已观察到；完整通道验收 `True`，协议结果 `0`。
flags：peer_version `1`、version_acked `1`、
local_open `1`、remote_open `1`、open_acked `1`。
远端 channel `2`。保留RX读取 `28` 条，未完成的具体拒绝原因需对照原始包与源码，不推断TZ拒绝。

一次 RAM 启动 `2026-11-25T23:12:31.453356+08:00`，镜像 `948fca94bea5d0258955c85d9e9827d44ca353f8e65138bec0e0784989712ad5`。
本次 `200` 次读取、`29` 次协议存储、`5` 次 doorbell，
耗时 `560` µs。资源请求、PMIC、GPU 写入均为 0。
映射 `2` / 释放 `2`，lease `0`；
traffic_retained `1` / uncertain `0` 原样保留，无 close、索引清零或重试。

正常180秒timer、四路stop、source丢0、drain及同机Fastboot终点核验 `True`。
USB独立逐字节重放 `254` 帧、`70929` raw、`62801` console字节。
接收器 `capture_failed` / `[Errno 32] Pipe error` 原样保留。
唯一返回核验 `2026-11-25T23:17:03.365939+08:00`，passed `True`。

原始日志未修改。派生解析视图只移除完整且有明确终止字段的温度记录，记录原偏移与全文，
从而复原被 printf 并发插入的 flags/end 记录；没有补猜任何字节。
若存在BOOTARGS交错，保存的demux记录先验证全部原字节，并要求拼接后与冻结包kernel.ver和本次启动参数的完整printf相等；
温度记录偏移相对于这一步之后的派生视图。其他服务的有界OPEN记录 `[{'index': '0', 'rcid': '3', 'name_len': '10', 'name_hex': '676c696e6b5f73737200', 'consumed': '1', 'ack_sent': '1', 'scope': 'transport_only'}]`，不作为资源请求ACK。
主代理执行实际构建、逐层读回、单次启动和独立 USB/写地址复核；
Aristotle 的专项 ASan/UBSan 与 ARM64 对象测试已通过（01为97场景，03另有16个多通道专项）；02的98场景保持原读写次数。
内核 `24a90b34057f4b355db2aea91d2e03d9c6a9976b4851d3445e26a9d5f855352a`。

独立从TX原始store轨迹解码 `5` 个完整控制帧，无资源数据帧。
这次未确认资源投票、GPU chip ID、CP fence 或 Metal 加速。下一进入MX及总线条件验证；完整OPEN不代表资源投票或GPU加速。
