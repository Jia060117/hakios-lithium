本轮 UFS 有序卸载/最终刷新封存/排空均通过，并核实同一设备回 Fastboot。

本次按两份原始帧的 offset 逐字节校验后拼接分析；原始 Pipe error、重复帧和原文件均保留。canonical 帧为派生数据，其帧数不是原始 USB 帧数。

# R40 保存日志分析：r40-ui-614-02

状态：**observations_complete**。生成时间：2026-11-23T01:58:49.930887+08:00。

pending 原因：无待生成的返回记录。采集器最后保存状态：capture_failed；只据保存文件分析，未证明当前物理运行状态。

镜像 SHA256：`0927c9d86fb1093fc394cb0c6666a38f281f7c62582458117a34242bda941dd1`。

## UI 软件观测

最后阶段 `abort`；最后活动阶段 `abort`。

| 计数 | 最后观测 | 最大观测 | 记录数 |
|---|---:|---:|---:|
| expected | 402 | 402 | 14 |
| queued | 134 | 134 | 14 |
| bbd_copyout | 134 | 134 | 14 |
| hid_dispatch_returned | 134 | 134 | 14 |
| cleanup | 0 | 0 | 21 |
| cleanup_evicted | 0 | 0 | 14 |
| queue_depth | 0 | 8 | 14 |
| swaps | 1038 | 1038 | 14 |
| sb_commits | 3501 | 3501 | 14 |
| max_read_us | 229239 | 229239 | 14 |

queued 是入队意图；bbd_copyout 是用户态读出；hid_dispatch_returned 是调用返回；sb_commits 是 after-CA runloop 观测。累计计数不求和，均不能证明物理响应、物理帧或导航验收。

## TSENS 四路忙闲温度

按日志中 UI 阶段边界归类，并非精确跨线程时间对齐；单位为原生 °C，校准与新鲜度未验。

| 阶段 | affinity / sensor | 有效数 | 无效数 | 最低 °C | 最高 °C | 中位 °C |
|---|---|---:|---:|---:|---:|---:|
| all | 0x0 / 4 | 122 | 0 | 47.7 | 72.3 | 66.6 |
| all | 0x1 / 6 | 122 | 0 | 48.3 | 78.1 | 66.6 |
| all | 0x100 / 9 | 122 | 0 | 43.8 | 72.3 | 64.5 |
| all | 0x101 / 11 | 122 | 0 | 43.8 | 73.3 | 64.7 |
| busy | 0x0 / 4 | 3 | 0 | 65.3 | 66.6 | 65.6 |
| busy | 0x1 / 6 | 3 | 0 | 66.3 | 68.2 | 66.9 |
| busy | 0x100 / 9 | 3 | 0 | 64.7 | 66.9 | 65.6 |
| busy | 0x101 / 11 | 3 | 0 | 64.7 | 66.3 | 65.9 |
| idle | 0x0 / 4 | 6 | 0 | 65.6 | 66.6 | 65.75 |
| idle | 0x1 / 6 | 6 | 0 | 65.6 | 67.2 | 66.9 |
| idle | 0x100 / 9 | 6 | 0 | 63.4 | 64.3 | 64 |
| idle | 0x101 / 11 | 6 | 0 | 64.3 | 65 | 64.5 |

## Framebuffer 整帧绘制 CPU / wall（仅 band=1）

源码中 band=0 为 setup 开销，band=1 为整个 draw job 的行范围，band=2..9 为各行分区；本次 2040 行日志的四分区为 band=2..5。主表只取 band=1，不混入 setup 或分区，也不把分区求和。缺少 band=1 时保留未取得；wall 可能包含调度等待，不推算物理 FPS。

band 定义来源：`ports/hakios-lithium/patches/0008-lithium-thread-wait-profile.patch:177-180; R40 main/expanded-before/iokit/Kernel/MiniIOMobileFramebuffer.cpp:1288-1291`。

| 阶段 | CPU 样本 / 中位 µs | wall 样本 / 中位 µs |
|---|---|---|
| all | 6 / 17004.0 | 6 / 17004.0 |
| busy | 0 / None | 0 / None |
| idle | 0 / None | 0 / None |

分 band 统计（all 阶段；各 band 原始记录及 busy/idle 分组保留在 JSON）：

| band | 范围 | 记录数 | CPU 中位 µs | wall 中位 µs |
|---|---|---:|---:|---:|
| 0 | setup_overhead | 6 | 13.0 | 13.0 |
| 1 | full_draw_job_rows | 6 | 17004.0 | 17004.0 |
| 2 | draw_rows_partition | 6 | 4276.0 | 4276.0 |
| 3 | draw_rows_partition | 6 | 4234.0 | 4234.0 |
| 4 | draw_rows_partition | 6 | 4295.0 | 4295.0 |
| 5 | draw_rows_partition | 6 | 4212.0 | 4212.5 |

## UFS 返回与终态

| 行 | phase | error | ready | request |
|---:|---|---|---|---|
| 14592 | process_stop | None | None | None |
| 14845 | unmount | 0 | None | None |
| 14846 | seal | 0 | None | 41419 |
| 14847 | finalized | 0 | 1 | 41419 |

内核日志有序 finalization 完成：True；正常 timer/drain 独立串流核验：True；四路 stop：True；source dropped：0（None 为缺证，非零丢失通过）。

USB 连续性：True；frames：493；console bytes：2001155。

已保存 devices/product 同机终点：True。不据 manifest、日志队列或终点单独标记自动返回全链、恢复耐久性或 UI 导航验收通过。

返回核验原始记录：

```json
{
  "attempt": "r40-ui-614-02",
  "started_at": "2026-11-23T01:57:43.484677+08:00",
  "elapsed_seconds": 672.611353,
  "devices_query_count": 1,
  "product_query_count": 1,
  "passed": true,
  "fastboot_endpoint_verified": true,
  "automatic_fastboot_verified": false,
  "runtime_verified": false,
  "storage_shutdown_verified": false,
  "ui_verified": false,
  "hardware_validation": "pending_main_runtime_review",
  "retry": false,
  "device_rows": [
    [
      "54223ce9",
      "fastboot"
    ]
  ],
  "observed_product": "MSM8996_A8",
  "finished_at": "2026-11-23T01:57:43.530679+08:00"
}
```

观察问题：ui_cycle_aborted_or_disabled。



主代理结案补充（2026-11-23）：同一设备已于01:57:43回Fastboot，实际卸载/封存请求41419与正常timer/drain、源丢0相互吻合。两原始流10497帧按offset校验，429642字节重叠内容完全一致；合并2001155字节，无缺口/冲突，原始两次Pipe error保留。134条事件完成一组busy/idle后由实体触摸中断脚本，不算完整三组通过。启动核仅一个5秒周期样本达到78.1°C（前73.6、后67.9），性能簇峰73.3°C；不能由单样本推定超温持续时间，未定最终甜点，691.2包已备未启。本轮未写电压、未刷写分区。
