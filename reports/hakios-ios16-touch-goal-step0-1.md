# HakiOS iOS 16 lithium touch migration: baseline and freeze-hash audit

日期：2026-11-19（Asia/Shanghai）。范围：按用户最新确认，以 R20-display 为唯一显示基线；复核旧 R19 冻结门禁涉及的共享报告哈希。没有构建、查询或操作手机，没有运行 ADB/Fastboot/QEMU。

## 基线裁定

用户确认 R19 曾上机，但显示分辨率不正确；R19 归档，不再复测或修复。R20-display 是当前显示基线，用户确认尺寸比例正常。当前工作树 R20 镜像哈希为 `3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82`；R15 桌面基线哈希为 `ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55`。

## R19 合同与当前共享路径差异

对 `artifacts/hakios/lithium-ios16/phase4-r19/contract.json` 与 `evidence/` 原始字节、当前共享路径独立计算 SHA-256，确认有三项后续主机工作变更：

| 文件 | R19 冻结 SHA-256 | 当前 SHA-256 |
|---|---|---|
| `scripts/review-hakios-ios16.py` | `7984ebe2824c1c542b164ee06574b9e5f2180db16f0603fc8ba6095b878dc3b7` | `c3b4991e0318ac4f4a688282ee7ad3eef43ee468ca2645f8e249afd84ce680a8` |
| `reports/hakios-ios16-source-review.json` | `3f8e64fc995e94157f8c0c9e4947c0233b1650bf93640f1b8108468727aaed48` | `abc82892cf3df12398a3db8f6ba3e622d24ab9d80ae1e79edb96fc772cde3659` |
| `reports/hakios-kernel-build.json` | `de8437709fed1193fdcfd71764f52c84c0d6547c0f6f40bc7788dbe4cb8c0299` | `e88d35b7b5038bf678fa32d2c1e058b9f4c40f1c9404b36d5b912a52ba9d9535` |

The change is explained by the host integration sequence on 2026-11-18: at 19:25:47.422 UTC the source-review script was updated to exercise the 0020 touch-batch queue patch; at 19:26:08–19:26:20 the R19 review task regenerated its shared JSON; at 19:27:54–19:28:20 the `hakios-kernel` task regenerated the shared kernel report. The writer is `scripts/hakios.py:279`. These are later mutations to mutable shared paths, not evidence that the archived R19 package changed.

The archived R19 contract binds `reports/hakios-kernel-build.json` to SHA-256 `de843770…0299` and the R19 kernel to `8e9209c1…9b4b`. The archived contract and its evidence copy still match. The current shared report instead records candidate kernel `32cec599…52a13b1`. The corresponding saved log contains the upstream kext export/symbol-set make failure (inner make exit code 2); an outer wrapper exit code 0 only records artifact production under that existing wrapper behavior, not a clean full build or releasable touch package.

## Disposition

- Do not edit or weaken the R19/R15/R20 frozen contracts to make mutable-path checks pass.
- Do not use the `32cec599…` candidate as a touch release. The current touch code is host-only; it is not connected to the active kernel and this audit did not build an image.
- Any eventual QUP6/FT5x46 package must use a new revision and a new immutable contract, retain R20-display’s display bytes/parameters, and independently verify the frozen R15, R19, and R20 image hashes before and after packaging.
- R20-display’s parent is the R19 image, but its own verified image is the active baseline. R19 is not required to pass a new display test.

## Evidence paths and source points

- Archived contract and evidence: `artifacts/hakios/lithium-ios16/phase4-r19/contract.json` and `artifacts/hakios/lithium-ios16/phase4-r19/evidence/reports/`.
- Current reports: `reports/hakios-ios16-source-review.json`, `reports/hakios-kernel-build.json`, and `reports/overnight-20261118/build-evidence.json`.
- Report writer: `scripts/hakios.py:279`. Review writer and task sequence are detailed in `reports/hakios-ios16-touch-freeze-audit.md`.
- Existing build summary and logged kext symbol-set limitation: `reports/overnight-20261118/REPORT.md` and `reports/overnight-20261118/build-evidence.json`.
