# Independent touch MMIO recovery safety review — 2026-11-19

## Decision

**Keep the read-only accessor as a disconnected host-test candidate. Withhold production exception integration and device-ready admission.** A dedicated, exact-instruction fixup can contain a narrowly identified synchronous external abort; it cannot turn an unknown bus transaction into a safe probe. The current policy still needs a stricter, versioned syndrome contract. No generic copyio change is warranted. The subsequent CLOCK/PIN design review does not authorize installing this hook.

R20-display remains the sole accepted baseline: 1080×2040 BGR24, stride 3240; 360×680 points, scale 3. Preserve bootcpu=1, cpus=1 and physical affinity 0x1. R19 stays archived. No device query, boot, partition operation, emulator, legacy-kernel command, compilation, or hardware verification was performed by this reviewer.

## Actual inspected status

The inspected candidate is **read-only**, not the earlier read32/write32 proposal. It allowlists 13 aligned physical words in eight distinct 4 KiB windows. The accessor has one exact MMIO LDR, no MMIO STR, and an exported failure return. The ordinary stack-output STR is outside the recovery label. Source: ports/hakios-lithium/peripherals/touch-mmio-policy.h:14–33 and touch-mmio-access.inc:18–38,125–167.

Latest access source inspected after the main author's update: SHA256 d8a1733d957700df0cf0145659db0615d9d7e19c3d4af7105719a413afc8969f. It declares the strong external lithium_touch_mmio_abort_hook_abi at :8–11, requires value 1 at :83, and rejects a NULL current_thread at :88–89. These address accidental live use without a hook provider and NULL ownership. A reachable claim should retain a link dependency; unused/dead-stripped candidate code need not fail linking. The symbol alone cannot establish that a genuine hook is on the executed exception path. No mock, link-negative, object, or final-link test was independently run here. The author reports the real helper assembled/disassembled to eight ARMv8.0 instructions; that result is **author-reported, not independently verified**.

The author states no production caller includes the candidate and no hook is installed. Focused inspection corroborates that the active sleh.c is byte-identical to its pinned HEAD and neither inspected common hakios.patch contains sleh.c. This is not a whole-repository include/link audit. kernel.inc:41–46 retains the Stage 0 diagnostic and blocks higher stages for unverified handoff. Existing production integration and new package readiness are not established.

## Existing APIs and exception conventions

All XNU paths below are relative to /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2/.

| Location | Finding |
| --- | --- |
| osfmk/arm64/loose_ends.c:738–754 | ml_probe_read and ml_probe_read_64 panic as unimplemented. They are not a usable probe route. |
| loose_ends.c:273–383,453–554 | Physical read/write helpers perform volatile accesses; optional latency handling runs after an access returns. It cannot interrupt a stalled transaction. |
| osfmk/arm64/sleh.c:270–335,1488–1504,1540–1631 | Existing copyio fixups match PC ranges, but normal VM/alignment handling does not admit direct DFSC 0x10 external aborts. Adding a copyio range alone does not provide SEA recovery. Do not broaden generic copyio fault classes. |
| osfmk/arm64/machine_routines_asm.s:39–78,440–456,1314–1315 | Existing copyio vector/error/table conventions are purpose-specific and should remain unchanged. |
| osfmk/arm64/machine_routines.c:1643–1677 | ml_io_map selects VM_WIMG_IO; the explicit-protection variant exists. Write-combining is a different memory type. |
| osfmk/arm/io_map.c:88–132 | Mapping rounds to kernel pages and may allocate with KMA_NOFAIL; the default mapping is permanent. Mapping failure/panic is outside the exact LDR fixup. |
| osfmk/arm/pmap/pmap.c:5647–5707; osfmk/arm64/proc_reg.h:1090–1118 | Non-DRAM VM_WIMG_IO uses the Device nGnRnE attribute index; the inspected code has a DRAM Device-GRE special case. Do not equate every IO mapping or WCOMB mapping with nGnRnE. |
| machine_routines.c:1940–2015; osfmk/arm64/copyio.c:195–207 | Translation validation/nofault copying and kernel bcopy wrappers do not validate bus response, peripheral power, or secure ownership. |

