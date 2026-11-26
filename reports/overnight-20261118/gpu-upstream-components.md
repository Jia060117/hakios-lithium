# GPU upstream components — implementation and API audit

Date: 2026-11-18. Host-only; only this report and gpu-reference-verification.md are writable outputs. Full commits/roots and aliases U/G/S/A/N/M/L/D/P/H are pinned in the companion report. All line references are Git-object source lines. No device/QEMU/code/build operations by this audit.

## 1. Four MSM8996 names: reported probes/stub, not verified backends

U devices.toml:1281-1295 records Gree G0245D/MSM8996/iPadOS17.7.11. Line1293 lists the four names; lines1294-1295 mark the record invalid. This is neither a lithium entry nor validation of actual backend code.

| Component | Pinned documentary evidence | Implementation audit / classification |
|---|---|---|
| NativeUfs8996 | G board.toml:35-40,143-153; porting.toml:169-173 | Author reports local CAP/VER/etc register probe, no UIC/UTP transport. Source/build body absent from audited trees. Name/config available; backend not verified. |
| Blsp8996 | G board.toml:38-40,146-149; porting.toml:207-222 | Author reports QUP_CONFIG/STATE/OPERATION/ERROR_FLAGS/HW_VERSION reads, **no I2C transaction**, HW_VERSION offset still unconfirmed. Actual implementation absent. Documented probe/placeholder, not verified I2C backend. |
| Adreno530 | G board.toml:146-153; porting.toml:230-240 | Author reports three RBBM reads, **Adreno530OffsetsQualified=false**, no clock/power/VBIF/ringbuffer work; A5xx has no GMU per the same note. Body absent; offsets/behavior not independently verified. Documented probe/placeholder, not GPU execution backend. |
| Msm8996Platform | G board.toml:150-152; porting.toml:43-45,238 | Author reports hooks returning -1 and capabilities0. Board says compiled separately; porting.toml:238 says not compiled/linked. Omitted source/build evidence prevents resolution. Reported stub, build/link state unverified. |

The audit examined complete tracked path lists for U/G/S, U components.toml and actual U drivers/kits directories. No implementation/build registration for these IDs was found; U content search matched only devices.toml:1293. Therefore there is no implementation file/line to cite for these four components in available audited sources. Documentation describing local code is not independent verification of that code. This bounded absence says nothing about inaccessible local/private copies or all remote revisions.

S soc.toml:10-14 selects only ArmCpu/ArmDTPlatform and excludes the A7xx GPU selection. S README.md:27-31,49-58,75-77 describes missing/new storage/GPU/BLSP work. G feeds.toml:1-8 pins S at4007997af4ed9db34c8dadcd44d9ce7a8b22ab7a. Neither supplies the missing four implementation bodies. G's build/link claims were not rerun.

Reproduce without checkout/fetch (run separately):

```powershell
git -C upstream/UniXNU ls-tree -r --name-only c34f018aafd6befd5dc93c05a8dccd5bdce6724b
git -C upstream/UniXNU grep -n -E 'NativeUfs8996|Blsp8996|Adreno530|Msm8996Platform' c34f018aafd6befd5dc93c05a8dccd5bdce6724b -- .
git -C upstream/research/UniXNU-g0245d ls-tree -r --name-only c4a44fa3588e49ed5fd9df9eba2ef2adfc7ca992
git -C upstream/research/UniXNU-msm8996-common ls-tree -r --name-only 4007997af4ed9db34c8dadcd44d9ce7a8b22ab7a
```

## 2. Actual NativeAdreno API and A740 hardware code

Inspected A031f35ccfbb3da1007f44ff42e287d78fb4973f8 matches U's Adreno740 gitlink. U components.toml:80-99 identifies A740/SM8550, dependencies Sm8550Platform/AdrenoPM4/AdrenoFirmware/ArmSmmu4K. Its advertised revision differs from inspected A.

