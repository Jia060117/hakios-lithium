# 初始化记录：2026-11-12（Asia/Shanghai）

## 设备实测

| 项目 | 结果 |
| --- | --- |
| 机型 / 代号 | Xiaomi MIX / lithium |
| SoC | MSM8996pro，SoC ID 305，revision 1.1 |
| GPU | Adreno530v5，sysfs 报告最大频率 652800000 Hz |
| 内存 | `/proc/meminfo` 的 MemTotal 为 5847268 kB，约为 6GB 机型 |
| 存储 | UFS，`/data` 总空间约 230G、可用约 227G |
| Android | 7.0 / API 24 |
| MIUI | V9 / V9.5.2.0.NAHCNFA |
| 安全补丁 | 2018-03-01 |
| 内核 | 3.18.31-perf-g0bf156d-01100-g2a3feef，aarch64 |
| Android 屏幕尺寸 | `wm size` 报告 1080×2040 |
| 帧缓冲 virtual_size | 1080×4080；这是虚拟缓冲尺寸，不是面板分辨率 |
| 密度 | 440 dpi |
| 触摸输入 | ft5x46，设备路径显示 I2C 12、地址 0x38 |
| 音频卡标识 | msm8996-tasha-mtp-snd-card |
| ADB | 已连接、已授权；普通 shell（uid 2000） |
| root | PATH 中未发现 su；未调用 su，也未尝试获取 root |
| BL | 用户确认已解锁；verifiedbootstate 为 orange；Fastboot 尚未独立复核 |
| 电池 | 首轮探测 10%，USB powered=true，status=2 |

## 已保存材料

- `scripts/probe-device.py` 已实际运行，详细结果位于 `reports/local/`。
- 设备树的 model、compatible、board-id、msm-id、pmic-id、address-cells、size-cells 已以二进制保存并计算 SHA-256。
- model：Qualcomm Technologies, Inc. MSM 8996pro + PMI8996 MTP。
- compatible：qcom,msm8996-mtp / qcom,msm8996 / qcom,mtp。
- board-id：十进制 `[38, 0]`；msm-id：`[305, 65536]`。
- pmic-id：十进制 `[131081, 65555, 0, 0]`；address-cells 和 size-cells 均为 2。

## 权限和资料缺口

- 完整 `/sys/firmware/fdt` 为 root-only，未取得完整 DTB。
- `/dev/block/bootdevice/by-name` 读取被拒绝。`/proc/partitions` 只有容量和内核编号，不能据此猜测刷写目标。
- 未取得原厂 boot/recovery 镜像或任何完整分区备份。
- `/proc/config.gz` 不存在，pstore 读取被拒绝。
- `/system/etc/firmware` 可见，但 `/firmware/image` 读取被拒绝；未把固件目录检查视为完整固件提取。

## 上游与主机

- HakiOS：`63fc2b4633bac3af8e57dadab9f5a8d4cb940438`。
- UniXNU：`c34f018aafd6befd5dc93c05a8dccd5bdce6724b`，20 个子模块按上游锁定提交下载。
- 两个上游均保持干净，精确版本另见 `upstreams.lock.json`。
- 在当前上游中未发现现成的 iOS 14 OS profile 或 lithium 设备适配；这不代表全网不存在相关工作。
- Windows ADB/Fastboot：37.0.1-15733141。
- WSL Ubuntu：x86_64；已确认 git、python3、clang、make、dtc 存在。
- 未安装额外构建依赖，未编译内核，也未验证 iOS 14 兼容性。

## 本轮完成范围

已完成项目骨架、上游下载、版本记录和手机只读探测。
手机保持 Android 开机；没有 root、重启、刷写、擦除或分区变更。
下一阶段优先处理原厂恢复材料、完整 DTB 和 boot 镜像，然后评估 lithium UEFI。