No supported safe-MMIO-probe API was found in this inspected ARM64 route. This is a scoped source conclusion, not an exhaustive claim about every XNU platform API.

sleh_synchronous checks instruction length and dispatches DABORT_EL1 at sleh.c:646–657,741–743. It may restore interrupt enables from the saved context at :689–692. A future Lithium-only hook would belong **after the interrupt-stack rejection at :1536–1538 and before ordinary fault classification at :1540**. Earlier platform-parity routing must remain intact (:674–682). SError uses the separate sleh_serror/platform-error route (:1982–2003); platform handlers can delegate (:442–459). Do not intercept that route or assume all platform errors take the same action.

## Blocking policy finding and exact proposed admission

**touch-mmio-policy.h:65–66 uses mask 0xfe0005ff.** It ignores the upper ISS fields and therefore admits syndromes outside the deliberately narrow ARMv8.0 policy. Numeric reproduction against the actual source constants produced:

| ESR | Current matcher | Conservative read-only recommendation |
| --- | --- | --- |
| 0x96000010 | accept | accept |
| 0x96000210 (EA=1) | accept | accept |
| 0x97000010 (ISV=1) | accept | reject |
| 0x96001810 (SET=3) | accept | reject |
| 0x96002010 (bit13=1) | accept | reject |
| WnR/FnV/S1PTW/CM=1; translation, table-walk, parity, alignment, SError | reject | reject |

For a versioned ARMv8.0 **read-only** contract, recommend:

~~~c
(esr & UINT32_C(0xfffffdff)) == UINT32_C(0x96000010)
~~~

Only EA bit9 varies. EC must be DABORT_EL1 (0x25), IL=1, DFSC=0x10; WnR, S1PTW, CM, FnV and all other ISS bits must be zero. Reject unreviewed extensions, including nonzero SET/ISV. This is a conservative policy recommendation, not a claim that every rejected encoding is architecturally impossible. Do not require ISV=1/SAS metadata for this ordinary EL1 load: the exact known LDR establishes its 32-bit access width. A later write helper needs a separate review; it is not admitted by this candidate.

A future actual hook must additionally enforce all of the following, using real saved state rather than caller assertions:

1. AArch64 saved64 EL1 thread-mode state, ordinary supported interrupt context, non-NULL actual current_thread equal to the immutable claim owner; sole online logical CPU0 and the correctly normalized physical affinity 1. Do not compare full MPIDR (historically 0x80000001) to 1.
2. Acquire-observed armed state; single claim, no nested probe, no prior poison; guard publication ordered before the faulting instruction. Enforce sole input-thread entry or serialize claim. Single-core execution alone does not make the plain check/set at access.inc:83–94 atomic against preemption by another caller.
3. Exact saved PC equal to the immutable exported LDR instruction label; no broad helper range. Valid FAR must equal both the saved x0 operand and this call's exact authorized mapped VA. Preserve full equality and fail closed on uncertain/tagged/UNKNOWN address bits.
4. The VA must derive from the provider's live, immutable Device mapping of exactly an allowed physical word; correct offset, alignment, nonoverflow and no boundary crossing. An allowlisted PA and arbitrary stored VA are not sufficient. Validate the actual mapping route/attributes and lifetime outside exception context.
5. Only the actual exported failure label is an admissible target. Redirect with set_saved_state_pc (osfmk/mach/arm/thread_status.h:696–710), retaining the architecture's signed-state handling. Do not modify CPSR, x0, x1, LR, or arbitrary registers; the leaf failure code returns false through its original LR.
6. Record diagnostics in resident ordinary memory, revoke armed and poison the entire provider; no retry, output publication, log, allocation, lock acquisition, MMIO, or reset in recovery. All mismatches fall through unchanged to normal exception handling. Any context/affinity accessor used by the callback must itself be exception-safe.

