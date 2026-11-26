验证结案（2026-11-23）：691.2 MHz 配置档已进入实际桌面，真实触摸可用，用户反馈“流畅了点，但没之前第一次提频那么明显”。600秒会话正常退出，Data卸载0/最终seal0/request40980/源丢0/drain1，02:12:46唯一devices/product核实同一手机回Fastboot。

自动手势只完成一组134事件及30秒空闲，第二组因发送期限超过100ms释放触点退出；不能算三组通过，亦不据此判CPU运算不稳。完整USB原始9560帧、2090909 console字节连续无缺口/重复/残帧/device-lost，接收器末尾Pipe error保留。

峰值：启动核79.4°C，性能簇76.5/77.8°C；本轮初温比614.4高约4.2°C，不能把全部温差归因于频率。完整绘制band1各6个样本的中位CPU/wall约17.004→15.146ms，限小样本，非物理FPS或整机速度验收。暂保留614.4为实际体验基线，不宣布最终甜点；本轮已完成，不继续升档。

对照详情：[614/691保存日志对照](/F:/IOS-MIX1/evidence/cpu-r40-20261123/691-comparison/final-69103-021210/comparison.md)。

# R40 保存日志分析：r40-ui-691-03

状态：**observations_complete**。生成时间：2026-11-23T02:12:46.850464+08:00。

pending 原因：无待生成的返回记录。采集器最后保存状态：capture_failed；只据保存文件分析，未证明当前物理运行状态。

镜像 SHA256：`80681f0c07cee86d31846d0c48cd471be6e835efe7d0ab62f47f467f456a5014`。

## UI 软件观测

最后阶段 `consumer`；最后活动阶段 `abort`。

| 计数 | 最后观测 | 最大观测 | 记录数 |
|---|---:|---:|---:|
| expected | 402 | 402 | 13 |
| queued | 147 | 147 | 13 |
| bbd_copyout | 147 | 147 | 13 |
| hid_dispatch_returned | 146 | 146 | 13 |
| cleanup | 1 | 1 | 21 |
| cleanup_evicted | 0 | 0 | 13 |
| queue_depth | 1 | 2 | 13 |
| swaps | 913 | 913 | 13 |
| sb_commits | 2819 | 2819 | 13 |
| max_read_us | 110522 | 110522 | 13 |

queued 是入队意图；bbd_copyout 是用户态读出；hid_dispatch_returned 是调用返回；sb_commits 是 after-CA runloop 观测。累计计数不求和，均不能证明物理响应、物理帧或导航验收。

## TSENS 四路忙闲温度

按日志中 UI 阶段边界归类，并非精确跨线程时间对齐；单位为原生 °C，校准与新鲜度未验。

| 阶段 | affinity / sensor | 有效数 | 无效数 | 最低 °C | 最高 °C | 中位 °C |
|---|---|---:|---:|---:|---:|---:|
| all | 0x0 / 4 | 122 | 0 | 52.2 | 75.2 | 70.1 |
| all | 0x1 / 6 | 122 | 0 | 52.5 | 79.4 | 69.8 |
| all | 0x100 / 9 | 122 | 0 | 48 | 76.5 | 67.5 |
| all | 0x101 / 11 | 122 | 0 | 48 | 77.8 | 68.2 |
| busy | 0x0 / 4 | 3 | 0 | 68.5 | 69.5 | 68.8 |
| busy | 0x1 / 6 | 3 | 0 | 68.5 | 69.5 | 69.1 |
| busy | 0x100 / 9 | 3 | 0 | 67.2 | 71.4 | 67.2 |
| busy | 0x101 / 11 | 3 | 0 | 68.5 | 69.8 | 69.5 |
| idle | 0x0 / 4 | 6 | 0 | 69.1 | 71.7 | 70.1 |
| idle | 0x1 / 6 | 6 | 0 | 69.8 | 73.6 | 70.4 |
| idle | 0x100 / 9 | 6 | 0 | 68.2 | 71.4 | 68.5 |
| idle | 0x101 / 11 | 6 | 0 | 68.2 | 71.7 | 70.25 |

## Framebuffer 整帧绘制 CPU / wall（仅 band=1）

源码中 band=0 为 setup 开销，band=1 为整个 draw job 的行范围，band=2..9 为各行分区；本次 2040 行日志的四分区为 band=2..5。主表只取 band=1，不混入 setup 或分区，也不把分区求和。缺少 band=1 时保留未取得；wall 可能包含调度等待，不推算物理 FPS。

band 定义来源：`ports/hakios-lithium/patches/0008-lithium-thread-wait-profile.patch:177-180; R40 main/expanded-before/iokit/Kernel/MiniIOMobileFramebuffer.cpp:1288-1291`。

| 阶段 | CPU 样本 / 中位 µs | wall 样本 / 中位 µs |
|---|---|---|
| all | 6 / 15146.0 | 6 / 15146.0 |
| busy | 0 / None | 0 / None |
| idle | 0 / None | 0 / None |

分 band 统计（all 阶段；各 band 原始记录及 busy/idle 分组保留在 JSON）：

| band | 范围 | 记录数 | CPU 中位 µs | wall 中位 µs |
|---|---|---:|---:|---:|
| 0 | setup_overhead | 6 | 12.0 | 12.0 |
| 1 | full_draw_job_rows | 6 | 15146.0 | 15146.0 |
| 2 | draw_rows_partition | 6 | 3821.0 | 3821.5 |
| 3 | draw_rows_partition | 6 | 3823.5 | 3823.5 |
| 4 | draw_rows_partition | 6 | 3846.0 | 3860.5 |
| 5 | draw_rows_partition | 6 | 3760.0 | 3760.5 |

## UFS 返回与终态

| 行 | phase | error | ready | request |
|---:|---|---|---|---|
| 14797 | process_stop | None | None | None |
| 15338 | unmount | 0 | None | None |
| 15339 | seal | 0 | None | 40980 |
| 15340 | finalized | 0 | 1 | 40980 |

内核日志有序 finalization 完成：True；正常 timer/drain 独立串流核验：True；四路 stop：True；source dropped：0（None 为缺证，非零丢失通过）。

USB 连续性：True；frames：9560；console bytes：2090909。

已保存 devices/product 同机终点：True。不据 manifest、日志队列或终点单独标记自动返回全链、恢复耐久性或 UI 导航验收通过。

返回核验原始记录：

```json
{
  "attempt": "r40-ui-691-03",
  "started_at": "2026-11-23T02:12:46.465862+08:00",
  "elapsed_seconds": 670.789436,
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
  "finished_at": "2026-11-23T02:12:46.524305+08:00"
}
```

观察问题：ui_cycle_aborted_or_disabled。

