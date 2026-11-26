用户实测补充（2026-11-23）：614.4 MHz 配置档比此前明显更流畅；“数倍”为主观描述，未量化。用户明确手动重启回 Fastboot，不能计为本轮自动返回成功。

# R40 保存日志分析：r40-ui-614-01

状态：**return_not_verified**。生成时间：2026-11-22T20:34:31.504523+08:00。

pending 原因：无待生成的返回记录。采集器最后保存状态：capture_failed；只据保存文件分析，未证明当前物理运行状态。

镜像 SHA256：`8aa362ad383087e8a8a0552f2a218071d3fc601c20c6b56a61f6547c87f41483`。

## UI 软件观测

最后阶段 `abort`；最后活动阶段 `abort`。

| 计数 | 最后观测 | 最大观测 | 记录数 |
|---|---:|---:|---:|
| expected | 402 | 402 | 2 |
| queued | 0 | 0 | 2 |
| bbd_copyout | 0 | 0 | 2 |
| hid_dispatch_returned | 0 | 0 | 2 |
| cleanup | 0 | 0 | 2 |
| cleanup_evicted | 0 | 0 | 2 |
| queue_depth | 0 | 0 | 2 |
| swaps | 1064 | 1064 | 2 |
| sb_commits | 3768 | 3768 | 2 |
| max_read_us | 0 | 0 | 2 |

queued 是入队意图；bbd_copyout 是用户态读出；hid_dispatch_returned 是调用返回；sb_commits 是 after-CA runloop 观测。累计计数不求和，均不能证明物理响应、物理帧或导航验收。

## TSENS 四路忙闲温度

按日志中 UI 阶段边界归类，并非精确跨线程时间对齐；单位为原生 °C，校准与新鲜度未验。

| 阶段 | affinity / sensor | 有效数 | 无效数 | 最低 °C | 最高 °C | 中位 °C |
|---|---|---:|---:|---:|---:|---:|
| all | 0x0 / 4 | 122 | 0 | 45.8 | 70.1 | 64.7 |
| all | 0x1 / 6 | 122 | 0 | 46.1 | 72.7 | 64.3 |
| all | 0x100 / 9 | 122 | 0 | 41.6 | 68.2 | 62.1 |
| all | 0x101 / 11 | 122 | 0 | 41.9 | 72.3 | 62.7 |
| busy | 0x0 / 4 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| busy | 0x1 / 6 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| busy | 0x100 / 9 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| busy | 0x101 / 11 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| idle | 0x0 / 4 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| idle | 0x1 / 6 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| idle | 0x100 / 9 | 0 | 0 | 未取得 | 未取得 | 未取得 |
| idle | 0x101 / 11 | 0 | 0 | 未取得 | 未取得 | 未取得 |

## Framebuffer 整帧绘制 CPU / wall（仅 band=1）

源码中 band=0 为 setup 开销，band=1 为整个 draw job 的行范围，band=2..9 为各行分区；本次 2040 行日志的四分区为 band=2..5。主表只取 band=1，不混入 setup 或分区，也不把分区求和。缺少 band=1 时保留未取得；wall 可能包含调度等待，不推算物理 FPS。

band 定义来源：`ports/hakios-lithium/patches/0008-lithium-thread-wait-profile.patch:177-180; R40 main/expanded-before/iokit/Kernel/MiniIOMobileFramebuffer.cpp:1288-1291`。

| 阶段 | CPU 样本 / 中位 µs | wall 样本 / 中位 µs |
|---|---|---|
| all | 6 / 17005.5 | 6 / 17005.5 |
| busy | 0 / None | 0 / None |
| idle | 0 / None | 0 / None |

分 band 统计（all 阶段；各 band 原始记录及 busy/idle 分组保留在 JSON）：

| band | 范围 | 记录数 | CPU 中位 µs | wall 中位 µs |
|---|---|---:|---:|---:|
| 0 | setup_overhead | 6 | 13.0 | 13.0 |
| 1 | full_draw_job_rows | 6 | 17005.5 | 17005.5 |
| 2 | draw_rows_partition | 6 | 4291.0 | 4291.0 |
| 3 | draw_rows_partition | 6 | 4257.5 | 4258.0 |
| 4 | draw_rows_partition | 6 | 4331.0 | 4331.0 |
| 5 | draw_rows_partition | 6 | 4136.0 | 4136.0 |

## UFS 返回与终态

| 行 | phase | error | ready | request |
|---:|---|---|---|---|

内核日志有序 finalization 完成：False；正常 timer/drain 独立串流核验：True；四路 stop：True；source dropped：0（None 为缺证，非零丢失通过）。

USB 连续性：True；frames：9927；console bytes：2206666。

已保存 devices/product 同机终点：False。不据 manifest、日志队列或终点单独标记自动返回全链、恢复耐久性或 UI 导航验收通过。

返回核验原始记录：

```json
{
  "attempt": "r40-ui-614-01",
  "started_at": "2026-11-22T20:30:09.054824+08:00",
  "elapsed_seconds": 825.024826,
  "devices_query_count": 1,
  "product_query_count": 0,
  "passed": false,
  "fastboot_endpoint_verified": false,
  "automatic_fastboot_verified": false,
  "runtime_verified": false,
  "storage_shutdown_verified": false,
  "ui_verified": false,
  "hardware_validation": "pending_main_runtime_review",
  "retry": false,
  "device_rows": [],
  "failure": "sole target absent; product query skipped",
  "finished_at": "2026-11-22T20:30:09.086223+08:00"
}
```

观察问题：ui_cycle_aborted_or_disabled。