The current callback at access.inc:104–121 checks saved operand==FAR, uses acquire/release armed operations, records fault_esr and fault_physical and permanently poisons. It does **not** explicitly store fault_far: guard.address remains available after poisoning, but that is not explicit ESR/FAR capture. Add a dedicated immutable FAR/PC diagnostic snapshot if that is the promised contract. Actual hook extraction of saved64/mode/PC/x0 and use of the saved-state setter remain unimplemented/unverified.

## Hard limits and board gates

A software deadline and read budget bound **completed iterations**, not the duration of an individual access. access.inc:65–76,130–166 checks time before/after completed mapping/reads. A transaction that never completes may never supply an ESR, and even barriers can wait. Neither a timer nor the leaf fixup proves a hard wall-clock bound.

A synchronous fault precisely attributed to this LDR can return false through a future validated hook. An asynchronous SError, EL3/XPU response, platform-fatal event, firmware hang, reset, or persistent interconnect fault is outside this mechanism. Returning from a synchronous abort also does not undo a device read side effect or repair a poisoned fabric. Device attributes, barriers and the READ allowlist establish none of the board's ownership/power guarantees.

The 13 physical words are:

~~~text
00325004 00352004 00330000 00330008 00330020 00330024
01067000 01068000 01079004 0108d004
075ba004 075ba030 075ba404
~~~

Their register-name comments cite matched board sources, but those board files were not audited during this MMIO review. QUP HW_VERSION is not the touchscreen controller's runtime ID. No speculative Stage 1 ID permission is granted.

Keep **all eight lqt_io admission gates unresolved** unless their separate evidence exists. Snapshots cannot set LQT_ALL_VERIFIED. Secure-world ownership/handoff, board power targets and actual rails, pull-ups, actual clock parents/rates/gates, pin roles/mux/electrical state, IRQ105/125 routing, runtime controller ID/reports, transfers and HID delivery remain unverified here. No supply/reset pulse, clock/TLMM write, QUP reset, transfer or IRQ registration is authorized by this report.

## Smallest provenance-preserving future route (not executed; integration withheld)

The common upstream xnu/hakios.patch and current WSL workspace copy both have zero sleh.c matches. A dependency adapter alone must not become the sole unrecorded origin of exception source.

1. First tighten and freeze the disconnected accessor and exact adapter tests. Test the real matcher and wrapper, negative ESR/mode/owner/PC/FAR/operand cases, claim serialization/preconditions, permanent poison, no output on failure, missing ABI provider at live link, and exact Apple ARM64 assembly/symbol/disassembly. No mock may masquerade as the production hook symbol/provider.
2. If a later independent decision authorizes integration, a new ordered port patch (proposed 0022) should extend common xnu/hakios.patch with the minimal LITHIUM_ARMV8_0-gated sleh.c declaration/call/ABI-provider hunk. Match the port callback/link gates. Non-Lithium builds must remain unaffected; keep default live admission disabled. Existing precedent: ports/hakios-lithium/patches/0005-lithium-console-capture.patch:1–2,29–61.
3. For an already prepared compilation tree, extend the explicit reconstruction allowlist or add a small equivalent dedicated sync for osfmk/arm64/sleh.c. Reconstruct from pinned git HEAD plus the **complete current common patch**, verify the previous full source fingerprint before mutation, preserve the original, copy only the allowlisted reconstructed target, and record/verify its hash. Precedent: scripts/hakios-dependencies.py:38–78; existing-tree validation :302–307; checkpoints :313–323; complete reverse patch check :18–36,337. Do not hand-edit the WSL compilation tree or reset fingerprints to bless arbitrary drift.
4. Normal ordered flow is prepare/check, dependency synchronization, refreshed current source-review/host evidence, full kernel build and normal build checks. Supported generic dispatch entries are scripts/run-wsl-limited.ps1:27–41. scripts/hakios.py:246–264 rejects stale source/dependency/overlay evidence. Confirm actual final exception object and linked callback/ABI/reference; host tests alone are insufficient. Do not revive archived R19 tasks. Do not rerun xnu/apply.sh against an existing tree (it refuses that at :16).

