"""R13 bounded HFS/VFS/bridge diagnostics, on the exact complete R12 tree."""
import difflib
import re
import subprocess
from hakios_boot_observer import once

PORT_DELTA={'patches/0014-lithium-hfs-diagnostics.patch','diagnostics/r13.json',
    'diagnostics/fsdiag.h','diagnostics/fsdiag-core.h','diagnostics/fsdiag.inc','diagnostics/cpu-sanity.inc','diagnostics/hfs-bench.inc'}
HFS_FILES=['hfs_vnops.c','hfs_readwrite.c','hfs_vfsutils.c','hfs_cnode.c','hfs_catalog.c','hfs_journal.c']
XNU_FILES=['bsd/kern/bsd_init.c','bsd/kern/kern_synch.c','osfmk/kern/sched_prim.c','bsd/vfs/vfs_bio.c']

def matches_prior(work,name,digest):
    """Accept only an exact reviewed R13 replacement of this exact prior input."""
    from hakios_r15 import matches_prior as reviewed_chain
    return reviewed_chain(work,name,digest)

def command_budget(cmdline,version):
    from hakios_service_wait_observer import command_budget as budget
    words=cmdline.split()
    if 'bootcpu=2' not in words or len([w for w in words if w.startswith('cpus=')])!=1 or not set(words)&{'cpus=1','cpus=3'}:
        raise RuntimeError('R13 requires bootcpu=2 and cpus=1 or cpus=3')
    return budget(cmdline.replace('cpus=3','cpus=1'),version)

def init_source(text):
    text=once(text,'int\nmain(int argc, char **argv, char **envp)',
        '#include "../../devices/lithium/diagnostics/hfs-bench.inc"\n\nint\nmain(int argc, char **argv, char **envp)')
    return once(text,'\tconfigure_inject();','\tlithium_hfs_bench();\n\tconfigure_inject();')

def mask(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text,flags=re.S)

def wrap(text,name,result,params,args,begin,end):
    """Wrap one actual definition; original body and return paths stay intact."""
    masked=mask(text)
    pattern=re.compile(r'(?m)^([ \t]*(?:static[ \t\n]+)?(?:(?:inline|__inline__)[ \t\n]+)?'+re.escape(result)+r'[ \t\n]+)'+re.escape(name)+r'\s*\(')
    found=[]
    for match in pattern.finditer(masked):
        at=match.end();depth=1
        while depth:
            if masked[at]=='(':depth+=1
            if masked[at]==')':depth-=1
            at+=1
        while masked[at].isspace():at+=1
        if masked[at]=='{':found.append((match,at))
    if len(found)!=1:raise RuntimeError(f'Expected one definition of {name}; got {len(found)}')
    m,brace=found[0];endbody=brace+1;depth=1
    while depth:
        if masked[endbody]=='{':depth+=1
        if masked[endbody]=='}':depth-=1
        endbody+=1
    original=text[m.start():endbody]
    renamed=re.sub(r'\b'+re.escape(name)+r'\s*\(',name+'_r13_impl(',original,count=1)
    # Keep the original linkage in the wrapper, make the preserved body local.
    if not re.match(r'\s*static\b',renamed):renamed='static '+renamed.lstrip()
    linkage='static ' if re.match(r'\s*static\b',original) else ''
    call=f'{name}_r13_impl({args})'
    invocation=f'{result} value={call};' if result!='void' else call+';'
    ret='return value;' if result!='void' else ''
    wrapper=(f'\nstatic {result} {name}_r13_impl({params});\n{linkage}{result}\n{name}({params})\n{{\n'
        f'#if defined(LITHIUM_ARMV8_0)\n    {begin}\n#endif\n    {invocation}\n'
        f'#if defined(LITHIUM_ARMV8_0)\n    {end}\n#endif\n    {ret}\n}}\n')
    return text[:m.start()]+wrapper+renamed+text[endbody:]

