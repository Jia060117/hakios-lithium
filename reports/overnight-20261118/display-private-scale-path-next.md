# Private scale path: bounded static follow-up

Date: 2026-11-18, Asia/Shanghai. Scope: host-only, read-only investigation of the already supplied 20F66 split dyld cache. Stopped at the user's wrap instruction; no broad extraction or further scanning is needed for this handoff.

## Finding and disposition

**The previous metadata-only review is now extended by actual, bounded Mach-O symbol-table and instruction inspection. A complete main-screen-scale -> source -> backboardd/UIScreen initialization chain is still unverified. No concrete contradiction to R19's early-DT correction was established. Its runtime sufficiency is also unverified.**

Two partial paths are verified below: an internal `main-screen-scale` numeric query, and a separate code path supplying `IODeviceTree:/chosen` plus `display-scale` to an internal reader. Their missing dispatch connection must not be filled in by inference. No claim is made that the second path supplies the first answer. No claim is made from an absent private function or Objective-C method symbol.

R16's userspace registry reader already reporting 2 before a later MG report of 3 remains evidence against treating a simple late registry correction as an established explanation. It does not identify MG's original source, first initialization, helper behavior, persisted cache, or profile precedence.

## Inputs, identity, and address convention

`ACTIVE` = `/home/jia/ios-mix1-build/hakios-lithium-ios16` in WSL.

`CACHE` = `ACTIVE/sysimage/20F66__iPhone10,3_6`. These files already existed. The supplied extracted payload and its existing primary image/mapping tables were reused; no IPSW/DMG or shared-cache extraction was performed.

- Primary cache: `CACHE/dyld_shared_cache_arm64`, 393,216 bytes. UUID read directly from header offset `0x58`: **e5420db3-30d0-33aa-8b7e-05b5038bb46d**.
- Existing image index: header `0x1c0/0x1c4`, image-table offset `0x270`, 2710 entries. Small mapping headers across the existing split files supplied 51 mappings; this was header reading, not scanning their payloads.
- `/usr/lib/libMobileGestalt.dylib`: base **0x19c526000**, LC_UUID **a5087d4a-01bc-3dc1-a83a-dce12b9d8c97**. UUID came from the actual mapped Mach-O load commands, not the primary cache UUID.
- `/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore`: base **0x188d7a000**, LC_UUID **df57bfbd-c5e1-38e0-9009-ca7394d39a34**. Its actual nlist entries were read, but no UIScreen scale implementation was identified.

All addresses below are **unslid cache virtual addresses**. No historical device slide was applied. Image bases, string objects, and function addresses are distinguished. This report contains text analysis, symbol names, UUIDs, addresses and source references only; no extracted Apple byte sequences or disassembly listing is saved here.

## Verified partial call paths

### 1. Actual exported answer entry and shared numeric reader

The supplied MG image's `LC_SYMTAB` identifies `_MGCopyAnswer` at **0x19c527bd4** and `_MGGetFloat32Answer` at **0x19c5282c0**. These are actual defined nlist symbols (type 15, section 1), not names inferred from nearby addresses.

- At **0x19c527bd4–0x19c527bd8**, `_MGCopyAnswer` supplies a zero second argument and tail-branches to the internal, unnamed entry **0x19c527680**.
- At **0x19c5282d8–0x19c5282e8**, `_MGGetFloat32Answer` supplies an output pointer and numeric parameters `7/7/5`, then calls the internal, unnamed numeric reader **0x19c527cec**. Its following instructions select the returned float or the caller-provided fallback.
- At **0x19c527d28–0x19c527d30**, that numeric reader supplies a type/status output pointer and calls **0x19c527680**, the same internal entry reached by `_MGCopyAnswer`. The subsequent bounded range contains result-type tests and output conversion. External conversion/release call destinations were not resolved to named exports, so no particular CF API name is asserted for those instructions.

Thus the verified common portion is:

`_MGGetFloat32Answer (0x19c5282c0) -> unnamed numeric reader (0x19c527cec) -> unnamed answer entry (0x19c527680) <- _MGCopyAnswer (0x19c527bd4)`

This establishes shared machinery, not the source selected for a particular question.

### 2. A real main-screen-scale query site

Inside the supplied MG image:

- The `main-screen-scale` C string is at **0x19c587c03** in `__cstring`.
- Its CFString record is at **0x1d7369d00**; its string-pointer field is at **0x1d7369d10** and the length is 17. The stored cache pointer includes chain bits; the string address is not the raw on-disk pointer value.
- The internal function beginning **0x19c539af0** materializes **0x1d7369d00** at **0x19c539b00–0x19c539b04**, supplies an output pointer and `7/7/5`, and calls **0x19c527cec** at **0x19c539b18**.
- **0x19c539b1c–0x19c539b40** selects zero on failure, compares the resulting float with 1, and returns the greater-than result.

This is a verified **consumer of the scale answer**, apparently a greater-than-one test. The caller's semantic name is not available and is not invented. It is not identified as the scale producer, backboardd startup, or UIScreen initialization. It uses the same numeric reader as the exported float accessor rather than calling that exported wrapper.

### 3. A separate display-scale / chosen-tree read path

The supplied MG image also contains:

- `display-scale` at **0x19c580dec**, CFString record **0x1d7366c00**.
- `IODeviceTree:/chosen` at **0x19c580dd7**.
- An unnamed function beginning **0x19c531c94**. At **0x19c531cc8–0x19c531ce8**, it loads that chosen path and the display-scale CFString, supplies arguments `0/0/1`, and calls the unnamed reader **0x19c54db84**.
- At **0x19c54db9c–0x19c54dbbc**, that reader preserves arguments and calls the unnamed object-reader entry **0x19c528654**.
- **0x19c528664–0x19c52867c** forwards the path to an external branch destination. Its ordinary-property branch at **0x19c5286d8–0x19c5286e8** forwards the saved property argument to another external destination. Those external targets were not resolved through the cache branch islands/GOT to named IOKit APIs. Therefore the exact API identity and original-tree versus live-registry behavior are **unverified**.

The surrounding function includes other property work and substantial profile-dependent branching. The bounded inspection does **not** establish that this function produces `main-screen-scale`, or that its observed chosen-property read dominates that question's answer. A property string alone would not establish that edge; the actual argument and call instructions improve the evidence but still do not close it.

## Exact file provenance for reproducing the bounded reads

The primary cache image table and the image's `LC_SEGMENT_64` / `LC_SYMTAB` were used to translate addresses; no new extracted Mach-O was written. Selected verified address-to-file translations:

| Unslid address / purpose | Existing cache file relative to CACHE | File offset |
|---|---|---|
| 0x19c527680, shared answer entry | dyld_shared_cache_arm64.09 | 0xdf3680 |
| 0x19c527bd4, MGCopyAnswer | dyld_shared_cache_arm64.09 | 0xdf3bd4 |
| 0x19c5282c0, MGGetFloat32Answer | dyld_shared_cache_arm64.09 | 0xdf42c0 |
| 0x19c531cd0, display-scale reference | dyld_shared_cache_arm64.09 | 0xdfdcd0 |
| 0x19c539b00, main-screen-scale reference | dyld_shared_cache_arm64.09 | 0xe05b00 |
| 0x19c54db84, internal property reader | dyld_shared_cache_arm64.09 | 0xe19b84 |
| 0x1d7369d00, main-screen-scale CFString | dyld_shared_cache_arm64.27.dylddata | 0x1a55d00 |
| 0x216168000, shared LINKEDIT base | dyld_shared_cache_arm64.42.dyldlinkedit | 0x5c000 |

MG's `LC_SYMTAB` has symbol offset **210728304**, count **1077**, common string-table offset **284419056**, size **48874923**. UIKitCore's symbol offset is **191960144**, count **120899**, with the same common string-table coordinates. Offsets are load-command file coordinates within shared LINKEDIT; do not treat them as offsets in the tiny primary metadata file. Symbol records and their individual names were read by address, not by scanning the entire 48 MB string table.

Available tools actually used: Python standard-library read/struct operations and installed `/usr/bin/llvm-mc --disassemble --triple=aarch64` on small instruction buffers through stdin. `/usr/bin/llvm-objdump`, `llvm-readobj`, and `llvm-nm` were found. Python `capstone`, `lief`, and `macholib` were not installed; none was installed for this task. Inspection helpers ran from stdin only.