This is a future design sequence, not commands executed or present integration approval. Moreover scripts/hakios.py:160–163 can use git add --intent-to-add internally for new common files. Running prepare would therefore exceed this review's no-stage/read-only scope; it was not run. Resolve that constraint explicitly before any later authorized implementation workflow.

## Source identities and verification limits

| Inspected source | SHA256 |
| --- | --- |
| ports/hakios-lithium/peripherals/touch-mmio-policy.h | 324bd13c3c0548cf0d7b9b1e9705bce4993857df34ba2a21db816f6fd5cefb69 |
| touch-mmio-access.inc, latest re-read | d8a1733d957700df0cf0145659db0615d9d7e19c3d4af7105719a413afc8969f |
| XNU osfmk/arm64/sleh.c | 710c576eeeb6d735c5f5bb3df8ad84efe3a390c87e1c1bda06476cc770167de9 |
| XNU osfmk/arm64/loose_ends.c | 7d5b08a08b42520eb7a3a8a94fc6d70ca93cd94197365f6b267f0af5034b8333 |
| XNU osfmk/arm64/machine_routines_asm.s | f2d3f19c0a7d1c22e4bfec685bb6f8b8a2e39272291571ffe71fca0d96c56a29 |
| upstream/hakios-xnu-kernel/xnu/hakios.patch | 0d3f8ae3b78e78f611448f8563dfe41d16333413d38bb75a155315a508f65b8e |
| WSL workspace xnu/hakios.patch | 8f1167b9bf3926fb1f2825c72f1b9dfb95e1281c0e125594e5525a3d135a744f |

The three core exception files were compared byte-for-byte with pinned HEAD. Candidate access source changed during the concurrent main work; the older inspected hash 4441ca98a26b747033affe3bb168a839f5caf12e75675041c2dae8f1ca9cb0b0 is superseded above. Findings are scoped to these inspected bytes, not subsequent edits.

No implementation tests, actual exception injection, final link, production build, mapping/PTE observation, CPU macro definition audit, or physical validation was performed. Exact new maintenance test files were not independently inspected. No implementation, patch, index or existing report was edited by this reviewer. The worktree was already dirty and contained staged changes; concurrent main edits prevent claiming that every repository file stayed globally unchanged.

Official architecture references consulted through public web search/open (some direct pages were unreadable; usable search extracts supplied only limited confirmation): Arm ESR_EL1 register description, FAR_EL1 description, Exception model guide, and DDI0487 known issue D16002 concerning SET for direct SEA. These support the syndrome/FAR/async distinctions, not any board safety claim:

- https://support.arm.com/documentation/111107/2026-09/AArch64-Registers/ESR-EL1--Exception-Syndrome-Register--EL1-
- https://developer.arm.com/documentation/102670/0300/AArch64-registers/AArch64-register-descriptions/AArch64-Generic-System-control-register-description/FAR-EL1--Fault-Address-Register--EL1-
- https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Exception%20model.pdf
- https://documentation-service.arm.com/static/698610e48503f171e66554b3?token=

## Actual command ledger

The following are the exact shell commands issued for this MMIO review, all with working directory F:\IOS-MIX1. Commands only read/query source or reproduce numeric predicates. Exit 0 does not imply every PowerShell statement succeeded: command 18 had nonterminating range-array errors and was corrected by command 19; command 16's formatted search output was followed by explicit output in command 17. Initial broad root output was truncated and focused rereads followed. Command 6 failed because WSL rg is unavailable, then grep/Python were used. No listed build/dependency script was executed. The only authorized write is creation of this report itself, followed by read-back. Public architecture lookups were separate tool calls, not shell commands and do not have shell exit codes.

<details>
<summary>Exact commands and process exits</summary>

### Command 1 — exit 0

~~~powershell
Get-Content -LiteralPath AGENTS.md; Get-Content -LiteralPath PLAN.md; Get-Content -LiteralPath OPERATIONS.md; Get-Content -LiteralPath project.json
~~~

### Command 2 — exit 0