| Layer | Pinned implementation evidence | Established scope |
|---|---|---|
| Wire contract | A NativeAdrenoABI.h:6-42 | Version2, fixed little-endian records, selectors1–18; explicitly kernel/client ABI, not Metal or Linux ioctl ABI. |
| BO/import | A NativeAdrenoABI.h:48-68,106-117 | Allocation/map/import caller pages and CPU-map token/cache/lifetime contract. |
| Submit/fence | A NativeAdrenoABI.h:77-102 | Up to128 handle/IOVA/byte buffers, queue, wait fence/timeout and returned fence. |
| Device facts | A NativeAdrenoABI.h:145-164 | Chip/GMEM/UCHE/VA/bank/UBWC values required from actual backend. Struct compatibility is not A530 hardware support. |
| Core dispatch | A NativeAdrenoUserClient.cpp:129-133,207-211,541-543,616-638 | Actual ABI checks and submit/fence/device-info code. |
| Published IOKit route | A NativeAdrenoPublish.cpp:66-72,1012-1021,1039-1094 | Actual AGXDeviceUserClient/import and18-entry dispatch/tunnel, magic0x4e544e4c. |
| Distinct old service route | A NativeAdrenoService.cpp:589-596,610-632 | CPU-map descriptor path refuses and table has14 entries. This is not the published route above; do not infer global API completeness/absence from it. |
| Hardware startup | A NativeAdrenoGpu.cpp:12-40 | Requires bootGmu/SMMU/SQE/power-list inputs and waits for init fence. |
| CP init implementation | A NativeAdrenoCpInit.cpp:6-19,29-59 | Programs SQE base/A7xx-derived thread/init/power-list sequence. Not A530 PM4/PFP/GPMU initialization. |

Real code is available as ABI/ownership/error-handling reference. It does not supply a drop-in A5xx hardware backend; no A740 runtime result was reproduced.

## 3. Actual NativeMetal frontend/bridge

Inspected N51b7003f9c05ec46f0aa4ed7e9bb47f8db893c04 matches U's gitlink; U components.toml:102-119 identifies the metal2vulkan/Turnip design and Adreno740 dependency.

| Stage | Pinned evidence | Finding |
|---|---|---|
| IOKit binding | N NativeAdrenoMetalPlugin.mm:80-89 | Resolves struct calls/map/unmap/service open/close. |
| Discovery/open | N NativeAdrenoMetalPlugin.mm:217-275,657-671 | Finds Turnip and calls native_bridge_metal_open; default/test versus published paths differ. |
| Metal object | N NativeBridgeMetalFrontend.mm:5080-5091,5111-5124 | nmb_device_open, failure refusal, target _MTLDevice subclass/private ABI assumptions. |
| Resource/commit code | N NativeBridgeMetalFrontend.mm:3501-3508,4458-4474 | Actual command-buffer scheduling/completion/commit and buffer creation bridge calls. |
| Runtime loader | N bridge/NativeMetalBridge.rs:90-95 | Loader/cache options passed to Device::system_default_with. |
| Missing selectors | N NativeBridgeMetalFrontend.mm:916-925 | Logs and can answer nil/zero. Presence is not complete Metal correctness. |

Metal-object→bridge/runtime→Vulkan/Turnip plumbing exists. iOS16.5 private API qualification, complete features and compatible A5xx backend remain unverified. The older file-header missing-feature list was not treated as a current exhaustive implementation inventory.

## 4. Exact A530 Turnip exclusion

M742a20f48c59e8649533c84c4d49dd95b403f5da contains both tu_device.cc and tu_knl_drm.cc; the old missing-Turnip assertion is false.

- src/freedreno/common/freedreno_devices.py:287-303: GPUId530 maps to CHIP.A5XX.
- src/freedreno/vulkan/tu_device.cc:1405-1407: switch on fd_dev_gen admits case6/case7.
- Same file:1452-1455: default returns VK_ERROR_INITIALIZATION_FAILED with device unsupported and exits initialization. **A530/gen5 is excluded.**
- Same file:2430-2437: only a6xx/a7xx device-entrypoint dispatch.
- src/gallium/drivers/freedreno/freedreno_screen.c:1046-1048: gen5 calls fd5_screen_init. This proves a separate A5xx Gallium route exists, not A5xx Vulkan.
- src/freedreno/vulkan/tu_knl_drm.cc:7-14,51-58: actual libdrm transport code; transport changes do not implement gen5 Vulkan.

