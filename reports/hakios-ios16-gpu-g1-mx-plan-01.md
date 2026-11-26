# GPU G1 MX启动配置只读01 · 2026-11-26

一次隔离RAM启动`2026-11-26T01:17:52.724409+08:00`，镜像`8ad17b049143b9f2f158e44c7f9139c77de2f7770136ca357a8e6f8099465d6f`。
只读采集通过`True`，目标档位解析通过`True`，生命周期`True`。

TOC与选中稀疏数据双遍保存，实际42次read32、零写/零投票/零轮询/零重试，
1映射/1释放、124µs。SMEM仅限`86000000..86200000`。
item473 version1、rail0x100/internal corner3需求上界：`850000`uV；
内核继承850mV边界判定`1`。无有效标志的原始0不是测得电压。

独立解码：`{"control": "closed-loop row", "ceiling_uV": 850000, "floor_uV": 850000, "initial_uV": 850000, "required_upper_bound_uV": 850000, "rail_id": 256, "wire_corner": 3, "internal_corner": 3, "control_raw": 1, "mode_record_offset": 424, "smem_sha256": null, "sparse_capture": true, "inherited_850_meets_decoded_voltage_bound": true, "provenance_and_same_boot_binding_required": true, "full_gpu_supply_certified": false, "capture_json_sha256": "7fe1737e554515b9aa65855f5ae0f4e013f6487ecba232ea14a4e889bcf6446a", "item_physical_address": 2248160368}`。
kind1表示扩展CPR记录，不能单凭kind推断当前闭环已启用；floor/initial不能替代ceiling。
这是启动交接配置，非实时PMIC测量，也不认证当前RPM与本地stock二进制完全一致。
GPU供电使用前仍需同次rail-ready与数字设定读回；本轮未取得该同次PMIC证据。
未操作GX/PLL/电压或RPM资源票，未读GPU/SMMU，不等于完整GPU上电、ID、CP或Metal验收。

正常180秒timer/四stop/source0/drain与唯一同机Fastboot终点`True`；
USB242帧/67470raw/59726console独立逐字节重放，
接收器`capture_failed`/`[Errno 32] Pipe error`原样保留。
CPU614.4MHz配置模型/R20显示/旧冻结与原索引保持，无物理UFS provider。
