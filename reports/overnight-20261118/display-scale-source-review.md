# R18 display dependency review: MG and UIScreen scale

Date: 2026-11-18 (Asia/Shanghai). Bounded host-only review, starting with `reports/hakios-ios16-display-r18-audit.md`. The project instructions, PLAN, OPERATIONS and selected project metadata were consulted. Current user prohibitions override historical device authorizations.

## Conclusion

**The exact reason MobileGestalt retained scale 3 in R16 is still an unverified private-framework decision. No actual private-framework source/call-chain proof was established.** No competing MG scale override or explicit scale-cache seed was identified in the inspected HakiOS sources. That scoped negative finding neither contradicts the R18 early-loader hypothesis nor proves R18 will fix the runtime result.

The strongest additional timing evidence is `console.log:1315`: a SpringBoard constructor-side registry reader already saw scale 2 / subtype 1334 before backboardd subsequently reported MG scale 3. Therefore “the consumers simply started before the registry correction” is not established. Original-tree access, an earlier or persisted answer cache, helper behavior, or another private profile source remain possibilities, not findings.

The main agent reports the frozen audit complete in `reports/overnight-20261118/display-goal-audit.json`: embedded FAT EFI and unchanged display sources pass; R16 retains three 250-point-wide samples at scale 3. Those are main-agent audit results, not independently repeated here.

## Reference notation

- `UP/` = `F:/IOS-MIX1/upstream/hakios-xnu-kernel/`, the supplied upstream reference tree.
- `ACTIVE/` = `/home/jia/ios-mix1-build/hakios-lithium-ios16/` in Ubuntu, the existing integrated workspace, read only. Its lines differ from UP and are labeled separately below.
- `R16LOG` = `reports/local/20261118-042809-642502-ios16-r16/console.log`, existing saved evidence, not a new phone observation.

## Saved evidence: registry correction and consumer disagreement

| Exact reference | Observation | Limit |
|---|---|---|
| `R16LOG:91-92` | Registry diagnostics change chosen scale/artwork scale/subtype from `3/3/2436` to `2/2/1334`; all three valid. | Does not establish every boot-tree view or MG's internal source. |
| `R16LOG:1315` | `LITHIUM DISPLAY STATUSBAR scale=2 subtype=1334 valid=1/1 native_profile=1`. | A userspace registry reader already sees the correction; this is not an MG query. |
| `R16LOG:1449-1451` | launchd spawns MobileGestaltHelper for IPC. | No query, reply or cache provenance is exposed. |
| `R16LOG:1716,1718` | backboardd logs MG scale 3, preferredScale 3 and main size 750x1334 at scale 3. | Equal values do not prove their internal dependency direction. |
| `R16LOG:3089` | UIKit reports no status-bar provider for a 250x444.666... screen at scale 3. | Confirms the bad UIKit profile, not its private initialization mechanism. |
| `R16LOG:4925,5472,7311` | Three UIScreen samples: logical 250x444.667@3; native 750x1334@3. | Stability across these samples does not identify a particular cache. |
| `R16LOG:4933-4938` | Real MG width=750, height=1334, scale=3 (Float32 CFNumber). ScreenDimensions, main-screen-class and ArtworkDeviceSubType unavailable. | The unavailable keys establish no dimensions/class/subtype. |

750/3=250 agrees with the observed logical width; arithmetic is not proof that UIScreen initializes directly from this MG accessor. Existing log order does not identify the first MG answer initialization in every process.

## What the inspected HakiOS code actually does

1. **MG diagnostics call the library; they do not synthesize scale.** `UP/userland/src/mg_test.c:11-18,29-46` calls MGCopyAnswer, describes/releases its returned CF value and prints it. `UP/userland/build.sh:202` links it with `-lMobileGestalt`; the active counterpart is `ACTIVE/userland/build.sh:205`.