~~~powershell
$files='AGENTS.md','PLAN.md','OPERATIONS.md','project.json'; foreach($f in $files){$v=Get-Content -LiteralPath $f; [pscustomobject]@{Path=$f;Lines=$v.Count;Bytes=(Get-Item -LiteralPath $f).Length;SHA256=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash}}; git --no-optional-locks status --porcelain=v1; if(Test-Path -LiteralPath reports/hakios-ios16-touch-mmio-recovery-review-20261119.md){Get-Item -LiteralPath reports/hakios-ios16-touch-mmio-recovery-review-20261119.md | Select-Object FullName,Length}
~~~

### Command 3 — exit 0

~~~powershell
$i=0; Get-Content -LiteralPath AGENTS.md | ForEach-Object {$i++; '{0}: {1}' -f $i,$_}
~~~

### Command 4 — exit 0

~~~powershell
$i=0; Get-Content -LiteralPath PLAN.md | ForEach-Object {$i++; '{0}: {1}' -f $i,$_}
~~~

### Command 5 — exit 0

~~~powershell
$a=Get-Content -LiteralPath AGENTS.md; for($i=54;$i -lt $a.Count;$i++){'AGENTS.md:{0}: {1}' -f ($i+1),$a[$i]}; $p=Get-Content -LiteralPath PLAN.md; for($i=0;$i -lt [Math]::Min(12,$p.Count);$i++){'PLAN.md:{0}: {1}' -f ($i+1),$p[$i]}; $o=Get-Content -LiteralPath OPERATIONS.md; for($i=0;$i -lt $o.Count;$i++){if($i -lt 18 -or $o[$i] -match '^#|最高|禁止|不得|允许|授权|当前|R20-display|bootcpu|affinity|MMIO|QUP6'){'OPERATIONS.md:{0}: {1}' -f ($i+1),$o[$i]}}; $j=Get-Content -Raw -LiteralPath project.json | ConvertFrom-Json; $j.PSObject.Properties.Name; $j | Select-Object name,active_kernel,baseline,ios16_display_baseline,touch_test_boundary | ConvertTo-Json -Depth 20
~~~

### Command 6 — exit 1

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && sha256sum osfmk/arm64/sleh.c osfmk/arm64/loose_ends.c osfmk/arm64/machine_routines_asm.s && rg -n -e handle_kernel_abort -e ml_probe_read -e recover -e copyio -e external -e SError -e sleh_synchronous -e sleh_.*error -e ml_io -e ml_phys -e io_map -e io_read -e io_write -e ESR_ osfmk/arm64/sleh.c osfmk/arm64/loose_ends.c osfmk/arm64/machine_routines_asm.s'
~~~

### Command 7 — exit 0

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && grep -nE "handle_kernel_abort|ml_probe_read|recover|copyio|external|SError|sleh_synchronous|sleh_.*error|ml_io|ml_phys|io_map|io_read|io_write|ESR_" osfmk/arm64/sleh.c osfmk/arm64/loose_ends.c osfmk/arm64/machine_routines_asm.s'
~~~

### Command 8 — exit 0

~~~powershell
$py=@'
from pathlib import Path
p=Path('/home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2/osfmk/arm64/sleh.c')
s=p.read_text().splitlines()
for a,b in [(25,145),(260,318),(575,748),(1080,1135),(1480,1665),(1960,2035)]:
 print(f'--- {p}:{a}-{b} ---')
 for i in range(a-1,min(b,len(s))): print(f'{i+1}: {s[i]}')
'@; wsl.exe -e python3 -c $py
~~~

### Command 9 — exit 0

~~~powershell
$py=@'
from pathlib import Path
b=Path('/home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2/osfmk/arm64')
for name,spans in [('loose_ends.c',[(25,105),(230,386),(453,555),(725,760)]),('machine_routines_asm.s',[(25,100),(100,175),(390,465),(670,720),(1290,1320)]),('sleh.c',[(318,450),(850,900)])]:
 p=b/name; s=p.read_text().splitlines()
 for a,z in spans:
  print(f'--- {p}:{a}-{z} ---')
  for i in range(a-1,min(z,len(s))): print(f'{i+1}: {s[i]}')
