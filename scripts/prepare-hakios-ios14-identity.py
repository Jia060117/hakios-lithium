"""Stage reviewed R31 XML/persona integration without touching the phone."""
from datetime import datetime, timezone, timedelta
import difflib
import shutil
import hakios


def replace_once(text, before, after):
    assert text.count(before) == 1, before[:100]
    return text.replace(before, after, 1)


def patch(files):
    return ''.join(''.join(difflib.unified_diff(a.splitlines(True), b.splitlines(True),
        fromfile='a/'+n, tofile='b/'+n)) for n,(a,b) in files.items())


def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    state=hakios.read(hakios.ROOT/'project.json')
    assert state['work_status']['hakios_temporary_boot_count']==30
    work=hakios.verify_workspace(spec)
    original=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-audit.json')
    assert original['baseline']==hakios.PIN and original['CONFIG_PERSONAS_enabled']
    signed=original['original_signatures']['System/Library/CoreServices/SpringBoard.app/SpringBoard']
    assert signed['entitlements']['com.apple.springboard-ui.client'] is True
    before_report=hakios.read(hakios.ROOT/'reports/hakios-dependencies.json')
    assert before_report['kernel_source_fingerprint']==hakios.kernel_source_fingerprint(work)
    port=hakios.ROOT/spec['port_path'];tree=work/'xnu-src/xnu-8796.121.2'
    sources={}
    name='bsd/kern/kern_proc.c';a=(tree/name).read_text()
    helper=(port/'compatibility/ios14-xml-entitlements.c').read_text()
    b=replace_once(a,'static int\ncsops_internal(pid_t pid,',helper+'\nextern void lithium_identity_xml_observed(uint32_t pid, size_t bytes, int error, int probe);\n\nstatic int\ncsops_internal(pid_t pid,')
    replacement='''error = lithium_csops_xml(blob, usize, uaddr);
			if (strcmp(pt->p_comm, "SpringBoard") == 0) {
				void *xml = NULL;
				size_t bytes = 0;
				(void)csblob_get_entitlements(blob, &xml, &bytes);
				lithium_identity_xml_observed(upid, bytes, error, usize < bytes);
			}
			goto out;'''
    start=b.index('\tcase CS_OPS_ENTITLEMENTS_BLOB: {');end=b.index('\tcase CS_OPS_DER_ENTITLEMENTS_BLOB: {',start)
    block=b[start:end]
    block=replace_once(block,'if (!osent) {\n\t\t\tgoto out;\n\t\t}', 'if (!osent) {\n\t\t\t'+replacement+'\n\t\t}')
    block=replace_once(block,'} else {\n\t\t\tgoto out;\n\t\t}', '} else {\n\t\t\t'+replacement+'\n\t\t}')
    b=b[:start]+block+b[end:];sources[name]=(a,b)
    name='bsd/kern/bsd_init.c';a=(tree/name).read_text()
    helper=(port/'compatibility/ios14-system-proxy.c').read_text()
    b=replace_once(a,'void\nbsd_init(void)', '#if CONFIG_PERSONAS\n#include <sys/persona.h>\n'+helper+'\nextern void lithium_identity_persona_bootstrap_observed(uint32_t id, int error);\n#endif\n\nvoid\nbsd_init(void)')
    b=replace_once(b,'\tbsd_init_kprintf("calling bsd_utaskbootstrap\\n");', '''#if CONFIG_PERSONAS
	uint32_t lithium_ui = 0;
	if (PE_parse_boot_argn("lithium_ui", &lithium_ui, sizeof(lithium_ui)) && lithium_ui == 1) {
		uid_t identity = PERSONA_ID_NONE;
		int identity_error = lithium_system_proxy_bootstrap(&identity);
		lithium_identity_persona_bootstrap_observed(identity, identity_error);
	}
#endif
	bsd_init_kprintf("calling bsd_utaskbootstrap\\n");''')
    sources[name]=(a,b)
    name='bsd/kern/sys_persona.c';a=(tree/name).read_text()
    b=replace_once(a,'static int\nkpersona_find_syscall(', 'extern void lithium_identity_persona_find_observed(int error, size_t count);\n\nstatic int\nkpersona_find_syscall(')
    start=b.index('kpersona_find_syscall(');end=b.index('/*\n * Syscall entry',start);block=b[start:end]
    block=replace_once(block,'struct kpersona_info kinfo;', 'struct kpersona_info kinfo = {0};')
    needle='\t(void)copyout(&k_idlen, idlenp, sizeof(u_idlen));\n\n\treturn error;'
    block=replace_once(block,needle,'''\t(void)copyout(&k_idlen, idlenp, sizeof(u_idlen));
	if (kinfo.persona_type == PERSONA_SYSTEM_PROXY) {
		lithium_identity_persona_find_observed(error, k_idlen);
	}
	return error;''')
    b=b[:start]+block+b[end:];sources[name]=(a,b)
    target=port/'compatibility/ios14-identity.patch';assert not target.exists(),'R31 review already exists; review it before regenerating'
    target.write_text(patch(sources))
    hakios.write(target.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(target),
        before_files={n:hakios.sha(tree/n) for n in sources},
        preserved=['audit-token pid/version validation','code-signature flags','original XML special-slot hash verification',
                  'native OSEntitlements path','DER path','full persona syscall/lookup/spawn implementation'],
        XML_policy='existing validated original XML only; no assertion bypass or grant-all',
        persona_policy='one boot-scoped type6 list entry with allocated ID and KAUTH_UID_NONE; caller credentials follow upstream spawn',
        hardware_verified=False))
    # Add persistent counters to the complete HakiOS overlay and summary.
    name='xnu/files/osfmk/arm64/loader_return.c';a=(work/name).read_text()
    observation='''
static uint32_t lithium_identity_xml_probes, lithium_identity_xml_copies, lithium_identity_xml_bytes;
static int lithium_identity_xml_error = -1, lithium_identity_persona_boot_error = -1;
static int lithium_identity_persona_find_error = -1;
static uint32_t lithium_identity_persona_id = ~0u, lithium_identity_persona_queries, lithium_identity_persona_count;

void
lithium_identity_xml_observed(uint32_t pid, size_t bytes, int error, int probe)
{
    (void)pid;
    os_atomic_store(&lithium_identity_xml_bytes, (uint32_t)bytes, relaxed);
    os_atomic_store(&lithium_identity_xml_error, error, relaxed);
    if (probe) os_atomic_inc(&lithium_identity_xml_probes, relaxed);
    else if (error == 0) os_atomic_inc(&lithium_identity_xml_copies, relaxed);
}

void
lithium_identity_persona_bootstrap_observed(uint32_t id, int error)
{
    lithium_identity_persona_id = id;
    lithium_identity_persona_boot_error = error;
    printf("LITHIUM IDENTITY PERSONA bootstrap_rc=%d id=%u type=6\\n", error, id);
}

void
lithium_identity_persona_find_observed(int error, size_t count)
{
    os_atomic_inc(&lithium_identity_persona_queries, relaxed);
    os_atomic_store(&lithium_identity_persona_find_error, error, relaxed);
    os_atomic_store(&lithium_identity_persona_count, (uint32_t)count, relaxed);
}

'''
    b=replace_once(a,'static void\nlithium_ui_save(void)\n{',observation+'static void\nlithium_ui_save(void)\n{')
    at=b.index('lithium_ui_save(void)\n{');end=b.index('\n}\n',at)
    old=b[at:end]
    new=old+'''
    snprintf(line, sizeof(line), "LITHIUM IDENTITY FINAL sb_xml_probes=%u copies=%u bytes=%u rc=%d persona_boot_rc=%d id=%u find_calls=%u find_rc=%d count=%u\\n",
        os_atomic_load(&lithium_identity_xml_probes, relaxed), os_atomic_load(&lithium_identity_xml_copies, relaxed),
        os_atomic_load(&lithium_identity_xml_bytes, relaxed), os_atomic_load(&lithium_identity_xml_error, relaxed),
        lithium_identity_persona_boot_error, lithium_identity_persona_id,
        os_atomic_load(&lithium_identity_persona_queries, relaxed), os_atomic_load(&lithium_identity_persona_find_error, relaxed),
        os_atomic_load(&lithium_identity_persona_count, relaxed));
    loader_log_line(line);'''
    b=b[:at]+new+b[end:];common={name:(a,b)}
    name='xnuloader/loader.c';a=(work/name).read_text()
    b=replace_once(a,'"LITHIUM UI FINAL ", "LITHIUM UI DEADLINE ",', '"LITHIUM UI FINAL ", "LITHIUM IDENTITY FINAL ", "LITHIUM UI DEADLINE ",')
    common[name]=(a,b)
    common_path=port/'patches/0020-lithium-ios14-identity-summary.patch'
    assert not common_path.exists();common_path.write_text(patch(common))
    # Clone the fully reviewed R30 root/libraries, keeping its frozen sources intact.
    prior=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-integration-preparation.json')
    now=datetime.now(timezone(timedelta(hours=8)))
    stage='artifacts/hakios/ios14-ui-integration/'+now.strftime('%Y%m%d-%H%M%S-%f')
    old=hakios.ROOT/prior['staging_path'];dest=hakios.ROOT/stage
    shutil.copytree(old,dest)
    def relocate(obj):
        if isinstance(obj,dict):return {k:relocate(v) for k,v in obj.items()}
        if isinstance(obj,list):return [relocate(v) for v in obj]
        if isinstance(obj,str) and obj.startswith(prior['staging_path']+'/'):
            return stage+obj[len(prior['staging_path']):]
        return obj
    prepared=relocate(prior)
    prepared.update(recorded_at=now.isoformat(),staging_path=stage,bootable_package_ready=False,
        round=31,full_kernel_compiled=False,identity_integration_review='ports/hakios-lithium/compatibility/ios14-identity.json')
    prepared['implementation_gaps']=[s for s in prepared['implementation_gaps'] if 'persona' not in s]
    prepared['implementation_gaps'] += ['R31 original XML csops and real system-proxy bootstrap require full compilation and phone evidence']
    hakios.write(hakios.ROOT/'reports/hakios-ios14-ui-integration-preparation.json',prepared)
    state['work_status'].update(status='hakios_r31_identity_integration_prepared',current_turn_device_boots=False,
        current_turn_partition_writes=False,approved_preparation_completed=False)
    state.update(next_device_boot_entry=None,next_test_ready_report=None)
    hakios.write(hakios.ROOT/'project.json',state)
    print('R31 reviewed signature/persona adapters staged; complete build still required; no phone operations')


if __name__=='__main__':main()
