# GPU G1 RPM VERSION/OPEN 01 · 2026-11-25

版本协商与远端 OPEN 已观察到；完整通道验收 `False`，协议结果 `-4`。
flags：peer_version `1`、version_acked `1`、
local_open `1`、remote_open `1`、open_acked `0`。
远端 channel `2`。拒绝处 RX 包未留存，具体原因未验证，不能推断 TZ 拒绝。

一次 RAM 启动 `2026-11-25T22:47:26.926442+08:00`，镜像 `dcf567dfe487538b039099410eaf07697fae259f9298dc6d7d007feef759893a`。
本次 `182` 次读取、`23` 次协议存储、`4` 次 doorbell，
耗时 `519` µs。资源请求、PMIC、GPU 写入均为 0。
映射 `2` / 释放 `2`，lease `0`；
traffic_retained `1` / uncertain `1` 原样保留，无 close、索引清零或重试。

正常180秒timer、四路stop、source丢0、drain及同机Fastboot终点核验 `True`。
USB独立逐字节重放 `246` 帧、`68523` raw、`60651` console字节。
接收器 `capture_failed` / `[Errno 32] Pipe error` 原样保留。
唯一返回核验 `2026-11-25T22:51:51.037913+08:00`，passed `True`。

原始日志未修改。派生解析视图只移除完整且有明确终止字段的温度记录，记录原偏移与全文，
从而复原被 printf 并发插入的 flags/end 记录；没有补猜任何字节。
主代理执行实际构建、逐层读回、单次启动和独立 USB/写地址复核；
Aristotle 的实际 adapter 97 场景 ASan/UBSan 与 ARM64 对象测试已通过。
内核 `06bd801a7e62034bd7393b0be19a2d02cd562c02a1f78e3ce5aef5bf2c927d5b`。

这次未确认资源投票、GPU chip ID、CP fence 或 Metal 加速。下一修订依据协议源码补齐拒绝处 RX 证据后继续。
