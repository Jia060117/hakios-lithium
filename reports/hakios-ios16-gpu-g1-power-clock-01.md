# GPU G1组合电源/时钟01

单次RAM接受 `2026-11-26T02:00:09.673809+08:00`，镜像 `2977ffa87e5ae20dca945fd2f24998522f25ca83d67878d47a44fcace1d7c027`。
组合阶段通过 `False`，正常定时与日志排空 `True`，同机Fastboot终点 `False`。
返回核验记录：`{"passed": false, "verified": false, "status": "window_missed_tool_interruption", "accepted_at": "2026-11-26T02:00:09.673809+08:00", "observed_at": "2026-11-26T02:23:51.824751+08:00", "window_seconds": [240, 900], "device_queries": 0, "automatic_fastboot_observed": false, "note": "Tool interruption prevented timely verification; no extended window or late endpoint query."}`。错过原核验窗口不补认自动返回。

同次S1启动计划：`{"rail_id": 257, "internal_corner": 2, "kind": 1, "required_uv": 620000, "ceiling_uv": 620000, "floor_uv": 550000, "initial_uv": 620000, "captured_mode_corners": [2, 3, 5, 8], "live_analog_voltage": false, "installed_rpm_private_copy_identity_verified": false}`。
实际S1数字设定/ready：`{"error": "-2", "callback_error": "0", "apid": "0", "mapping": "0x0", "complete": "0", "stable": "0", "digital_valid": "0", "digital_uv": "0", "ready_valid": "0", "ready": "0", "commands": "0", "loads": "129", "maps": "1", "unmaps": "1", "released": "1", "elapsed_us": "222", "pmic_setting_writes": "0"}`；电压上界比较 `False`。
MX启动计划：`{"rail_id": 256, "internal_corner": 3, "kind": 1, "required_uv": 850000, "ceiling_uv": 850000, "floor_uv": 850000, "initial_uv": 850000, "captured_mode_corners": [3, 5, 8], "live_analog_voltage": false, "installed_rpm_private_copy_identity_verified": false}`。启动表不等实时模拟电压或RPM私有副本身份证明。

终态：`{"phase": "power_clock_cycle", "error": "1", "blocker": "1", "blocker_name": "MMPLL2_PM8994_S1_vdd_dig_unbound", "mx_plan": "1", "mx_required_uv": "850000", "bus_stable": "0", "bus_branches": "0", "ahb_valid": "0", "ahb_config_hz": "0", "rail_ready": "0", "mx_ready": "0x0", "cx_on": "0", "consumer_safe": "0", "original_verified": "0", "restore_ack": "0", "pmic_commands": "0", "retain_power": "0", "uncertain": "0", "clock_rc": "0", "clock_loads": "0", "clock_stores": "0", "native_maps": "0", "native_unmaps": "0", "vbif_valid": "0", "vbif_raw": "0x0", "vbif_pa": "0xb0c000", "chip_id_valid": "0", "hardware_chip_id_valid": "0", "inherited_bus_vote_ack": "0", "same_boot_bimc_rate_measured": "0", "bke_read": "0", "elapsed_us": "455", "total_us": "200000", "gx_untouched": "1", "pll_untouched": "1", "smmu": "unread", "rpm_votes": "0", "mx_writes": "0"}`。
时钟与恢复：`{"error": "0", "cleanup_error": "0", "phase": "0", "failure_phase": "0", "configured_hz": "0", "readback_valid": "0", "cleanup_complete": "0", "retained_dependencies": "0x0", "dirty_registers": "0x0", "last_pa": "0x0", "last_raw": "0x0"}`。
父分支与恢复：`{"used": "0", "saved_valid": "0", "saved": "0x0", "after": "0x0", "final": "0x0", "ready": "0", "restored": "0", "error": "0", "cleanup_error": "0", "reads": "0", "stores": "0", "maps": "0", "unmaps": "0", "dirty": "0", "uncertain": "0", "retained": "0"}`。

GPU时钟仅配置模型 `None`Hz，非PMU/频率计实测。
VBIF_VERSION raw `None`只表示对应访问与版本观测；硬件完整chip ID未验证。
本轮不装载固件/不提交CP，不等于GPU绘制或Metal加速。CPU614.4/R20/旧冻结保持，默认关。

USB `270`帧/`75151`raw/`66511`console独立逐字节重放；
接收器 `capture_failed` / `[Errno 32] Pipe error` 原样保留，源字节未修改。
