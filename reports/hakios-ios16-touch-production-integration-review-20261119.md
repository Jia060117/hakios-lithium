# Touch production integration review — 2026-11-19

Scope: read-only host review in F:/IOS-MIX1. Root AGENTS.md, PLAN.md, OPERATIONS.md and project.json were read first; subsequent reads followed the requested preparation/build, provenance, patch, runtime dependency and actual XNU include paths. No agents, device tools/queries, partition operations, QEMU, old-7195 commands, staging, commits or resets. Only this report is added to the repository. No proprietary binaries were opened, copied or packaged.

## Concrete recommendation

Reuse the existing port-copy and input-init hook. No new synchronization framework, driver translation unit or input-queue implementation is needed. The missing pieces are an explicit maintenance-header promotion list, runtime/provider binding in kernel.inc, and an honest runtime-specific provenance/build-check branch. The MMIO provider remains main-owned. **Do not run the existing preparer or build checker unchanged for a runtime integration.**

| Existing source location | Integration consequence |
|---|---|
| scripts/hakios.py:55–59, 177–216 | inputs() hashes the whole port; prepare() rejects unrecorded device-file edits, applies ordered common patches and copies port files to the isolated workspace's devices/lithium. verify_workspace() compares the exact copied tree. Keep this mechanism. |
| scripts/prepare-hakios-lithium-overnight.py:12–19, 37–53 | input_source() generates patch 0019; preserve this pure function. Its main currently copies only boot-options.h and overwrites an admission-only contract. Extend that bounded promotion step with the explicit list below and runtime metadata; do not replay its old main unchanged. |
| ports/hakios-lithium/patches/0019-lithium-peripheral-admission.patch:7–20 | Already includes ../../../../devices/lithium/peripherals/kernel.inc and calls lithium_peripheral_init(). Leave patch 0019 unchanged. |
| Actual XNU bsd/dev/hakios.input.c:324–338 | Both hook sites are present. Relative include resolves from xnu-src/xnu-8796.121.2/bsd/dev to workspace devices/lithium/peripherals. |
| ports/hakios-lithium/peripherals/kernel.inc:20–39, 59–80 | Retain explicit-request admission and affinity=1/max_logical=0 checks. Absent options return before worker allocation (:69–71). Add runtime/provider binding here; current stages 1–3 only log a block (:41–47). |
| scripts/hakios-dependencies.py:106–151, 301–349 | Normal driver/overlay synchronization moves patched input C and public header into the actual XNU tree and records fingerprints. Do not bypass it with hand copies. |

**Minimal source/provenance-safe hook path:** maintenance headers → explicit promotion into ports/hakios-lithium/peripherals → hakios.prepare() → devices/lithium/peripherals/kernel.inc → existing patch-0019 hook → normal sync-drivers → actual XNU bsd/dev/hakios.input.c → make kernel. Header-only helpers are compiled through the existing input translation unit; no drivers/map entry is needed for them.

## Exact header promotion list and graph

Copy each listed file, byte-for-byte, to **ports/hakios-lithium/peripherals/<same basename>**; the existing prepare step supplies the corresponding **devices/lithium/peripherals/<same basename>**. Tests stay in maintenance.

| Maintenance source | Direct include edges / source lines |
|---|---|
| maintenance/hakios-lithium/peripherals-next/touch-runtime.h | :4–7 → qup-combined.h, touch-stage-trace.h, touch-delivery.h, touch-static.h |
| maintenance/hakios-lithium/peripherals-next/touch-hakios-sink.h | :4 → touch-delivery.h; include after sys/hakios.input.h and sys/errno.h (:6–12) |
| maintenance/hakios-lithium/peripherals-next/qup-combined.h | :8–9 → ../peripherals/qup-transfer.h, ../peripherals/touch-i2c.h |
| maintenance/hakios-lithium/peripherals-next/touch-stage-trace.h | :4–5 → touch-stage.h, ../peripherals/touch-queue.h |
| maintenance/hakios-lithium/peripherals-next/touch-stage.h | :4 → ft5x46-frame.h |
| maintenance/hakios-lithium/peripherals-next/ft5x46-frame.h | :10–12 → stdbool.h, stddef.h, stdint.h |
| maintenance/hakios-lithium/peripherals-next/touch-delivery.h | :4 → ../peripherals/touch-queue.h |
| maintenance/hakios-lithium/peripherals-next/touch-static.h | :4 → stddef.h |
| maintenance/hakios-lithium/peripherals/qup-transfer.h | :9–12 → stdbool.h, stddef.h, stdint.h, string.h |
| maintenance/hakios-lithium/peripherals/touch-i2c.h | :4–6 → stdbool.h, stddef.h, stdint.h |
| maintenance/hakios-lithium/peripherals/touch-queue.h | :4 → touch-frame.h; R20 geometry :15–33 |
| maintenance/hakios-lithium/peripherals/touch-frame.h | :4–6 → stdbool.h, stddef.h, stdint.h |

