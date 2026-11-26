# HakiOS iOS 16 lithium touch: Step 2 host preparation

Date: 2026-11-19. Scope: project parallelism rule, source audit, host-only queue candidate and static QUP clock mapping. No phone query, boot, ADB/Fastboot command, partition operation or QEMU was performed. No R21/R20 image was changed or produced.

## Result

The repository rule now says to parallelize independent work, with up to nine concurrent subagents, isolated write sets, main-agent review and bounded reassignment. See `AGENTS.md` under “并行执行规则”.

R20-display remains the last hardware-confirmed display package: the user reported the iOS home screen and normal screen proportions. That does not verify physical touch. The current peripheral admission hook still fails closed and reports `mmio=0`, `irq_attached=0`, `hid_events=0`; no existing package can exercise the physical controller.

The audit confirms the supplied stock DTS describes I2C12 at `0x075ba000`, device address `0x38`, input clock 19.2 MHz and configured bus rate 400 kHz (`artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts:4844-4865`). GPIO87/88 use the `blsp_i2c12` pin group (`:14221-14252`). The matching Linux source revision `LineageOS/android_kernel_xiaomi_msm8996@9c0359c4b8b68752e7a7486fac33c1896420d2b4` identifies the QUP6 I2C source command register at GCC offset `0x30020`, and branch/halt register at `0x30008` with enable bit 0 (`drivers/clk/qcom/gcc-msm8996.c:1069-1081,2197-2210`). With GCC base `0x00300000` from the stock DTS (`:5312-5315`), these are static candidate physical addresses `0x00330020` and `0x00330008`. The BLSP2 AHB vote is separately described at halt `0x25004`, enable `0x52004` bit 15 (`gcc-msm8996.c:1930-1942`). These definitions do **not** establish that the clocks are enabled, that MMIO is safe in this boot stage, or that UEFI handed QUP ownership to XNU. GPIO87/88 alternate-function encoding and live pad state remain unverified.

The touch hardware is strongly identified from static DTS and saved Android inventory as the FT5x46 family at I2C12 address `0x38`, with FTE8716 variants listed. The actual runtime chip/firmware and active panel variant remain unverified. GPIO105 (`interrupts`) conflicts with GPIO125 (`irq-gpio` and active touch pinctrl); GPIO125 is the better static candidate, not a proven runtime wire. GPIO89 reset, L14 and display-shared LAB/IBB ownership are also unverified. Do not toggle reset or shared rails on this evidence. The upstream HakiOS FocalTech implementation is FT3680 over SPI and has a different report and power/reset assumptions; it is not a MIX1 driver.

## Isolated queue candidate

An additional patch candidate is `ports/hakios-lithium/patches/0020-lithium-touch-batch-queue.patch`; its standalone host model is `maintenance/hakios-lithium/peripherals-next/touch-batch-queue-test.c`. The proposed kernel API appends up to 20 touch-only events under one queue lock, stamps them with a shared frame time, rejects insufficient capacity without partial enqueue, and wakes readers once. It preserves the existing single-event API. `git apply --check` passed in the HakiOS source root.

The model compiled with Clang C11 `-pedantic -Wall -Wextra -Werror` and ASan/UBSan, then passed six host-model suites for invalid inputs, shared timestamps/order, capacity rejection, counter wrap, corrupt occupancy and finger transitions. This test is a **separate model**, not execution of the patched kernel function. Consequently the patch has not been merged into the active HakiOS patch sequence, has not been included in a production kernel build, and is not integration-accepted. `touch_feel` and the five-finger return behavior need additional direct review/testing before merge.

Host command: compile `maintenance/hakios-lithium/peripherals-next/touch-batch-queue-test.c` with `clang -std=c11 -pedantic -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined`, then run with `ASAN_OPTIONS=halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Exit code was 0 and output was `PASS: 6 host-model suites (...)`. Patch-context check: from `upstream/hakios-xnu-kernel`, `git apply --check ../../ports/hakios-lithium/patches/0020-lithium-touch-batch-queue.patch`; exit code 0.

## Parallel execution record

Independent agents reviewed board resources/IRQ conflict, the existing HID route, build/freeze gates, upstream FocalTech differences, package immutability, QUP clock mapping and authorization/status records; another prepared the queue API candidate. Main-agent review checked the cited DTS/source entries and reran the host-model compile/test plus patch-context check. No agent received device-operation permission. Exact per-agent wall times were not available, so none are inferred.

## Remaining work and hardware boundary

1. Complete static validation of the QUP6 clock IDs and GPIO alternate function against pinned MSM8996 sources; do not infer live register state from source definitions.
2. Review whether an initial controller setup can avoid QUP SW_RESET. The existing combined-transfer prototype writes that register and cannot be used as a harmless identity probe. Platform ownership, enabled clocks, pad voltage/pull-ups and access behavior are not evidenced.
3. Integrate the batch API candidate only after direct semantic and kernel-build validation; then implement a default-off QUP6/FT5x46 backend that does not reset the controller or switch shared rails during the first probe.
4. Only a separately frozen package containing the real backend can test identity and raw reports. IRQ selection, split-frame consistency and end-to-end backboardd/SpringBoard touch remain later checks.

No physical touch data, IRQ delivery, kernel integration, new package, device status or UI response is claimed by this report.