def include(text):
    return '#if defined(LITHIUM_ARMV8_0)\n#include "../../bsd/sys/hakios.fsdiag.h"\n#endif\n'+text

def stage(text,name,result,params,args,label,object='0',owner='0'):
    return wrap(text,name,result,params,args,
        f'uint64_t token=lithium_fsdiag_stage_begin("{label}",(uintptr_t)({object}),(uintptr_t)({owner}));',
        'lithium_fsdiag_stage_end(token,'+('value' if result in ('int','errno_t') else '0')+');')

def hfs_transform(name,text):
    if name=='hfs_vnops.c':
        for fun,arg,op,vp,fr,to in [('hfs_vnop_renamex','vnop_renamex_args','LF_RENAME','ap->a_fvp','ap->a_fcnp->cn_nameptr','ap->a_tcnp->cn_nameptr'),
            ('hfs_vnop_fsync','vnop_fsync_args','LF_FSYNC','ap->a_vp','NULL','NULL'),
            ('hfs_vnop_close','vnop_close_args','LF_CLOSE','ap->a_vp','NULL','NULL')]:
            text=wrap(text,fun,'int',f'struct {arg} *ap','ap',
                f'const char *mountname=vfs_statfs(vnode_mount({vp}))->f_mntfromname;\n'
                f'    uint64_t id=strnstr(mountname,"ufs_darwin_data",MNAMELEN)?lithium_fsdiag_begin({op},{vp},{fr},{to}):0;',
                'lithium_fsdiag_end(id,value);')
    elif name=='hfs_readwrite.c':
        text=wrap(text,'hfs_vnop_write','int','struct vnop_write_args *ap','ap',
            'const char *mountname=vfs_statfs(vnode_mount(ap->a_vp))->f_mntfromname;\n'
            '    uint64_t id=strnstr(mountname,"ufs_darwin_data",MNAMELEN)?lithium_fsdiag_begin(LF_WRITE,ap->a_vp,NULL,NULL):0;',
            'lithium_fsdiag_end(id,value);')
    elif name=='hfs_vfsutils.c':
        for fun in ['hfs_start_transaction','hfs_end_transaction']:
            text=stage(text,fun,'int','struct hfsmount *hfsmp','hfsmp',fun,'hfsmp')
        text=stage(text,'hfs_lock_global','int','struct hfsmount *hfsmp, enum hfs_locktype locktype','hfsmp,locktype',
            'hfs.global_lock','&hfsmp->hfs_global_lock','hfsmp->hfs_global_lockowner')
        # Catalog and allocation locks share this entry; individual cnode locks
        # underneath carry the actual object/owner sample.
        text=stage(text,'hfs_systemfile_lock','int','struct hfsmount *hfsmp, int flags, enum hfs_locktype locktype',
            'hfsmp,flags,locktype','hfs.systemfile_lock','hfsmp')
    elif name=='hfs_cnode.c':
        text=stage(text,'hfs_lock','int','struct cnode *cp, enum hfs_locktype locktype, enum hfs_lockflags flags',
            'cp,locktype,flags','hfs.cnode_lock','cp','cp->c_lockowner')
        text=stage(text,'hfs_lock_truncate','void','struct cnode *cp, enum hfs_locktype locktype, enum hfs_lockflags flags',
            'cp,locktype,flags','hfs.truncate_lock','cp','cp->c_truncatelockowner')
    elif name=='hfs_catalog.c':
        text=stage(text,'cat_rename','int','struct hfsmount *hfsmp, struct cat_desc *from_cdp, struct cat_desc *todir_cdp, struct cat_desc *to_cdp, struct cat_desc *out_cdp',
            'hfsmp,from_cdp,todir_cdp,to_cdp,out_cdp','hfs.cat_rename','hfsmp')
    elif name=='hfs_journal.c':
        for fun in ['journal_start_transaction','journal_end_transaction']:
            text=stage(text,fun,'int','journal *jnl','jnl',fun,'jnl','jnl->owner')
        text=stage(text,'journal_lock','void','journal *jnl','jnl','journal.mutex','jnl','jnl->owner')
        text=stage(text,'journal_flush','int','journal *jnl, journal_flush_options_t options','jnl,options','journal.flush','jnl','jnl->owner')
        # do_journal_io includes both strategy submission and buf_biowait;
        # buf_biowait is separately timed, so completion latency is visible.
        text=stage(text,'do_journal_io','size_t','journal *jnl, off_t *offset, void *data, size_t len, int direction',
            'jnl,offset,data,len,direction','journal.io','jnl')
    return include(text)