All 12 basenames are distinct. Every quoted include was resolved against the proposed production destinations: **12-file closure passed**. Existing ../peripherals/ references resolve back to the same production peripherals directory, so no include rewriting is required. At review start, the production-input inventory in ports/hakios-lithium/peripherals comprised boot-options.h, contract.json and kernel.inc. Main has since added MMIO policy/access candidates (including touch-mmio-policy.h); per main's update, these are not compiled and are outside this review's verified production-input set. This is a historical production-input inventory, not a current directory listing.

Additional edit list for main: the preparer's bounded promotion block; ports/hakios-lithium/peripherals/kernel.inc and contract.json; a new sibling **scripts/hakios_touch_runtime.py** provenance helper; runtime dispatch/checks in scripts/review-hakios-ios16.py and scripts/check-hakios-ios16-build.py. Provider source/header dependencies must be added explicitly by main, not assumed included in this 12-header closure. No change to scripts/hakios.py, hakios-dependencies.py or patches 0019/0020/0021 is needed for this header/glue integration itself.

## Provenance and frozen-output gates

- Preserve scripts/hakios_touch_stage0.py unchanged. Its REVIEWED_DELTA (:11–17) and validate_delta() (:34–39) permit exactly five post-R20 input changes; source_checks() (:41–71) explicitly requires no physical backend and stages 1–3 blocked. Adding headers/glue must fail that historical gate, not silently broaden it.
- The new runtime helper should retain R20/R15 frozen checks and R20 source ancestry, then validate its own exact delta: all 12 promoted headers, kernel.inc, contract.json and any main-owned provider/patch inputs. Record full maintenance-source → promoted-port → actual devices/lithium hashes and reject missing/extra/drifted inputs. Keep baseline commit and all non-port manifest inputs pinned. Stage 0 evidence remains Stage 0 evidence.
- scripts/review-hakios-ios16.py:34–68 currently reconstructs the exact 0019+0020 input/header result, requires admission-only flags and verifies only parser/body hashes. Keep reconstruction of the unchanged hook/API, dispatch on an explicit runtime contract marker, and verify the full promoted closure/provider contract. Metadata must distinguish compiled capability, default-off behavior and hardware verification; do not leave misleading admission-only claims after adding a provider.
- scripts/check-hakios-ios16-build.py:86–95 currently selects the Stage 0 helper merely because patch 0021 is present. Add runtime dispatch first. Its :232–251 also selects phase4-touch-stage0 and a fixed existing Stage 0 report; use a fresh runtime-specific output/report path and preserve existing outputs. It deliberately refuses existing destinations.
- scripts/hakios.py:246–264 requires fresh source-review inputs, exact dependency fingerprints, overlay and actual-driver hashes before building. Refresh those normally after promotion. prepare() can internally use git add --intent-to-add for added patch files (:158–163); none was invoked in this review. The proposed unchanged patches do not add files; promoted headers use device copying.
- Do not run scripts/prepare-hakios-ios16-r19.py:12–17: it is historical and refuses an existing frozen R19 contract. scripts/build-hakios-ios16-touch-r21.py:22–28, 64–77, 127–143 is a fixed old Stage 0 selector/loader build with a pinned kernel and fixed overlay allowlist; it does not build this runtime kernel.

Frozen image hashes are reference values from the root controls/helper, **not freshly rehashed in this review**: R15 ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55; archived R19 2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a; sole accepted R20-display 3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82. No frozen image/contract was written. Preserve bootcpu=1 cpus=1 and affinity0x1; retain 1080×2040 BGR24/stride3240, 360×680 points/scale3. The active generated .config has CONFIG_CPUS=1 but no CONFIG_BOOTCPU/FBWIDTH/FBHEIGHT values: configuration generation alone does not prove the final R20 payload arguments. Independently compare the future package command line against R20.

## Actual XNU environment and queue limits

Workspace: /home/jia/ios-mix1-build/hakios-lithium-ios16; actual tree: xnu-src/xnu-8796.121.2. Existing bsd/DEVELOPMENT/hakios.input.d records kernel.inc/boot-options.h at :288–289 and production stdint/stdbool/stddef/string headers at :26,59,62,124. makedefs/MakeInc.def:304–313 uses -nostdinc; :886–893 supplies component/exported/EXTERNAL_HEADERS/kernel-SDK include paths. The existing wrapper xnu-deps/host/bin/xnu-clang:20–21 forces arm64-apple-ios16.5 and the local compatibility header. Do not test against host libc headers and call that production compatibility.

