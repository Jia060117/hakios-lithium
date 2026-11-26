#!/usr/bin/env python3
"""Generate the bounded MIX warm UFS component and HakiOS P2 observation delta."""
import difflib
from pathlib import Path
import subprocess
import tempfile
import hakios

def replace(text, before, after):
    hakios.require(text.count(before) == 1, 'Storage anchor differs: ' + before[:80])
    return text.replace(before, after, 1)

def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    component = hakios.ROOT / 'upstream/research/ios-sailfish/shim/ufs.c'
    hakios.require(hakios.git(component.parents[1], 'rev-parse', 'HEAD') ==
        '6f27e41ff249f2680540ebe8ee5ab982c5e641c9' and hakios.sha(component) ==
        'e63da1abed62bd0aab0906b17e5984f5f11cdf108e6c1d3978c7ef77aa561b9f',
        'Pinned UFS component differs')
    original = component.read_text(encoding='utf-8')
    # Reuse only its transport. GPT and filesystem code come from complete Mu/HakiOS.
    transport = original[original.index('#define UFS_BASE'):original.index('static uint64_t le64')]
    transport = replace(transport, '0x01d84000UL', '0x00624000ULL')
    transport = replace(transport, 'sweet live FDT', 'lithium stock/live FDT')
    transport = replace(transport, 'aligned(1024)', 'aligned(4096)')
    transport = replace(transport, 'aligned(128)', 'aligned(4096)')
    transport = replace(transport, 'data[2 * BLOCK]', 'data[16 * BLOCK]')
    transport = replace(transport, '} prdt[1];\n', '} prdt[1];\n\tuint8_t cache_page_pad[4096 - 1040];\n')
    transport = replace(transport,
        'ufs_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(UFS_BASE + off) = v; }',
        'ufs_wr(uint32_t off, uint32_t v) { __asm__ volatile("dsb sy" ::: "memory");\n'
        '\t*(volatile uint32_t *)(UFS_BASE + off) = v; __asm__ volatile("dsb sy" ::: "memory"); }')
    transport = replace(transport, '\tstatic uint8_t tag;\n',
        '\tstatic uint8_t tag;\n'
        '\t/* Only TUR, capacity and bounded READ(10), LUN0, device-to-host. */\n'
        '\tif (!mActive || mDead || lun != 0 || !cdb || !ocs_out || !status_out ||\n'
        '\t    (cdb[0] != 0 && cdb[0] != 0x25 && cdb[0] != 0x28) ||\n'
        '\t    cdb_len > 16 || len > sizeof(data) || (len && buf != data) ||\n'
        '\t    (cdb[0] == 0 && (cdb_len != 6 || len != 0)) ||\n'
        '\t    (cdb[0] == 0x25 && (cdb_len != 10 || len != 8)) ||\n'
        '\t    (cdb[0] == 0x28 && (cdb_len != 10 || len == 0 ||\n'
        '\t      (uint32_t)(cdb[7] << 8 | cdb[8]) * BLOCK != len))) return 5;\n')
    transport = replace(transport, '\tufs_wr(REG_UTRLDBR, 1);',
        '\tWriteBackInvalidateDataCacheRange(utrl, sizeof(utrl));\n'
        '\tWriteBackInvalidateDataCacheRange(&ucd, sizeof(ucd));\n'
        '\tif (len) WriteBackInvalidateDataCacheRange(data, sizeof(data));\n'
        '\t/* Never ring a doorbell against LK/another kernel\'s descriptor list. */\n'
        '\tif (ufs_rd(REG_UTRLBA) != (uint32_t)(uintptr_t)utrl ||\n'
        '\t    ufs_rd(REG_UTRLBAU) != (uint32_t)((uint64_t)(uintptr_t)utrl >> 32) ||\n'
        '\t    !(ufs_rd(REG_UTRLRSR) & 1) || ufs_rd(REG_UTRLDBR)) { mDead = TRUE; return 5; }\n'
        '\tufs_wr(REG_UTRLDBR, 1);')
    transport = replace(transport, 'ticks() - start > 19200000',
        'ticks() - start > ({ uint64_t hz; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz)); hz; })')
    transport = replace(transport, '\t\t\treturn 1;\t\t\t/* timeout */',
        '\t\t\tmDead = TRUE; /* retain DMA buffers forever; no reuse after timeout */\n'
        '\t\t\treturn 1;\t\t\t/* timeout */')
    transport = replace(transport, '\t*ocs_out = utrl[0].dw2 & 0xff;',
        '\tInvalidateDataCacheRange(utrl, sizeof(utrl));\n'
        '\tInvalidateDataCacheRange(&ucd, sizeof(ucd));\n'
        '\tif (len) InvalidateDataCacheRange(data, sizeof(data));\n'
        '\t*ocs_out = utrl[0].dw2 & 0xff;')
    dest = hakios.ROOT / spec['port_path'] / 'firmware/lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc'
    dest.write_text('/* Generated from pinned ios-sailfish shim/ufs.c. See storage-provenance.json. */\n' + transport,
        encoding='utf-8', newline='\n')
    repo = component.parents[1]
    hakios.write(dest.parent / 'storage-provenance.json', dict(
        component_path=component.relative_to(hakios.ROOT).as_posix(),
        component_commit=hakios.git(repo, 'rev-parse', 'HEAD'), component_sha256=hakios.sha(component),
        generated_transport_sha256=hakios.sha(dest),
        scope='MSM8996 warm host and read-only UEFI BlockIO; Mu GPT and HakiOS bridge/HFS remain upstream implementations',
        changes=['MIX register base', 'UEFI cached DMA maintenance and page alignment',
            'command and list ownership checks', 'finite timeout without DMA reuse', '64KB bounce reads'],
        legacy_7195_runtime_reused=False, hardware_verified=False))
    # Apply only the declared earlier patches to a scratch copy of the fixed loader.
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for patch in hakios.common_patches(spec):
            if patch.name >= '0004': continue
            for path in __import__('re').findall(r'^--- a/(.*)$', patch.read_text(), __import__('re').M):
                out = root / path
                if out.exists(): continue
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(subprocess.check_output(['git','-C',str(source),'show',hakios.PIN+':'+path]))
            subprocess.run(['git','apply',str(patch)],cwd=root,check=True)
        path = 'xnuloader/loader.c'
        loader = (root / path).read_text()
    modified = replace(loader, '\tif (log == NULL) return;\n',
        '\tprint("P2 STORAGE LOADER:\\n");\n'
        '\tvolatile uint64_t *ufs_state = (volatile uint64_t *)(uintptr_t)0x415ff300ULL;\n'
        '\tif (ufs_state[0] == 0x465532505854494cULL) {\n'
        '\t\tprint("UEFI UFS stage=%d cap=%x hce=%x hcs=%x\\n",ufs_state[1],ufs_state[2],ufs_state[3],ufs_state[4]);\n'
        '\t\tprint("UEFI UFS ver=%x doorbell=%x rc_ocs_st=%x\\n",ufs_state[6],ufs_state[5],ufs_state[7]);\n'
        '\t}\n'
        '\tlithium_show_line(lithium_last_line(log_buf, log_len, "LITHIUM P2 LOADER "), log_buf + log_len);\n'
        '\tlithium_show_line(lithium_last_line(log_buf, log_len, "LITHIUM P2 STOP "), log_buf + log_len);\n'
        '\tif (log == NULL) { print("KERNEL NOT STARTED; STORAGE/PSCI STOP\\n"); return; }\n'
        '\tprint("P2 STORAGE KERNEL:\\n");\n'
        '\tstatic const char *p2[] = {"LITHIUM P2 START ", "LITHIUM P2 HEADER ", "LITHIUM P2 MOUNT ",\n'
        '\t    "LITHIUM P2 VERSION ", "LITHIUM P2 DYLD ", "LITHIUM P2 LAUNCHD ",\n'
        '\t    "LITHIUM P2 UNMOUNT ", "LITHIUM P2 RESULT "};\n'
        '\tfor (unsigned i=0; i<sizeof(p2)/sizeof(p2[0]); i++)\n'
        '\t    lithium_show_line(lithium_last_line(log, bytes, p2[i]), log + bytes);\n')
    modified = replace(modified, 'if (klog != NULL) lithium_return_screen(klog, klog_len);',
        'lithium_return_screen(klog, klog_len);')
    anchor = '\tif (checkpoint == CHECKPOINT_SELFTEST) {\n\t\tkernel_mpidr = this_mpidr;\n\t}'
    modified = replace(modified, anchor,
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tint lithium_storage_test = 0;\n'
        '\tfor (const char *c=pi.cmdline; *c; c++)\n'
        '\t\tif ((c == pi.cmdline || c[-1] == \' \') && starts_with(c,"lithium_storage_test=1") &&\n'
        '\t\t    (c[22] == 0 || c[22] == \' \')) lithium_storage_test = 1;\n'
        '\tif (lithium_storage_test) {\n'
        '\t\tint ready = 0;\n'
        '\t\tfor (uint32_t i=0; block_enabled && i<block_shared.parts; i++) {\n'
        '\t\t\tconst struct loader_block_part *p = &block_shared.part[i];\n'
        '\t\t\tif (memcmp(p->name, "userdata", 9) != 0 || p->flags & BLOCK_PART_UNIT) continue;\n'
        '\t\t\tefi_block_io *bio = block_bios[i];\n'
        '\t\t\tif (p->block_size == 4096 && p->start_lba == 98304 &&\n'
        '\t\t\t    p->blocks == 61208543ULL && bio->media->read_only && !block_write_requested &&\n'
        '\t\t\t    kernel_mpidr != this_mpidr &&\n'
        '\t\t\t    !EFI_ERROR(bio->read_blocks(bio,bio->media->media_id,0,8192,ufs_buffer)) &&\n'
        '\t\t\t    ufs_buffer[1024] == 0x48 && ufs_buffer[1025] == 0x58) ready++;\n'
        '\t\t}\n'
        '\t\tprint(ready == 1 ? "LITHIUM P2 LOADER READY userdata HFSX read_only=1\\n" :\n'
        '\t\t    "LITHIUM P2 LOADER FAIL userdata/geometry/HFSX/bootcpu\\n");\n'
        '\t\tif (ready != 1) { write_logs(NULL,0); return 1; }\n'
        '\t}\n'
        '#endif\n' + anchor)
    modified = replace(modified, '\t\t\t\tset_result("hung", loader_ctx[CTX_LOG_LEN], 0);',
        '#if defined(HAKIOS_LITHIUM)\n\t\t\t\tprint("LITHIUM P2 STOP kernel silence timeout\\n");\n#endif\n'
        '\t\t\t\tset_result("hung", loader_ctx[CTX_LOG_LEN], 0);')
    modified = replace(modified, '\t\t\t\tset_result("no-such-core", (uint64_t)(status < 0 ? -status : status), 0);',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\t\t\t\tprint("LITHIUM P2 STOP core=%x PSCI/status=%d\\n",kernel_mpidr,(uint64_t)(status < 0 ? -status : status));\n'
        '#endif\n\t\t\t\tset_result("no-such-core", (uint64_t)(status < 0 ? -status : status), 0);')
    patch = hakios.ROOT / spec['port_path'] / 'patches/0004-msm8996-storage-test.patch'
    patch.write_text(''.join(difflib.unified_diff(loader.splitlines(keepends=True), modified.splitlines(keepends=True),
        fromfile='a/'+path,tofile='b/'+path)),encoding='utf-8',newline='\n')
    hakios.write(patch.with_suffix('.json'), dict(baseline=hakios.PIN, patch_sha256=hakios.sha(patch),
        scope='MIX storage-test geometry/read-only preflight and retained P2 results; upstream block service unchanged',
        hardware_verified=False, ios14_compatibility_verified=False))
    print('Generated MIX warm UFS component and bounded HakiOS P2 loader patch')

if __name__ == '__main__': main()