No alternate A530-capable Turnip fork was verified; no global impossibility/absence claim is made.

## 5. Dada gpu2 source chain: A830, not A530/A740

D f1ae906fcc43ba8f763663257724ba938fb820ab README.md:1-6,21 identifies Xiaomi15/SM8750/Adreno830 (A8xx). UniXNU A is A740/A7xx; MIX is A530/A5xx. Dada runtime reports remain the author's claims, not independent MIX evidence.

| Link | Pinned D evidence | Verified source scope |
|---|---|---|
| Bases | gpu2/BASES.txt:1-4 | Short pins metal2vulkan720ef1c, NativeMetalc6d29ca, Mesa85fda3b; XNU12377.121.6. Full dependency hashes not resolved here. |
| Rust bridge | gpu2/build_bridge.sh:19-33,39-58 | Static bridge/metal2vulkan runtime, iOS26/custom arm64e pointer authentication. |
| Turnip build | gpu2/build_turnip_ios.sh:15-39,45-61 | arm64e/iOS26, freedreno Vulkan, Darwin KMD/NativeAdreno ABI dir; expects vk_icdGetInstanceProcAddr. Script not executed. |
| Plugin | gpu2/build_plugin.sh:10-17,34-52 | Objective-C++ frontend/Rust bridge and Turnip dylib packaged as RedwoodMetal.bundle with principal class/minimum iOS26. |
| Darwin implementation | gpu2/patches/mesa-85fda3b-dada.patch:165-175,1171-1192 | Adds actual tu_knl_darwin.cc source/framework linkage, beyond build-option claims. Patch application/build unverified here. |
| Service/ABI selection | Same patch:1305-1334 | Tries rw_open by default, queries params/device info, compares compiled NativeAdrenoAbiVersion. Older version1 comment at1178 does not identify linked header version. |
| Redwood adapter | Same patch:2333-2366,2625-2667,2917-2932 | Adds tu_knl_darwin_redwood.h translating NativeAdreno operations to RedwoodGPU IOKit/MSM-style records. Code queries chip/GMEM/VA, allocates and opens service. Not unchanged A740 driver. |
| Kernel receiver | base/xnu/files/iokit/Kernel/RedwoodGPU.cpp:1166-1227 | Real get-param/GEM-new/info/submit handlers, backend-ready gate. Plumbing, not independently verified device execution. |

External directories gpu2/src/mesa, nativeadreno, nativemetal, metal2vulkan, data-a-live are not tracked in D's gpu2/src tree. D supplies scripts/patches, not full self-contained dependency trees. No giant clone was attempted. Mesa85fda3b/newer-gen pipeline is separate from Mesa25.0.7's gen6/7 gate.

D userland/docs/air.md:3-10 describes an earlier AIR→NIR effort and says its other two thirds were not there. Later gpu2 code prevents interpreting it as repository-wide absence of Metal objects/submission. Neither old AIR statistics nor newer README claims are target acceptance results.

## 6. Available, missing, unverified

| Area | Available reference | Missing/unverified for HakiOS/lithium |
|---|---|---|
| A530 | Linux A5xx implementation; Mesa A5xx Gallium | Working HakiOS A530 backend/execution. |
| Blsp8996/Adreno530 | Documentary probe descriptions | Actual omitted bodies; no accepted I2C/GPU backend. |
| Native client | A ABI/BO/queue/fence/IOKit code | A5xx hardware/cache/VM ownership/recovery. |
| Metal | N frontend/runtime/resource/commit plumbing | iOS16.5 qualification and A5xx execution stack. |
| Vulkan | M gen6/7 Turnip; D newer Darwin/Redwood patch | M explicitly excludes A530; alternate implementation unverified. |
| Power/firmware | Linux PM4/PFP/GPMU/ZAP/PAS13 references | MIX clocks/power/secure ownership/SMMUv2 and firmware execution. |
| Presentation | N handoff/D surface-import source | Fence/IOSurface lifetime/scanout correctness and accelerated SpringBoard. |

Admission-only integration, a successful build, or no-MMIO policy does not establish physical-driver behavior. This audit edits no shared source and changes no GPU flags/kernel route/device state.