Recorded flags: actual BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/.CFLAGS SHA256 **b801a32ea5ea2c62278c243b44b8026c0e472dbfe256bec9eee2cc646797cd95**. Both unchanged actual input and an in-memory insertion of the maintenance runtime/sink before kernel.inc passed -fsyntax-only with these flags. A second probe using the wrapper-equivalent /usr/bin/clang command and -Werror=implicit-function-declaration also passed both. Actual preprocessor macros include UINT32_C, UINT64_C and UINT32_MAX/UINT64_MAX. No compatibility-header change was shown necessary. These probes check the existing includes plus candidate headers; they do not compile a provider binding, link a new kernel or prove promoted-file build dependencies.

- Existing queue API: ports/.../patches/0020-lithium-touch-batch-queue.patch:24–70; actual bsd/dev/hakios.input.c:199–245; public bsd/sys/hakios.input.h:38–48. It validates 1–20 touch records, publishes the whole batch or rejects it, timestamps with mach_absolute_time(), returns EINVAL/ENOBUFS and wakes readers once. Queue capacity is 512 (:17).
- **There is no exported nonblocking/try/deadline batch-queue API in the inspected production header/source.** Full capacity causes no wait for space, but lck_mtx_lock() at actual :218 can wait without a caller deadline. Optional touch-latency logging also executes under that lock (:231–237). Legacy single-event posting (:167–190) can drop the oldest event and is not an atomic-batch substitute.
- XNU osfmk/kern/lock_mtx.h:142–145 exposes lck_mtx_try_lock(), a nonblocking primitive, not an existing queue entry point. No queue code was added. A strict bounded Stage 3 publication guarantee remains unresolved with the existing mutex API.
- touch-hakios-sink.h:66–85 checks the deadline before publication; it cannot preempt mutex acquisition. runtime :247–278 marks late acceptance uncertain and does not retry. Queue acceptance is not downstream HID release/receipt; sink :88–97 supplies no resync, and runtime :31–41 requires a real acknowledged fence for cleanup.
- runtime :9–23, 389–414 requires one serialized executor and monotonic microseconds, with at most one probe/poll per step. Existing thread_call_enter_delayed() takes absolute-time units and explicitly warns about overlapping self-reschedules (actual osfmk/kern/thread_call.h:116–129). Convert units and serialize lifetime/step/cancel; busy only detects recursion. Cancellation is not itself quiescence (:212–240). No scheduler or queue was implemented in this review.

## Commands and results

All commands ran from F:/IOS-MIX1. Read commands were PowerShell Get-Content and bounded rg/path inspections; git --no-optional-locks status --porcelain=v1 was read-only, exit 0. One early rg call incorrectly supplied literal Windows wildcard paths (rg reported error 123 / exit 2; the mixed PowerShell command ended 0). It was corrected to rg ... scripts -g 'prepare-hakios*.py' -g 'build-hakios*.py' -g '!*-ios14-*', exit 0. Initial combined root output was truncated and followed by bounded rereads. No build/check/preparer CLI was executed.

The actual compiler commands below were launched by Python subprocess.run inside **wsl.exe -d Ubuntu -- python3 -B -**, outer exits 0. Syntax commands used the exact recorded .CFLAGS argv (trailing NUL removed and the unquoted multiword CONFIG_NMBCLUSTERS define regrouped into one argument), with the compiler resolved to workspace xnu-deps/host/bin/xnu-clang and cwd set to the existing bsd/DEVELOPMENT build directory:

| Actual command / input | Exit |
|---|---:|
| xnu-clang [recorded .CFLAGS arguments] -fsyntax-only /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2/bsd/dev/hakios.input.c | 0 |
| xnu-clang [same arguments] -fsyntax-only -x c -; stdin = actual input C with only the hook include expanded to absolute maintenance runtime.h, sink.h and existing production kernel.inc | 0 |
| xnu-clang [same arguments] -dM -E -x c -; same candidate stdin | 0 |
| /usr/bin/clang -include <workspace>/xnu-deps/hostinc/xnu_clang_compat.h -target arm64-apple-ios16.5 --ld-path=<workspace>/xnu-deps/host/bin/ld64 [same arguments with wrapper-filtered flags removed] -Wno-everything -Werror=implicit-function-declaration -fsyntax-only -x c -; actual control, then candidate stdin | 0 / 0 |

Exact lightweight test commands (each compile **0**, each subsequent executable **0**):