'@; wsl.exe -e python3 -c $py
~~~

### Command 10 — exit 0

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && find osfmk/arm osfmk/arm64 -maxdepth 3 -type f \( -name io_map.c -o -name pmap.c -o -name pmap.h -o -name proc_reg.h -o -name machine_routines.c -o -name machine_routines.h -o -name thread_status.h -o -name copyio.c \) -print && grep -nE "ISS_DA_|FSC_|ESR_EC_DABORT|ESR_EC_SERROR|ESR_IL|ESR_INSTR|PSR64_MODE_EL1|MAIR|ATTRIDX|DEVICE" osfmk/arm64/proc_reg.h'
~~~

### Command 11 — exit 0

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && sha256sum osfmk/arm/io_map.c osfmk/arm/pmap/pmap.c osfmk/arm64/proc_reg.h osfmk/arm64/machine_routines.c osfmk/arm64/copyio.c && grep -nE "io_map|VM_WIMG|MAP_MEM|pmap_map|vm_map|KMA_|Device" osfmk/arm/io_map.c && grep -nE "VM_WIMG_IO|VM_WIMG_WCOMB|VM_WIMG_POSTED|VM_WIMG_MASK|CACHE_ATTRINDX|ATTRINDX_DISABLE|ATTRINDX_DEVICE|MAIR_DISABLE|io_map" osfmk/arm/pmap/pmap.c osfmk/arm64/machine_routines.c && grep -nE "ml_io_read|ml_io_write|ml_validate_nofault|nofault|copyin_kern|copyout_kern|copyio|probe|recover" osfmk/arm64/machine_routines.c osfmk/arm64/copyio.c'
~~~

### Command 12 — exit 0

~~~powershell
$py=@'
from pathlib import Path
b=Path('/home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2')
for name,spans in [('osfmk/arm/io_map.c',[(50,138)]),('osfmk/arm64/machine_routines.c',[(1640,1680),(1936,2033)]),('osfmk/arm/pmap/pmap.c',[(1755,1833),(5620,5712),(10045,10095)]),('osfmk/arm64/proc_reg.h',[(470,525),(1087,1118),(1700,1793),(1827,1878)]),('osfmk/arm64/copyio.c',[(125,215)]),('osfmk/arm64/sleh.c',[(442,508)])]:
 p=b/name; s=p.read_text().splitlines()
 for a,z in spans:
  print(f'--- {name}:{a}-{z} ---')
  for i in range(a-1,min(z,len(s))): print(f'{i+1}: {s[i]}')
'@; wsl.exe -e python3 -c $py
~~~

### Command 13 — exit 0

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && sha256sum osfmk/mach/arm/thread_status.h osfmk/arm64/exception_asm.h && grep -nE "set_saved_state_pc|add_saved_state_pc|set_saved_state_reg|get_saved_state_reg|is_saved_state64|MANIPULATE_SIGNED|set_saved_state_cpsr" osfmk/mach/arm/thread_status.h osfmk/arm64/exception_asm.h && grep -nE "COPYIO_RECOVER_RANGE|^LEXT|Lcopy.*|^copyio_error" osfmk/arm64/machine_routines_asm.s'
~~~

### Command 14 — exit 0

~~~powershell
wsl.exe -e bash --noprofile --norc -c 'cd /home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2 && sha256sum osfmk/arm/machine_routines.h && grep -nE "io_read|io_write|probe|ml_io_map|ml_nofault|validate_nofault|ml_phys" osfmk/arm/machine_routines.h && find osfmk/arm osfmk/arm64 -maxdepth 2 -type f \( -name io.h -o -name misc_protos.h -o -name cpu_data.h \) -print'
~~~

### Command 15 — exit 0