2. **The backboardd injector has other display hooks, with no identified MG scale hook.** `UP/userland/src/inject.c:505-528` replaces QuartzCore readiness, update/render and GL virtual methods. Its constructor at `:1164-1233` installs rendering/input/container support. `:1191-1216` can set generic `caenv=NAME=VALUE` switches, but the retained R18 command line in the starting audit has no caenv argument. Targeted checks of the complete three requested source files and active counterparts found no main-screen-answer replacement. This is scoped evidence, not a claim about every private binary.

3. **The status-bar fallback does not set UIScreen/MG scale.** `UP/userland/src/inject_sb.c:1335-1341,1362-1414` replaces fallback-provider class methods/allocation. The active guard at `ACTIVE/userland/src/inject_sb.c:1382-1386` returns when its registry profile reader accepts scale 2/subtype 1334. That reader, `ports/hakios-lithium/diagnostics/display-userspace.inc:3-27`, reads four-byte CFData using IORegistryEntryCreateCFProperty at IODeviceTree:/chosen and /product. R16LOG:1315 records its accepted result. This code cannot be identified as the cause of MG=3.

4. **The wallpaper literal 3.0 is local, not a global screen profile.** `UP/userland/src/inject_sb.c:970-993` reads UIScreen.scale, uses 3.0 only when the screen object is absent, and sets wallpaper/poster layer rasterizationScale. It does not set UIScreen scale or MG answers. The persisted image header at `:1038-1044` stores magic/width/height/row for hakios-wallpaper.raw, not a MobileGestalt answer cache.

5. **R16/R18 display samples read actual accessors.** `ports/hakios-lithium/diagnostics/display-userspace.inc:37-60,66-79` resolves MGCopyAnswer from /usr/lib/libMobileGestalt.dylib and calls UIScreen selectors. R18's `display-userspace-r18.inc:37-60,66-79,93-97` preserves these calls. Expected 375x667@2 values are comparisons, not substitute answers supplied to UIKit.

6. **An optional external tweak path limits absolute negative claims.** `ACTIVE/userland/src/inject_sb.c:2078-2096,2175` conditionally opens /redwood/lib/libhakios.tweaks.dylib if /var/jb/usr/lib/TweakInject exists. Targeted text checks of UP and ACTIVE inject_tweaks.c found no MG/scale-specific implementation. Third-party phone contents were not inspected; their absence is not assumed.

7. **No explicit MG scale seed was found in the selected assembly scripts.** Targeted text checks of UP and ACTIVE userland/build.sh, userland/mkjobs.py, sysimage/mkvarskel.py and sysimage/unpack.sh found no main-screen-scale/cache seed. The helper job listing at `UP/userland/build.sh:111` (active :114) does not establish its answers. Exact probes for mobile/Library/Caches/com.apple.MobileGestalt.plist, root/Library/Caches/com.apple.MobileGestalt.plist and db/MobileGestalt/Cache.plist under each of ACTIVE/sysimage/var.skel, ACTIVE/userland/var and ACTIVE/userland/root/private/var found no regular files at those nine candidate paths. These were hypotheses for host-path probes, not established iOS 16 cache locations. They do not exclude other paths, mounted images or persisted phone Data.

## Existing iOS 16 cache metadata: identity, not answer provenance

Only the existing 393,216-byte primary metadata file was read:

`ACTIVE/sysimage/20F66__iPhone10,3_6/dyld_shared_cache_arm64`

Its UUID bytes at offset 0x58 are `e5420db330d033aa8b7e05b5038bb46d`, matching the previously recorded supplied-cache identity in `reports/hakios-ios16-device-r6.json:193-203`. Header offsets 0x1c0/0x1c4 give image-table offset 0x270 and count 2710. Exact-name lookups yielded:

| Image | Unslid image base | Metadata entry offset/index |
|---|---|---|
| /usr/lib/libMobileGestalt.dylib | 0x19c526000 | 0x12b0 / 130 |
| /System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore | 0x188d7a000 | 0x450 / 15 |
| /System/Library/Frameworks/UIKit.framework/UIKit | 0x1f74b2000 | 0xe0d0 / 1779 |
| /System/Library/Frameworks/QuartzCore.framework/QuartzCore | 0x18827b000 | 0x3f0 / 12 |
| /System/Library/Frameworks/CoreGraphics.framework/CoreGraphics | 0x188781000 | 0x430 / 14 |

