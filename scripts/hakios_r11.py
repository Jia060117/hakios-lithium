"""R11 exact diagnostics on complete HakiOS R10; no service shortcuts."""
import difflib
import re
import subprocess
from hakios_boot_observer import once


def inject_source(text):
    text=once(text,'char line[256];','char line[1024];')
    text=once(text,'#include "../../devices/lithium/diagnostics/ls-ready-observer.inc"',
        '#include "../../devices/lithium/diagnostics/service-progress.inc"\n'
        '#include "../../devices/lithium/diagnostics/ls-ready-r11.inc"')
    return once(text,'if (strcmp(name, "lsd") == 0) lithium_ls_watch(cache, length);',
        'if (strcmp(name, "lsd") == 0 || strncmp(name, "containermanagerd", 17) == 0) {\n'
        '\t\tlp_install(name, cache, length);\n'
        '\t\tif (strcmp(name, "lsd") == 0) lithium_ls_watch(cache, length);\n\t\tlp_launch();\n\t}')


def sb_source(text):
    text=once(text,'static uintptr_t slide;', 'static uintptr_t slide;\n'
        '#include "../../devices/lithium/diagnostics/service-progress.inc"')
    return once(text,'\tslide = (uintptr_t)_dyld_get_shared_cache_range(&length) - CACHE_BASE;',
        '\tconst void *lithium_cache = _dyld_get_shared_cache_range(&length);\n'
        '\tslide = (uintptr_t)lithium_cache - CACHE_BASE;\n'
        '\tlp_install("SpringBoard", lithium_cache, length);\n\tif (lp_expected) lp_launch();')


def trace_source(text):
    return once(text,'\tlithium_ipc_history_dump();',
        '\tlithium_ipc_history_dump();\n'
        '\textern void lithium_fs_progress_dump(void);\n\tlithium_fs_progress_dump();')


def return_source(text):
    return once(text,'    uint32_t n=0; *lost=0;',
        '    /* R11 caps reader hold time to one writer chunk; USB may send shorter frames. */\n'
        '    if (max > 256) max = 256;\n    uint32_t n=0; *lost=0;')


def syscall_source(text):
    text=once(text,'extern const char *syscallnames[];', 'extern const char *syscallnames[];\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '#include "../../../../../devices/lithium/diagnostics/fs-progress.inc"\n#endif')
    old='\terror = (*(callp->sy_call))(proc, &uthread->uu_arg[0], &(uthread->uu_rval[0]));'
    text=once(text,old,'#if defined(LITHIUM_ARMV8_0)\n'
        '\tint lithium_fs_slot = lfs_start(proc, syscode, (const uint64_t *)&uthread->uu_arg[0]);\n#endif\n'+old+
        '\n#if defined(LITHIUM_ARMV8_0)\n\tlfs_end(lithium_fs_slot, error, uthread->uu_rval[0]);\n#endif')
    return once(text,'\tarm_prepare_syscall_return(callp, regs, uthread, error);',
        '#if defined(LITHIUM_ARMV8_0)\n\tlfs_return_thread(proc, code, error, uthread->uu_rval[0]);\n#endif\n'
        '\tarm_prepare_syscall_return(callp, regs, uthread, error);')


def xnu_patch(text,work):
    name='bsd/dev/arm/systemcalls.c'
    base=subprocess.check_output(['git','show','HEAD:'+name],cwd=work/'xnu-src/xnu-8796.121.2',text=True)
    # Keep the whole pinned HakiOS syscall implementation (including systrace),
    # then regenerate its full dependency diff from Apple's pinned HEAD.
    after=syscall_source((work/'xnu-src/xnu-8796.121.2'/name).read_text())
    pattern=r'(?ms)^diff --git a/'+re.escape(name)+' b/'+re.escape(name)+r'\n.*?(?=^diff --git |\Z)'
    text=re.sub(pattern,'',text)
    return text+'diff --git a/'+name+' b/'+name+'\n'+''.join(difflib.unified_diff(
        base.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