## Concrete gaps and smallest next evidence

1. **Missing local-symbol companion:** the primary cache's local-symbol offset/size at header `0x48` are both zero. The primary header's companion symbol-file UUID field at **0x190** is **db615077-8daa-3b6e-836d-d965e18ada97**. No `*.symbols` file exists in CACHE. The ordinary per-image nlists above do exist; absence of the companion is not absence of all symbols and is not proof that analysis is impossible. If already supplied elsewhere, the exact matching companion or a trusted index derived from it would reduce anonymous-function ambiguity. Do not substitute symbols from another OS/cache UUID.
2. **Missing query-dispatch edge:** no verified edge connects `main-screen-scale` at **0x1d7369d00**, through the unnamed answer entry **0x19c527680**, to a handler that reads chosen `display-scale`, or to another concrete source. The next static evidence is a bounded decode of that question's lookup/dispatch record and selected handler, with exact record and target addresses. An additional data reference to the scale CFString exists at **0x1d7363958**; its role was not established and it is not labeled a dispatch entry.
3. **Missing external-reader resolution:** follow the actual branch-island/GOT targets reached from **0x19c52867c** and **0x19c5286e8** to exact exported functions before naming registry APIs or claiming original-DT access. Merely assigning the nearest image base to a branch island is unsafe and was not used as provenance. No required cache segment was found missing for the bounded reads performed here.
4. **Missing consumer edge:** UIKitCore's inspected nlist contains undefined MG imports, including `_MGCopyAnswer` and `_MGGetFloat32Answer`, but that proves only imports. It does not identify `-[UIScreen scale]`, its initializer, or a backboardd scale call site. No UIScreen method implementation address or backboardd UUID/call-chain finding is asserted. Next evidence is an existing Objective-C method/xref index or targeted method metadata plus the concrete initialization caller.

No substantial new extractor, index, tool installation, companion search across large trees, or multi-GB payload scan was undertaken. These gaps are handed off rather than extending this bounded investigation.

## R16 observation versus R19 hypothesis

Existing saved evidence only, `F:/IOS-MIX1/reports/local/20261118-042809-642502-ios16-r16/console.log`:

- **91–92:** kernel registry diagnostics correct scale/artwork scale/subtype from `3/3/2436` to `2/2/1334`.
- **1315:** constructor-side userspace registry reader reports `scale=2 subtype=1334 valid=1/1 native_profile=1`; this line was read directly again in this investigation.
- **1716/1718:** backboardd later reports MG/preferred scale 3 and 750x1334 at scale 3.
- **4925/5472/7311:** saved UIScreen samples are logical 250x444.667 at 3, native 750x1334 at 3.

The other saved-log references are retained from `reports/overnight-20261118/display-scale-source-review.md`, not newly audited as a complete log in this task. The registry reader is not MG and log order does not reveal every process's first MG query. Therefore **simple late timing remains unsupported**.

R19 retains the R18 early-loader display implementation. Local source references: `ports/hakios-lithium/patches/0018-lithium-early-display-profile.patch:21–40` places profile configuration before DT construction and changes emitted chosen display-scale; `ports/hakios-lithium/diagnostics/display-loader.inc:47–65,68–80` selects the implicit profile and changes the emitted product artwork values. This differs from the later live property replacement described at `ACTIVE/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:618–645` in the previous review. No source was modified or frozen-image audit repeated here.

The newly inspected MG code is compatible with some relevance of a chosen display-scale value, but does not prove that it supplies this particular MG answer or explain why R16 retained 3. There is **no concrete contradiction to the early-DT fix in these findings**. There is also no proof that it resolves R19. A later, separately authorized run showing initial loader/kernel 2 yet actual MG 3 would establish insufficiency in that run; none was performed here. Original-tree consumption, cache precedence and helper influence remain unverified. No hooks, cache clearing, or parameter changes are proposed.

## Write and operation boundary

Only `reports/overnight-20261118/display-private-scale-path-next.md` was written by this investigation. No source edits, builds, frozen-report edits, proprietary byte outputs in the repository, device operations or queries, ADB/Fastboot commands, partition operations, QEMU, or network access occurred. Other agents' existing changes were left alone. No frozen image or 149-hash audit was repeated.