~~~powershell
$paths='ports/hakios-lithium/peripherals/touch-mmio-policy.h','ports/hakios-lithium/peripherals/touch-mmio-access.inc'; foreach($p in $paths){if(!(Test-Path -LiteralPath $p)){throw "Missing specified candidate: $p"}; $v=Get-Content -LiteralPath $p; '{0} SHA256={1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; for($i=0;$i -lt $v.Count;$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}}
~~~

### Command 16 — exit 0

~~~powershell
$j=Get-Content -Raw -LiteralPath project.json | ConvertFrom-Json; $j | Select-Object active_workspace,active_port_path,active_manifest_path,active_entrypoint,source_lock_path | ConvertTo-Json -Depth 8; Get-Item -LiteralPath upstream/hakios-xnu-kernel/xnu/hakios.patch,ports/hakios-lithium/patches/0010-lithium-console-capture.patch,scripts/hakios-dependencies.py,scripts/prepare-hakios-ios16.py,scripts/check-hakios-ios16-build.py -ErrorAction SilentlyContinue | Select-Object FullName,Length; $targets='upstream/hakios-xnu-kernel/xnu/hakios.patch','scripts/hakios-dependencies.py','scripts/check-hakios-ios16-build.py'; foreach($p in $targets){if(Test-Path -LiteralPath $p){Select-String -LiteralPath $p -Pattern 'sleh.c','console','hakios.patch','dependency','adapter','apply','patch','source_sync' | Select-Object Path,LineNumber,Line}}
~~~

### Command 17 — exit 0

~~~powershell
$targets=@{'upstream/hakios-xnu-kernel/xnu/hakios.patch'='sleh.c|serial_console.c|console|^diff --git';'scripts/hakios-dependencies.py'='console|hakios.patch|adapter|source_sync|pinned|reconstruct|apply';'scripts/check-hakios-ios16-build.py'='dependencies|source|hash|hakios.patch|console'}; foreach($p in $targets.Keys){'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; Select-String -LiteralPath $p -Pattern $targets[$p] | ForEach-Object {'{0}:{1}: {2}' -f $p,$_.LineNumber,$_.Line}}
~~~

### Command 18 — exit 0

~~~powershell
$specs=@(@{p='scripts/hakios-dependencies.py';s=@(@(1,82),@(245,355))},@{p='ports/hakios-lithium/patches/0005-lithium-console-capture.patch';s=@(@(1,210))},@{p='upstream/hakios-xnu-kernel/xnu/apply.sh';s=@(@(1,200))}); foreach($spec in $specs){$p=$spec.p; if(!(Test-Path -LiteralPath $p)){throw "Missing $p"}; 'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; $v=Get-Content -LiteralPath $p;foreach($span in $spec.s){for($i=$span[0]-1;$i -lt [Math]::Min($span[1],$v.Count);$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}}}; Select-String -LiteralPath scripts/hakios.py -Pattern 'def inputs','def prepare','patch','kernel_source_fingerprint','active_kernel_source','deps' | ForEach-Object {'scripts/hakios.py:{0}: {1}' -f $_.LineNumber,$_.Line}; $j=Get-Content -Raw -LiteralPath config/hakios-lithium.json | ConvertFrom-Json; $j.PSObject.Properties | Where-Object {$_.Name -match 'patch|kernel|source|workspace|path'} | ForEach-Object {$_.Name; $_.Value | ConvertTo-Json -Depth 8}
~~~

### Command 19 — exit 0

~~~powershell
$ErrorActionPreference='Stop'; foreach($p in @('ports/hakios-lithium/patches/0005-lithium-console-capture.patch','upstream/hakios-xnu-kernel/xnu/apply.sh')){$v=Get-Content -LiteralPath $p;for($i=0;$i -lt [Math]::Min(210,$v.Count);$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}}; $p='scripts/hakios.py';'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;$v=Get-Content -LiteralPath $p;for($i=45;$i -lt 219;$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]};for($i=240;$i -lt 295;$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}
~~~

### Command 20 — exit 0

~~~powershell
$ErrorActionPreference='Stop'; $p='scripts/hakios.py';$v=Get-Content -LiteralPath $p;for($i=50;$i -lt 72;$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}; $p='scripts/run-wsl-limited.ps1';'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;Select-String -LiteralPath $p -Pattern 'hakios-prepare','hakios-deps','hakios-config','hakios-review','hakios-kernel','hakios-check','hakios-ios16-build','hakios-touch' | ForEach-Object {'{0}:{1}: {2}' -f $p,$_.LineNumber,$_.Line}; $p='ports/hakios-lithium/peripherals/kernel.inc';'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; Select-String -LiteralPath $p -Pattern 'mmio','touch_stage','touch_runtime','physical','handoff' | ForEach-Object {'{0}:{1}: {2}' -f $p,$_.LineNumber,$_.Line};'PATCH_SLEH_MATCHES';(Select-String -LiteralPath upstream/hakios-xnu-kernel/xnu/hakios.patch -Pattern 'sleh.c' | Measure-Object).Count
~~~

### Command 21 — exit 0

~~~powershell
$py=@'
from pathlib import Path
import re,hashlib,subprocess
p=Path('/mnt/f/IOS-MIX1/ports/hakios-lithium/peripherals/touch-mmio-policy.h')
s=p.read_text(); mask=int(re.search(r'mask = UINT32_C\(0x([0-9a-fA-F]+)\)',s).group(1),16); wanted=int(re.search(r'wanted = UINT32_C\(0x([0-9a-fA-F]+)\)',s).group(1),16)
print('policy_sha256',hashlib.sha256(p.read_bytes()).hexdigest(),'mask',hex(mask),'wanted',hex(wanted))
for label,e in [('baseline_read',0x96000010),('read_EA1',0x96000210),('write',0x96000050),('FnV1',0x96000410),('S1PTW1',0x96000090),('CM1',0x96000110),('ISV1',0x97000010),('SET3',0x96001810),('bit13',0x96002010),('translation',0x96000004),('tablewalk',0x96000015),('parity',0x96000018),('alignment',0x96000021),('SError',0xbe000000)]:
 print(label,hex(e),'candidate='+str((e&mask)==wanted),'strict_read='+str((e&0xfffffdff)==0x96000010))
words=[int(v,16) for v in re.findall(r'case UINT32_C\(0x([0-9a-fA-F]+)\)',s)]
print('allowlist_words',len(words),'unique',len(set(words)),'4KiB_windows',len({a&~4095 for a in words}),'all_aligned',all(a%4==0 for a in words))
b=Path('/home/jia/ios-mix1-build/hakios-lithium-ios16'); tree=b/'xnu-src/xnu-8796.121.2'
for rel in ['osfmk/arm64/sleh.c','osfmk/arm64/loose_ends.c','osfmk/arm64/machine_routines_asm.s']:
 data=(tree/rel).read_bytes(); r=subprocess.run(['git','--no-optional-locks','show','HEAD:'+rel],cwd=tree,capture_output=True,check=True); print(rel,'sha256',hashlib.sha256(data).hexdigest(),'equals_pinned_HEAD',data==r.stdout)
p=b/'xnu/hakios.patch'; s=p.read_text(); print('workspace_hakios.patch_sha256',hashlib.sha256(p.read_bytes()).hexdigest(),'sleh_matches',len(re.findall('sleh.c',s)))
p=tree/'osfmk/mach/arm/thread_status.h';s=p.read_text().splitlines()
for a,z in [(630,722),(770,825)]:
 print(f'--- {p}:{a}-{z} ---')
 for i in range(a-1,z):print(f'{i+1}: {s[i]}')
'@; wsl.exe -e python3 -c $py
~~~

### Command 22 — exit 0

~~~powershell
$ErrorActionPreference='Stop'; $paths='ports/hakios-lithium/peripherals/touch-mmio-policy.h','ports/hakios-lithium/peripherals/touch-mmio-access.inc'; foreach($p in $paths){$v=Get-Content -LiteralPath $p; 'HASH {0} {1}' -f $p,(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash; for($i=0;$i -lt $v.Count;$i++){'{0}:{1}: {2}' -f $p,($i+1),$v[$i]}}; 'REPORT_EXISTS={0}' -f (Test-Path -LiteralPath reports/hakios-ios16-touch-mmio-recovery-review-20261119.md)
~~~

</details>