def xnu_transform(name,text):
    if name=='bsd/kern/bsd_init.c':
        text=once(text,'\tload_init_program(p);',
            '#if defined(LITHIUM_ARMV8_0)\n\tlithium_cpu_sanity();\n\tlithium_fsdiag_init();\n#endif\n\tload_init_program(p);')
    elif name=='bsd/kern/kern_synch.c':
        # Entry has the exact absolute deadline already computed by msleep/tsleep.
        pos=text.index('_sleep(');brace=text.index('{',pos)
        text=text[:brace+1]+'\n#if defined(LITHIUM_ARMV8_0)\n\tlithium_fsdiag_wait((uintptr_t)chan,abstime,wmsg);\n#endif\n'+text[brace+1:]
    elif name=='osfmk/kern/sched_prim.c':
        # Every timeout/leeway/deadline path ultimately arms this wait queue.
        # Record before splsched/lock acquisition; no scheduler semantics change.
        for fun in ['assert_wait_timeout','assert_wait_timeout_with_leeway','assert_wait_deadline','assert_wait_deadline_with_leeway']:
            m=re.search(r'(?m)^'+fun+r'\(',text)
            if not m:raise RuntimeError('Missing '+fun)
            brace=text.index('{',m.start());end=text.index('\n}',brace)
            piece=text[brace:end]
            needle='\ts = splsched();'
            if fun=='assert_wait_timeout':
                # Deadline is calculated while holding waitq lock in upstream;
                # the recorder does not sleep or take scheduler/filesystem locks.
                needle='\tclock_interval_to_deadline(interval, scale_factor, &deadline);'
            addition='\n#if defined(LITHIUM_ARMV8_0)\n\tlithium_fsdiag_wait((uintptr_t)event,deadline,"'+fun+'");\n#endif'
            piece=once(piece,needle,needle+addition)
            text=text[:brace]+piece+text[end:]
        m=re.search(r'(?m)^assert_wait\(',text);brace=text.index('{',m.start())
        text=text[:brace+1]+'\n#if defined(LITHIUM_ARMV8_0)\n\tlithium_fsdiag_wait((uintptr_t)event,0,NULL);\n#endif\n'+text[brace+1:]
        m=re.search(r'(?m)^thread_unblock\(',text);brace=text.index('{',m.start())
        text=text[:brace+1]+'\n#if defined(LITHIUM_ARMV8_0)\n\tlithium_fsdiag_wake(thread,wresult);\n#endif\n'+text[brace+1:]
    elif name=='bsd/vfs/vfs_bio.c':
        text=stage(text,'buf_biowait','errno_t','buf_t bp','bp','buf.biowait','bp')
        text=stage(text,'buf_flushdirtyblks','void','vnode_t vp, int wait, int flags, const char *msg',
            'vp,wait,flags,msg','buf.flushdirty','vp')
    return include(text)