These are **image bases, not MG/UIScreen function addresses**. No new extraction, cache-payload-wide string scan, symbol index, linkedit scan or private-framework disassembly was performed. The historical R6 slide in `reports/hakios-ios16-device-r6.json:196-199` was not applied to R16 or R18. Metadata identifies supplied images; it does not prove a hardcoded scale, persisted answer cache, particular property/syscall read or UIScreen-to-MG call chain. Private implementation provenance remains **not established**.

## Does this contradict the early-loader correction?

There is a proven difference in where the correction occurs. `ACTIVE/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:618-645` replaces live scale/artwork registry properties using IORegistryEntry::setProperty during framebuffer startup. R18's `ports/hakios-lithium/patches/0018-lithium-early-display-profile.patch:21-40` configures the profile before tree construction and changes the emitted chosen scale. `ports/hakios-lithium/diagnostics/display-loader.inc:47-65,68-80` selects the implicit profile and replaces the existing product artwork scale/subtype while emitting the tree.

Thus R18 corrects the initial serialized identity as well as the later registry view. An original-tree dependency is a plausible reason this could matter. But the actual MG access path, its timing and any persisted/helper/model-profile precedence remain unproven. The comment at `UP/xnuloader/loader.c:3242-3243` saying display-scale feeds MG is not private-framework call-chain evidence.

**Disposition:** no identified inspected hook/cache seed contradicts the early-loader hypothesis; its causal explanation and R18 runtime sufficiency are unverified. No runtime-code change or cache invalidation/deletion is recommended from this review.

## Exact next evidence, when separately authorized

This is an evidence handoff, not a request or authorization to operate the phone. No new diagnostic implementation was added.

1. Existing LOADER before/after markers, if captured: after should show chosen_scale=2 artwork_scale=2 subtype=1334 auto_profile=1 render=750x1334. Loader lines may be absent from a kernel-only USB stream.
2. Existing kernel DISPLAY DT before **and** after markers should each show 2/2/1334, valid=1/1/1. Retain geometry and the separate userspace STATUSBAR native_profile=1 reading with process/time context.
3. Actual backboardd StartWindowServer MG scale, preferredScale and initial main screen size: compare with scale 2 and 750x1334; do not substitute registry expectations.
4. Actual MG answer availability/type/completeness and all three UIScreen samples: expected MG width750/height1334/scale2; expected UIScreen logical375x667@2, native750x1334@2. Keep unsupported keys unavailable. Retain bounded WINDOW records and separate photographic Dock/labels/bounds acceptance.
5. If loader and kernel start at 2 yet MG remains 3, the initial-DT correction is insufficient in that run. Missing discriminating evidence: earliest main-screen-scale query PID/time; actual MG entry/caller address normalized with that run's cache UUID/slide; returned CF type/value; and associated source read (registry path/key, original-tree access, helper IPC or actual cache-file path/read). Those provenance records are not in the inspected evidence. Do not attribute the result to disk cache or clear Data from the unchanged number alone.
6. If MG becomes 2 while preferredScale/UIScreen remains 3, retain first display-mode/UIScreen initialization and process/time context to investigate the consumer boundary. Existing equal values do not establish the private dependency direction. Record actual relevant tweak-loader/environment evidence if present; no such configuration is assumed.

## Operational handoff and boundary

Per the main agent's latest instruction, the normal boot entry's current provenance is stale after four admission-file changes. This review does not certify that entry, refresh its provenance or authorize a boot-guard bypass. Main may review/checkpoint this report; runtime code and the phone remain outside this task.

Only this report was written by this investigation. No source/runtime edits, builds, firmware extraction/download, phone queries, boot/reboot, ADB/Fastboot commands, partition operations or QEMU occurred. The main frozen-package audit was not repeated.
