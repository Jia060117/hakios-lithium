#!/usr/bin/env python3
"""Generate a narrow device contract patch against the recorded complete HakiOS worktree."""
from pathlib import Path
import difflib
import hakios

def main():
    spec = hakios.read(hakios.MANIFEST)
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work / 'ios-mix1-workspace.json'))
    changes = {}
    def edit(name, old, new):
        text = changes.get(name, (work / name).read_text())
        hakios.require(text.count(old) == 1, 'Unexpected patch context: ' + name + ': ' + old[:60])
        changes[name] = text.replace(old, new)
    edit('xnuloader/loader.c', '#define BLOCK_DARWIN_TYPE  { 0x7c3457ef, 0x0000, 0x11aa, { 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac } }', '''#if defined(HAKIOS_LITHIUM)
#define BLOCK_DARWIN_NAME "userdata"
#define BLOCK_DARWIN_TYPE { 0x1b81e7e6, 0xf50d, 0x419b, { 0xa7, 0x39, 0x2a, 0xee, 0xf8, 0xda, 0x33, 0x35 } }
#define BLOCK_LITHIUM_FIRST 98304ULL
#define BLOCK_LITHIUM_BLOCKS 61208543ULL
#define BLOCK_LITHIUM_END 5242880ULL
#else
#define BLOCK_DARWIN_NAME "darwin"
#define BLOCK_DARWIN_TYPE  { 0x7c3457ef, 0x0000, 0x11aa, { 0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac } }
#endif''')
    edit('xnuloader/loader.c', 'static const char name[] = "darwin";', 'static const char name[] = BLOCK_DARWIN_NAME;')
    edit('xnuloader/loader.c', '} else if (bio->media->read_only) {', '''#if defined(HAKIOS_LITHIUM)
	} else if (info->start_lba != BLOCK_LITHIUM_FIRST || bp->blocks != BLOCK_LITHIUM_BLOCKS) {
		why = "MIX userdata geometry differs from the installed contract";
#endif
	} else if (bio->media->read_only) {''')
    edit('xnuloader/loader.c', 'lba < BLOCK_WRITE_FIRST) {', '''lba < BLOCK_WRITE_FIRST
#if defined(HAKIOS_LITHIUM)
			    || lba >= BLOCK_LITHIUM_END || count > BLOCK_LITHIUM_END - lba
#endif
			) {''')
    edit('drivers/storage/hakios.ufs.c', '#define UFS_DARWIN_MIN     5242875ULL', '''#if defined(LITHIUM_ARMV8_0)
#define UFS_DARWIN_NAME "userdata"
#define UFS_LITHIUM_BLOCKS 61208543ULL
#define UFS_LITHIUM_END 5242880ULL
#else
#define UFS_DARWIN_NAME "darwin"
#endif
#define UFS_DARWIN_MIN     5242875ULL''')
    edit('drivers/storage/hakios.ufs.c', 'strncmp((const char *)ufs_page->part[i].name, "darwin", 7)', 'strncmp((const char *)ufs_page->part[i].name, UFS_DARWIN_NAME, sizeof(UFS_DARWIN_NAME))')
    edit('drivers/storage/hakios.ufs.c', 'return ufs_part_bytes((uint32_t)ufs_write_part) - UFS_WRITE_FIRST * 4096;', '''#if defined(LITHIUM_ARMV8_0)
	return (UFS_LITHIUM_END - UFS_WRITE_FIRST) * 4096;
#else
	return ufs_part_bytes((uint32_t)ufs_write_part) - UFS_WRITE_FIRST * 4096;
#endif''')
    edit('drivers/storage/hakios.ufs.c', '} else if (ufs_page->part[i].blocks < UFS_DARWIN_MIN) {', '''#if defined(LITHIUM_ARMV8_0)
			} else if (ufs_page->part[i].blocks != UFS_LITHIUM_BLOCKS || ufs_page->part[i].start_lba != 98304ULL) {
				why = "MIX userdata geometry differs from the installed contract";
#endif
			} else if (ufs_page->part[i].blocks < UFS_DARWIN_MIN) {''')
    # Data block and raw-store requests share the upstream write function.
    edit('drivers/storage/hakios.ufs.c', 'lck_mtx_lock(&ufs_write_mtx);\n\t/*', '''#if defined(LITHIUM_ARMV8_0)
	if (offset / 4096 >= UFS_LITHIUM_END || len / 4096 > UFS_LITHIUM_END - offset / 4096) {
		ufs_write_refused++;
		return EROFS;
	}
#endif
	lck_mtx_lock(&ufs_write_mtx);
	/*''')
    edit('sysimage/mkvarskel.py', "    ('/private/var/containers/Shared', 0o755, 0, 0),", "    ('/private/var/containers/Shared', 0o755, 0, 0),\n    ('/private/var/containers/Shared/SystemGroup', 0o755, 0, 0),\n    ('/private/var/containers/Shared/AppGroup', 0o755, 0, 0),")
    patch = ''.join(''.join(difflib.unified_diff((work/name).read_text().splitlines(True), text.splitlines(True), fromfile='a/'+name, tofile='b/'+name)) for name, text in sorted(changes.items()))
    path = hakios.ROOT / spec['port_path'] / 'patches/0003-lithium-ios16-storage.patch'
    path.write_text(patch)
    print('Generated device-only storage/name/range and directory patch: ' + str(path))

if __name__ == '__main__':
    main()