```bash
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals/boot-options-test.c -o /tmp/touch-production-review-8janz0k7/0  # exit 0
/tmp/touch-production-review-8janz0k7/0  # ASAN_OPTIONS=detect_leaks=1:halt_on_error=1; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1  # exit 0
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals/kernel-admission-test.c -o /tmp/touch-production-review-8janz0k7/1  # exit 0
/tmp/touch-production-review-8janz0k7/1  # ASAN_OPTIONS=detect_leaks=1:halt_on_error=1; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1  # exit 0
/usr/bin/clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer /mnt/f/IOS-MIX1/maintenance/hakios-lithium/peripherals-next/touch-runtime-test.c -o /tmp/touch-production-review-8janz0k7/2  # exit 0
/tmp/touch-production-review-8janz0k7/2  # ASAN_OPTIONS=detect_leaks=1:halt_on_error=1; UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1  # exit 0
```

Tests verified existing default-off/malformed/CPU/allocation/stage-blocking behavior and runtime-model gates/deadlines, including 178 injected MMIO-model faults + 178 callback-deadline cases. Runtime-model MMIO is not physical MMIO. Temporary test artifacts were removed. Existing kernel-admission-test.c:41–63,71–80 covers production default-off/Stage0/blocked stages; runtime-test.c:270–273 covers absent configuration. After real glue changes, extend these checks to count zero provider/clock/IRQ/queue calls for absent, malformed and unverified requests; rerun against the changed production hook and full promoted closure.

## Deferred production build path — not executed

After main supplies the provider, exact promotion contract and fresh provenance/output dispatch, execute sequentially under the approved resource wrapper:

    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-wsl-limited.ps1 -Task hakios-prepare
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-wsl-limited.ps1 -Task hakios-check
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-wsl-limited.ps1 -Task hakios-deps

Refresh source review with the existing scripts/review-hakios-ios16.py under the same resource envelope; there is no neutral review task in the inspected wrapper, so use its direct host invocation rather than a historical R19 task:

    wsl.exe -d Ubuntu -u root --exec systemd-run --unit=ios-mix1-hakios --wait --pipe --collect -p MemoryMax=14G -p MemoryHigh=12G -p MemorySwapMax=1G -p CPUQuota=1200% -p TasksMax=1024 -p User=jia -p WorkingDirectory=/mnt/f/IOS-MIX1 --setenv=JOBS=12 --setenv=MAKEFLAGS=-j12 --setenv=MAKEJOBS=--jobs=12 /usr/bin/python3 /mnt/f/IOS-MIX1/scripts/review-hakios-ios16.py
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-wsl-limited.ps1 -Task hakios-kernel

The kernel task actually invokes make kernel SHELL=/bin/bash MAKEJOBS=--jobs=12 DEVICE=lithium JOBS=12 (scripts/hakios.py:269–279). Workspace Makefile:74–77 → tools/make-kernel.sh:45–54 → sync-drivers/config → xnu-build.sh:20–22 → DEVELOPMENT/ARM64/BCM2837 XNU, with LITHIUM_ARMV8_0=1. Verify fresh dependency files name all promoted headers and provider, and the linked kernel has the actual runtime/glue, not just static diagnostic strings. The existing make-kernel judge accepts only known kext symbol-set failures; do not treat arbitrary raw make failures as success.

For a later complete package, normal support and userspace build paths are scripts/build-hakios-support.py:19–35 and scripts/build-hakios-ios16-userspace.py:34–74 (--phase4). They write shared build outputs/reports and are deferred here, as are build checks, package checks and new freeze. Do not use archived revision package/freeze entry points. Preserve existing R15/R19/R20 and Stage 0 outputs; no touch package is ready from this review.

Unverified: production promotion/binding, linked runtime kernel, new package and final payload arguments, QUP6 secure-world ownership/handoff, mapping/access, clock/pins/rails, GPIO105/125 IRQ, controller runtime ID/reports, physical touch/HID delivery, strict publication deadlines and downstream cleanup acknowledgment. The three targeted tests do not rerun or replace the earlier 26-suite gate. Main owns MMIO work. No physical inference is made from host success.

Future sleh hook, only if separately approved: per main's closing update (not independently re-audited here), the root HakiOS patch currently has no sleh hunks and the actual sleh source is unmodified. Normal sync_kernel_overlay()/sync_kernel_drivers() (scripts/hakios-dependencies.py:106–151,301–349) will not automatically install a new sleh change. Use a reviewed, reproducible patch path with explicit installation into the actual pinned XNU source, guarded by prior-state source hashes and dependency fingerprints; record the installed source/patch provenance and verify it through prebuild gates and an actual kernel build (scripts/hakios.py:134–144,246–264). A new hook cannot use copyio or a no-op ABI stub. This conditional note adds no hook implementation or build evidence.

