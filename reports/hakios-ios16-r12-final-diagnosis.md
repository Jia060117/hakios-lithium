# R12 raw UFS measurements

R12 ran once on the MIX using the complete HakiOS/XNU8796.121.2, D22AP iOS16.5 package. Fastboot accepted the temporary boot. The kernel registered three XNU CPUs at CNTFRQ=19200000; the fourth physical core remained in the loader service. The diagnostic ran before launchd and did not mount HFS. It measured 100 synchronous 4KiB operations per test in the same LITHIUM RAW key=value format.

~~~text
LITHIUM BOOTARGS debug=0x8 rd=md0 ufs_shared_pa=0x0000000040854000 hakios_kver=2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty bootcpu=2 cpus=3 serial=3 -v ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 loader_return_after=600 ufs_rawbench=1
LITHIUM RAW test=backup_read scope=wall count=100 errors=0 min_us=211 avg_us=224 max_us=460
LITHIUM RAW test=backup_read scope=firmware count=100 errors=0 min_us=114 avg_us=121 max_us=189
LITHIUM RAW test=write scope=wall count=100 errors=0 min_us=134 avg_us=146 max_us=385
LITHIUM RAW test=write scope=firmware count=100 errors=0 min_us=58 avg_us=59 max_us=62
LITHIUM RAW test=flush scope=wall count=100 errors=0 min_us=246 avg_us=312 max_us=552
LITHIUM RAW test=flush scope=firmware count=100 errors=0 min_us=206 avg_us=265 max_us=398
LITHIUM RAW test=write_flush scope=wall count=100 errors=0 min_us=387 avg_us=465 max_us=728
LITHIUM RAW test=write_flush scope=firmware count=100 errors=0 min_us=265 avg_us=325 max_us=460
LITHIUM RAW test=read scope=wall count=100 errors=0 min_us=196 avg_us=207 max_us=720
LITHIUM RAW test=read scope=firmware count=100 errors=0 min_us=100 avg_us=103 max_us=156
LITHIUM RAW test=restore_write scope=wall count=100 errors=0 min_us=136 avg_us=146 max_us=350
LITHIUM RAW test=restore_write scope=firmware count=100 errors=0 min_us=58 avg_us=60 max_us=72
LITHIUM RAW test=restore_read scope=wall count=100 errors=0 min_us=150 avg_us=173 max_us=784
LITHIUM RAW test=restore_read scope=firmware count=100 errors=0 min_us=53 avg_us=63 max_us=684
LITHIUM RAW restore backed=100 touched=100 restored=100 verified=100 mismatches=0
LITHIUM RAW end valid=1 complete=1 timeout=0 preflush_error=0 restore_flush_error=0
--- loader_return_after expired ---
LITHIUM LOG writer_contention_dropped_bytes=0
LITHIUM USB: normal timer return boundary; draining before quiesce
LITHIUM USB: timer boundary drain=1 target=12005
~~~

Every test had 100 successful samples and zero errors. The write+flush wall average was 465us, raw read average 207us, and the largest reported sample was 784us. The original 400KiB scratch area outside the installed image was fully restored and read back with zero mismatches.

This result selects the HFS/journal/metadata investigation branch: raw 4KiB operations are far below 2ms. It does not prove HFS itself caused R11's 20.549070-second rename. Metadata serialization, workload contention and filesystem journal behavior still need to be separated before changing policy. No filesystem tuning or additional benchmark ran in this revision.

The USB receiver reported Pipe error after the kernel's normal timer return and successful drain. Independent decoding verified all 54 frames and all 12,054 console bytes byte-for-byte, with no gaps, duplicates, partial frames or device loss. This verifies the captured kernel stream through the timer boundary. Original source logs outside the USB stream are not part of this capture. The phone's physical post-return screen was not observed; launchd and desktop were not reached.

The framed receiver stored 13,782 bytes, SHA256 29d45dd43b486acdbcfcf289c639779bdbf32d0bb900cd940865f7a06f48b49b. Console SHA256 is 270926ad905164f1486f097079168c503f6a1af34dcf23be8fdbffe20d9f14d. Full run files are under reports/local/20261117-230916-917288-ios16-r12/.
