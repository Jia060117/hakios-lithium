# GPU reference verification — corrected audit

Date: 2026-11-18, Asia/Shanghai. Bounded host-only audit, started 19:21:36 +08:00. Only this report and gpu-upstream-components.md are owned outputs. Read AGENTS.md, PLAN.md, OPERATIONS.md, project.json. HakiOS remains the sole active kernel. No phone/device/USB command, QEMU, build, code edit, boot, flash, userdata operation or firmware download was performed by this audit. The user's concurrent admission-only build is not a physical-driver result and was not altered or independently verified here.

## Evidence authority and pins

All source citations use one-based lines of `git show <commit>:<path>`. Paths following aliases are repository-relative. Git objects, not sparse checkout visibility or downloaded filenames, are authoritative. Local roots below are under F:/IOS-MIX1/.

| Alias | Repository | Inspected commit |
|---|---|---|
| U | upstream/UniXNU | c34f018aafd6befd5dc93c05a8dccd5bdce6724b |
| G | upstream/research/UniXNU-g0245d | c4a44fa3588e49ed5fd9df9eba2ef2adfc7ca992 |
| S | upstream/research/UniXNU-msm8996-common | 4007997af4ed9db34c8dadcd44d9ce7a8b22ab7a |
| A | upstream/UniXNU/drivers/Adreno740 | 031f35ccfbb3da1007f44ff42e287d78fb4973f8 |
| N | upstream/UniXNU/drivers/AdrenoMetal | 51b7003f9c05ec46f0aa4ed7e9bb47f8db893c04 |
| M | artifacts/reference-source/overnight-20261118/mesa-25.0.7 | 742a20f48c59e8649533c84c4d49dd95b403f5da |
| L | artifacts/reference-source/overnight-20261118/linux-v6.12 | adc218676eef25575469234709c2d87185ca223a |
| D | artifacts/reference-source/overnight-20261118/hakios-arm64e-dada | f1ae906fcc43ba8f763663257724ba938fb820ab |
| P | artifacts/reference-source/overnight-20261118/dipper-xnu | 2854c1e22fc2d73bb7a890c2c84b80182ea5ad18 |
| H | artifacts/reference-source/overnight-20261118/hackios-xnu-userland | 2573c4a8d80ed143e534d16b5a386efa5b06ccbb |

A/N equal U's submodule gitlinks. U components.toml:86,108 advertises different revisions (6fa7bae…, 6d7f734…); code findings use inspected A/N. U/G/S/A/N had no tracked git diff --name-only HEAD changes when checked. Some reference worktrees/indexes are incomplete; missing checkout files are not missing Git objects.

## Exact Turnip correction and A530 exclusion

The earlier claim that the local Mesa reference lacks Turnip is withdrawn. M contains:

- src/freedreno/vulkan/tu_device.cc: blob 62ebfd0b38ad81ebb4a56a4d3f61ec3c25fbd40b.
- src/freedreno/vulkan/tu_knl_drm.cc: blob 27916d0fee839a0375be5ae4177bddb6fafd9683.

Both were read directly from pinned Git objects. The exclusion is in implementation code, not inferred from filenames:

| M file and exact lines | Observed code | Conclusion |
|---|---|---|
| src/freedreno/common/freedreno_devices.py:287-303 | GPUId(530) maps to CHIP.A5XX | A530 is generation5 in this database. |
| src/freedreno/vulkan/tu_device.cc:1405-1407 | switch(fd_dev_gen(...)), case6, case7 | These are the admitted Turnip generations. |
| src/freedreno/vulkan/tu_device.cc:1452-1455 | default: VK_ERROR_INITIALIZATION_FAILED, device unsupported, goto fail_free_name | A530/gen5 is explicitly excluded at this pin. |
| src/freedreno/vulkan/tu_device.cc:2430-2437 | a6xx/a7xx device-entrypoint dispatch | Corroborating generation-specific implementation. |
| src/gallium/drivers/freedreno/freedreno_screen.c:1046-1048 | case5 calls fd5_screen_init | A5xx Gallium support exists; this does not establish A5xx Vulkan/Turnip. |
| src/freedreno/vulkan/tu_knl_drm.cc:7-14,51-58 | libdrm headers and drmPrimeHandleToFD | A real DRM backend exists; it is not a Darwin or gen5 implementation by itself. |

This is a claim about Mesa25.0.7 at the pinned commit, not every Mesa version/fork. No A530-capable Turnip fork was verified. Replacing transport or changing a build option does not implement the missing gen5 Vulkan path.

## UniXNU four-component finding

U devices.toml:1281-1295 is G0245D/MSM8996/iPadOS17.7.11, not lithium. Line1293 names NativeUfs8996, Blsp8996, Adreno530, Msm8996Platform; lines1294-1295 mark the record invalid with a board/SoC mismatch. G board.toml:3-8 currently says msm8996; the error's cause/timing was not resolved.

