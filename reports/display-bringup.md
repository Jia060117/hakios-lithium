# lithium 原厂显示交接采样

2026-11-12，在同一台已核验的 MIX 一代上完成一次 `fastboot boot` 显示探针启动，随后普通重启回原厂 Android。原始结果与哈希绑定见 `display-state.json`、`display-probe-build.json` 和 `display-probe-qemu.json`。

## 实测结果

| 项目 | 入口采样值 |
| --- | --- |
| 活动内容所在管线 | VIG0 |
| 帧缓冲 | `0x83401000` |
| 图像与源尺寸 | 1080 × 2040 |
| 每像素字节数 | 3，24 位 |
| stride | 3240 字节 |
| SRC_FORMAT | `0x2243f` |
| SRC_UNPACK_PATTERN | `0x20001` |
| SRC_OP_MODE | `0x80000000` |
| MDSS HW_VERSION | `0x10070002` |
| CTL0 LAYER0 / TOP | `0x1` / `0x1f20` |
| FDT memreserve 表 | 已读到完整终止项，表内没有条目 |

126 个寄存器地址由匹配原厂 DT 和 MDSS 源码定义生成白名单；探针只读这些显示寄存器，没有写显示控制器。记录显示可见图像范围完全落在 `cont_splash_mem` 保留区内，采用线性布局。按参考 MDSS 的 unpack 元素定义，对应 B、G、R 字节顺序。

原始 memreserve 表为空不表示 `/reserved-memory` 节点或其他 DDR 空闲。Android 后期的 32 位像素及 4080 虚拟高度也不适用于本次入口状态。Gree 板的旋转、偏移与颜色参数没有移入本项目。

QEMU 的十个用例全部通过，包含确定性 MMIO 模拟值与 memreserve 拷贝检查；手机采样没有捕获异常。镜像 SHA-256：`7ab7d822a7840f7263bb7558d4b154a09d72143d658787a1a46f5af49fc5b761`。

## 源码依据与边界

寄存器定义取自 `LineageOS/android_kernel_xiaomi_msm8996` 的 `lineage-18.1` 分支，下载文件及 SHA-256 留在 `artifacts/research/mdss/sources.json`。它们不是原厂内核的同一提交；具体寄存器值来自本机采样。实际屏幕方向、面板颜色以及面板长期运行仍须单独观察，不能仅凭寄存器非零证明。

## 复现

```powershell
python scripts/build-entry-probe.py --profile display
wsl -d Ubuntu --exec python3 /mnt/f/IOS-MIX1/scripts/validate-entry-probe.py --profile display
python scripts/boot-entry-probe.py --boot --profile display --runs 1
```

第三条命令操作真机；前两条离线执行。所有二进制采集使用 Python 捕获 `adb exec-out`，避开此 MIUI shell 的换行改写。