def storage_source(text):
    text=once(text,'/*\n * Saving the data volume without being asked',
        '#if defined(LITHIUM_ARMV8_0)\n#include <sys/hakios.fsdiag.h>\n'
        'static uint64_t lf_now(void) {uint64_t ns;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;}\n'
        '#endif\n\n/*\n * Saving the data volume without being asked')
    # Per-request firmware elapsed time is the existing cumulative mailbox
    # counter delta, protected by the original ufs_lock. No protocol changes.
    text=wrap(text,'ufs_request','int','uint32_t op, uint32_t part, uint64_t lba, uint64_t count, uint8_t *dst, const uint8_t *src',
        'op,part,lba,count,dst,src',
        'uint64_t request_stage=lithium_fsdiag_stage_begin("ufs.request",(uintptr_t)ufs_page,0);\n'
        '    uint64_t began=lf_now(),firmware_before=ufs_page->answer.ticks;\n    uint32_t sleeps_before=lf_ufs_sleeps;',
        'uint64_t firmware_ticks=ufs_page->answer.ticks-firmware_before;\n'
        '    uint64_t freq=__builtin_arm_rsr64("CNTFRQ_EL0");\n'
        '    uint64_t firmware_ns=freq?(firmware_ticks/freq)*1000000000ULL+(firmware_ticks%freq)*1000000000ULL/freq:0;\n'
        '    lithium_fsdiag_ufs(op,lf_now()-began,firmware_ns,lf_ufs_sleeps-sleeps_before,value);\n'
        '    lithium_fsdiag_stage_end(request_stage,value);')
    text=once(text,'static LCK_GRP_DECLARE(ufs_lck_grp, "hakios-ufs");',
        'static LCK_GRP_DECLARE(ufs_lck_grp, "hakios-ufs");\n#if defined(LITHIUM_ARMV8_0)\nstatic uint32_t lf_ufs_sleeps;\n#endif')
    text=once(text,'\t\tIOSleep(1);', '#if defined(LITHIUM_ARMV8_0)\n\t\tlf_ufs_sleeps++;\n#endif\n\t\tIOSleep(1);')
    for fun,params,args in [('ufs_data_io','bool write, uint64_t offset, uint8_t *p, uint64_t len','write,offset,p,len'),
        ('ufs_data_flush','void','')]:
        text=wrap(text,fun,'int',params,args,'uint64_t stamp=lithium_fsdiag_bridge_begin();','lithium_fsdiag_bridge_end(stamp);')
    # Timed lock acquisition, preserving the original lock/unlock order.
    for lock in ['ufs_lock','ufs_write_mtx']:
        needle='lck_mtx_lock(&'+lock+');'
        repl='{\n#if defined(LITHIUM_ARMV8_0)\nuint64_t token=lithium_fsdiag_stage_begin("'+lock+'",(uintptr_t)&'+lock+',0);\n#endif\n'+needle+'\n#if defined(LITHIUM_ARMV8_0)\nlithium_fsdiag_stage_end(token,0);\n#endif\n}'
        text=text.replace(needle,repl)
    return text

def extend_patch(text,work,names,transform,base_fn,source_root=None):
    for name in names:
        base=base_fn(name);after=transform(name,((source_root or work/'xnu-src/xnu-8796.121.2')/name).read_text(errors='surrogateescape'))
        pattern=r'(?ms)^diff --git a/'+re.escape(name)+' b/'+re.escape(name)+r'\n.*?(?=^diff --git |\Z)'
        if re.search(pattern,text):text=re.sub(pattern,'',text)
        else:
            pattern=r'(?ms)^--- a/'+re.escape(name)+r'\n\+\+\+ b/'+re.escape(name)+r'\n.*?(?=^--- a/|\Z)'
            text=re.sub(pattern,'',text)
        text+='diff --git a/'+name+' b/'+name+'\n'+''.join(difflib.unified_diff(
            base.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
    return text

def xnu_patch(text,work,source_root=None):
    return extend_patch(text,work,XNU_FILES,xnu_transform,lambda n:subprocess.check_output(
        ['git','show','HEAD:'+n],cwd=work/'xnu-src/xnu-8796.121.2',text=True),source_root)

def hfs_patch(text,work,source_root=None):
    return extend_patch(text,work,['bsd/hfs/'+n for n in HFS_FILES],lambda n,t:hfs_transform(n.split('/')[-1],t),
        lambda n:(work/'xnu-src/hfs-627.100.6/core'/n.split('/')[-1]).read_text(errors='surrogateescape'),source_root)