G board.toml:146-153 describes local probe-only scaffolds/stub, and G porting.toml:43-45,172,221,238 explicitly denies working storage/I2C/GPU operation. However, **the actual bodies/build definitions of these four components are absent from the audited U/G/S tracked trees/catalog and actual U drivers/kits directories**. Thus Blsp8996/Adreno530 cannot be accepted as real transferable backends. Author-reported behavior remains unverified source-wise: Blsp8996 performs register reads, no I2C transactions; Adreno530 reads three unqualified RBBM offsets and performs no GPU submission. They are documented placeholders/probes, not demonstrated physical drivers. See the companion report for the exact classification and reproducible negative searches.

## Download validity audit

Directory: artifacts/reference-source/a5xx-packets/. Responses were not changed.

| Download | Result | SHA-256 |
|---|---|---|
| mesa-a5xx.xml | Rejected: 7505-byte HTML bot challenge, line1 | 1ef1081de9d9f32892394cf3cc1741ff0dd3de2cae7a454b818a059a25e9ae31 |
| mesa-adreno_pm4.xml | Rejected: 7505-byte HTML bot challenge, line1 | 0960624b5016a3e216a9d4d458f952b1e4f390c0f48d536e2219d6f992bd7560 |
| mesa-freedreno_ringbuffer.c | Rejected: 7505-byte HTML bot challenge, line1 | a457e524427563c3feeaca1d01fa0912a3145d003aa010f85589ecdc17b3a38b |
| linux-a5xx.xml | Byte-identical to L drivers/gpu/drm/msm/registers/adreno/a5xx.xml | 5d77e21ffdb5a82e8f26b37f00f26404b67be2a70c7f333fd7748c15416f0466 |
| linux-a5xx_gpu.c | Byte-identical to L drivers/gpu/drm/msm/adreno/a5xx_gpu.c | de1963851ba66ec013519a5bde67f483e2026773056c4b2f285a9c026c5928db |
| linux-a5xx_gpu.h | Byte-identical to L drivers/gpu/drm/msm/adreno/a5xx_gpu.h | 658dcec285ce7641f5def63f571e7587d4bd17fb62aea880116a77db6df60dd8 |
| linux-adreno_gpu.h | Byte-identical to L drivers/gpu/drm/msm/adreno/adreno_gpu.h | 7bdabea1ae7310697e0e2a4e0353cff2a7cf0ac2af746e5418e249d4584c86df |
| linux-adreno_pm4.xml | Byte-identical to L drivers/gpu/drm/msm/registers/adreno/adreno_pm4.xml | ab27047cc9cde4bc5996d6eb528fbb2dbdfdd9dc4515f75bbbada43a23809536 |
| mesa-freedreno_pm4.h | Byte-identical to M src/freedreno/common/freedreno_pm4.h | 74f9a3fda2ea27a21535ae310d911df3d30fb06f336eda70a9408be5309d996c |

Six byte streams validated, three rejected. This does not certify every prior download, its HTTP headers or firmware provenance. Rejected HTML was not used as register/packet evidence.

## Other corrected evidence boundaries

| Finding | Pinned evidence | Boundary/correction |
|---|---|---|
| Linux actual A530 reference | L drivers/gpu/drm/msm/adreno/a5xx_catalog.c:111-131,154-158 | Chip IDs05030002/05030004, revision530, 1MiB GMEM, a5xx_gpu_init, PM4/PFP/GPMU/ZAP filenames. Linux code, not HakiOS runtime support. |
| Secure zap path | L drivers/gpu/drm/msm/adreno/a5xx_gpu.c:19,642-677 | PAS13, SCM resume/zap load. Existing MIX secure state/ownership and required HakiOS sequence unverified; do not assume a new SCM load is necessarily required. |
| Actual fence emitter | L drivers/gpu/drm/msm/adreno/a5xx_gpu.c:219-227 | Event/IRQ, low/high fence address and sequence number. No MIX execution evidence. |
| Packet helper | L drivers/gpu/drm/msm/adreno/adreno_gpu.h:637-664 | Parity/type4/type7 source definitions. |
| XML overstatement withdrawn | L drivers/gpu/drm/msm/registers/adreno/adreno_pm4.xml:1614-1634 | Event/timestamp/IRQ/address defined; final payload word is ???, not a named value field. Sequence-number semantics come from the emitter. |
| Dada generation | D README.md:1-6,21; gpu2/build_plugin.sh:14,34-52 | A830/SM8750, arm64e/iOS26. Separate from UniXNU A740 and MIX A530. |
| Old AIR scope | D userland/docs/air.md:3-10 | Earlier compiler effort, not proof the later gpu2 object/submission pipeline is absent. |
| Dipper scope | P docs/STATUS.md:20; docs/build/USERLAND.md:55 | A630 author-reported results/firmware names, not A530 qualification. |
| Old userland repository | H README.md:1-7 | Moved pointer, no additional implementation at this pin. |

The previous final-host-suite/ASan/UBSan claim was not independently reproduced. Host models are not hardware drivers. Available NativeAdreno/Metal and Dada source plumbing is detailed in gpu-upstream-components.md. Missing target evidence includes A5xx Vulkan/Metal, iOS16.5 private ABI compatibility, power/clock/firmware/SMMUv2, DMA/fence/recovery and accelerated presentation. No physical-driver claim is made for admission-only integration.
