# GPU G1组合电源/时钟03

单次RAM接受 `2026-11-26T02:44:30.198492+08:00`，镜像 `bdc795b835f8203d8b63cb3c6fdae91ae72659532699a4157ff587663b99dad2`。
组合阶段通过 `True`，正常定时与日志排空 `True`，同机Fastboot终点 `True`。
返回核验记录：`{"started_at": "2026-11-26T02:48:33.400099+08:00", "elapsed_seconds": 243.200608, "devices_query_count": 1, "product_query_count": 1, "device_rows": [["54223ce9", "fastboot"]], "same_serial_fastboot": true, "observed_product": "MSM8996_A8", "product_verified": true, "passed": true, "retry": false, "attempt_id": "gpu-g1-power-clock-03", "phase": "g1-power-clock", "capture_stop_requested": true, "finished_at": "2026-11-26T02:48:33.919703+08:00"}`。错过原核验窗口不补认自动返回。

同次S1启动计划：`{"rail_id": 257, "internal_corner": 2, "kind": 1, "required_uv": 620000, "ceiling_uv": 620000, "floor_uv": 550000, "initial_uv": 620000, "captured_mode_corners": [2, 3, 5, 8], "live_analog_voltage": false, "installed_rpm_private_copy_identity_verified": false}`。
实际S1数字设定/ready：`{"error": "0", "callback_error": "0", "last_error": "0", "last_phase": "7", "last_matches": "1", "last_mapping_reads": "1", "matches": "1", "mapping_reads": "528", "route_valid": "1", "apid": "159", "mapping": "0x11400", "complete": "1", "stable": "1", "digital_valid": "1", "digital_uv": "790000", "ready_valid": "1", "ready": "1", "commands": "16", "loads": "609", "maps": "2", "unmaps": "2", "released": "1", "elapsed_us": "1758", "pmic_setting_writes": "0"}`；电压上界比较 `True`。
独立字节解码：`{"apid": 159, "digital_uv": 790000, "ready": true, "raw_byte_sweeps": [[28, 9, 0, 142, 128, 128, 130, 131], [28, 9, 0, 142, 128, 128, 130, 131]], "type": 28, "subtype": 9, "mode_raw": 128, "step_raw": 130, "status_raw": 131, "analog_voltage_measured": false}`；不是模拟电压测量。
MX启动计划：`{"rail_id": 256, "internal_corner": 3, "kind": 1, "required_uv": 850000, "ceiling_uv": 850000, "floor_uv": 850000, "initial_uv": 850000, "captured_mode_corners": [3, 5, 8], "live_analog_voltage": false, "installed_rpm_private_copy_identity_verified": false}`。启动表不等实时模拟电压或RPM私有副本身份证明。

终态：`{"phase": "power_clock_cycle", "error": "0", "blocker": "0", "blocker_name": "none", "mx_plan": "1", "mx_required_uv": "850000", "bus_stable": "1", "bus_branches": "0", "ahb_valid": "1", "ahb_config_hz": "19200000", "rail_ready": "1", "mx_ready": "0x81", "cx_on": "1", "consumer_safe": "1", "original_verified": "1", "restore_ack": "1", "pmic_commands": "2", "retain_power": "0", "uncertain": "0", "clock_rc": "10", "clock_loads": "160", "clock_stores": "42", "native_maps": "4", "native_unmaps": "4", "vbif_valid": "1", "vbif_raw": "0x20040001", "vbif_pa": "0xb0c000", "chip_id_valid": "0", "hardware_chip_id_valid": "0", "inherited_bus_vote_ack": "0", "same_boot_bimc_rate_measured": "0", "bke_read": "0", "elapsed_us": "39320", "total_us": "200000", "gx_untouched": "0", "pll_untouched": "0", "smmu": "unread", "rpm_votes": "0", "mx_writes": "0"}`。
时钟与恢复：`{"error": "10", "cleanup_error": "0", "phase": "13", "failure_phase": "11", "configured_hz": "133000000", "readback_valid": "1", "cleanup_complete": "1", "retained_dependencies": "0x0", "dirty_registers": "0x3efed", "last_pa": "0x8c4110", "last_raw": "0x1"}`。
父分支与恢复：`{"used": "1", "saved_valid": "1", "saved": "0x1", "after": "0x1", "final": "0x1", "ready": "1", "restored": "1", "error": "0", "cleanup_error": "0", "reads": "3", "stores": "0", "maps": "1", "unmaps": "1", "dirty": "0", "uncertain": "0", "retained": "0"}`。

GPU时钟仅配置模型 `133000000`Hz，非PMU/频率计实测。
VBIF_VERSION raw `537133057`只表示对应访问与版本观测；硬件完整chip ID未验证。
本轮不装载固件/不提交CP，不等于GPU绘制或Metal加速。CPU614.4/R20/旧冻结保持，默认关。

USB `281`帧/`78240`raw/`69248`console独立逐字节重放；
接收器 `capture_failed` / `[Errno 32] Pipe error` 原样保留，源字节未修改。
