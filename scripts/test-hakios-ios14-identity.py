"""Exercise production XML size/copy semantics and persona ownership failures."""
from pathlib import Path
import struct
import subprocess
import hakios


def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    work=hakios.verify_workspace(spec);port=hakios.ROOT/spec['port_path'];tree=work/'xnu-src/xnu-8796.121.2'
    review=hakios.read(port/'compatibility/ios14-identity.json')
    assert review['patch_sha256']==hakios.sha(port/'compatibility/ios14-identity.patch')
    xml=(port/'compatibility/ios14-xml-entitlements.c').read_text()
    persona=(port/'compatibility/ios14-system-proxy.c').read_text()
    proc=(tree/'bsd/kern/kern_proc.c').read_text()
    init=(tree/'bsd/kern/bsd_init.c').read_text()
    assert xml in proc and persona in init
    # The kernel tests the signature's entitlement hash before copying it.
    original=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-audit.json')
    signed=original['original_signatures']['System/Library/CoreServices/SpringBoard.app/SpringBoard']
    root=Path('/home/jia/ios-mix1-build/hakios-ios14-root-ro/root')
    app=root/'System/Library/CoreServices/SpringBoard.app/SpringBoard'
    assert hakios.sha(app)==signed['sha256'] and signed['entitlements']['com.apple.springboard-ui.client'] is True
    raw=app.read_bytes();marker=struct.pack('>2I',0xfade7171,signed['xml_bytes'])
    at=raw.index(marker);assert raw.find(marker,at+1)<0
    blob=raw[at:at+signed['xml_bytes']]
    import hashlib
    assert hashlib.sha256(blob).hexdigest()==signed['xml_blob_sha256']
    out=work/'lithium-identity-host';out.mkdir(exist_ok=True)
    (out/'original-springboard-xml.bin').write_bytes(blob)
    begin=proc.index('static int\ncsops_copy_token(');end=proc.index('\n}\n',begin)+3
    copy=proc[begin:end]
    head='''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
typedef size_t user_size_t;
typedef uintptr_t user_addr_t;
static int copyout(const void *s, user_addr_t a, size_t n) { if (!a) return EFAULT; memcpy((void *)a,s,n); return 0; }
struct cs_blob { void *xml; size_t bytes; int error; };
static int csblob_get_entitlements(struct cs_blob *b, void **out, size_t *n) {
    *out=b->xml;*n=b->bytes;return b->error;
}
'''
    main_xml='''
int main(int argc, char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=(size_t)ftell(f);rewind(f);
    unsigned char *xml=malloc(n),*out=malloc(n+16);assert(xml&&out);assert(fread(xml,1,n,f)==n);fclose(f);
    struct cs_blob b={xml,n,0};memset(out,0xaa,n+16);
    assert(lithium_csops_xml(&b,8,(user_addr_t)out)==ERANGE);
    uint32_t length;memcpy(&length,out+4,4);assert(ntohl(length)==n);assert(out[0]==0 && out[8]==0xaa);
    puts("PASS XML original signed size probe returns ERANGE and network-order length");
    assert(lithium_csops_xml(&b,n,(user_addr_t)out)==0&&!memcmp(out,xml,n));
    puts("PASS XML exact buffer copies original SpringBoard blob");
    memset(out,0xaa,n+16);assert(lithium_csops_xml(&b,n+16,(user_addr_t)out)==0&&out[n]==0xaa);
    puts("PASS XML oversized buffer does not overwrite trailing bytes");
    memset(out,0xaa,n+16);assert(lithium_csops_xml(&b,7,(user_addr_t)out)==ERANGE&&out[0]==0xaa);
    puts("PASS XML insufficient header buffer remains untouched");
    assert(lithium_csops_xml(&b,n,0)==EFAULT);
    puts("PASS XML copyout fault propagates");
    b.error=80;memset(out,0xaa,n+16);assert(lithium_csops_xml(&b,n,(user_addr_t)out)==80&&out[0]==0xaa);
    puts("PASS XML signature validation error cannot copy permissions");
    b=(struct cs_blob){NULL,0,0};assert(lithium_csops_xml(&b,8,(user_addr_t)out)==0);
    assert(!memcmp(out,(char[8]){0},8));
    puts("PASS XML missing signature permissions return zero header");
    free(out);free(xml);return 0;
}
'''
    phead='''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
typedef uint32_t uid_t;
#define PERSONA_ID_NONE ((uid_t)-1)
#define KAUTH_UID_NONE ((uid_t)-1)
#define PERSONA_SYSTEM_PROXY 6
struct persona { uid_t pna_id; int pna_type; int refs; int listed; };
static struct persona object;static int existing_count,find_error,alloc_error,begin_error,lookup_error,wrong_type,alloc_calls;
static int persona_find_all(const char *name,uid_t id,int type,struct persona **p,size_t *n) {
    assert(!name&&id==PERSONA_ID_NONE&&type==6);if(find_error)return find_error;
    if(!existing_count)return ESRCH;*p=&object;object.refs++;*n=existing_count;return 0;
}
static void persona_put(struct persona *p) { assert(p&&p->refs>0);p->refs--; }
static struct persona *persona_alloc(uid_t id,const char *name,int type,char *path,uid_t uid,int *err) {
    assert(id==PERSONA_ID_NONE&&name&&!path&&uid==KAUTH_UID_NONE&&type==6);alloc_calls++;
    *err=alloc_error;if(alloc_error)return NULL;object=(struct persona){501,6,1,0};return &object;
}
static int persona_init_begin(struct persona *p) {assert(p==&object);return begin_error;}
static void persona_init_end(struct persona *p,int err) {assert(p==&object);p->listed=!err;}
static struct persona *persona_lookup(uid_t id) {
    assert(id==object.pna_id);if(lookup_error)return NULL;object.refs++;if(wrong_type)object.pna_type=7;return &object;
}
static void reset(void) {object=(struct persona){501,6,1,1};existing_count=find_error=alloc_error=begin_error=lookup_error=wrong_type=alloc_calls=0;}
'''
    pmain='''
int main(void) {
    uid_t id=PERSONA_ID_NONE;reset();assert(!lithium_system_proxy_bootstrap(&id)&&id==501&&object.refs==1&&object.listed&&alloc_calls==1);
    puts("PASS persona bootstrap creates one real list-owned entry and releases lookup reference");
    reset();existing_count=1;assert(!lithium_system_proxy_bootstrap(&id)&&object.refs==1&&!alloc_calls);
    puts("PASS persona existing identity is reused without allocation");
    reset();existing_count=2;assert(lithium_system_proxy_bootstrap(&id)==EEXIST&&object.refs==1&&!alloc_calls);
    puts("PASS persona ambiguous existing identities rejected");
    reset();find_error=EINVAL;assert(lithium_system_proxy_bootstrap(&id)==EINVAL&&!alloc_calls);
    puts("PASS persona query error does not allocate a replacement");
    reset();alloc_error=ENOMEM;assert(lithium_system_proxy_bootstrap(&id)==ENOMEM&&object.refs==1&&alloc_calls==1);
    puts("PASS persona allocation failure propagates");
    reset();begin_error=EEXIST;assert(lithium_system_proxy_bootstrap(&id)==EEXIST&&object.refs==0&&!object.listed);
    puts("PASS persona initialization failure releases unlisted object");
    reset();lookup_error=1;assert(lithium_system_proxy_bootstrap(&id)==ESRCH&&object.refs==1&&object.listed);
    puts("PASS persona failed validation keeps only list-owned reference");
    reset();wrong_type=1;assert(lithium_system_proxy_bootstrap(&id)==EINVAL&&object.refs==1);
    puts("PASS persona incorrect resulting type rejected and lookup reference released");
    return 0;
}
'''
    checks=[]
    for name,source,args in [('xml',head+copy+'\n'+xml+main_xml,[str(out/'original-springboard-xml.bin')]),('persona',phead+persona+pmain,[])]:
        src=out/(name+'.c');src.write_text(source);exe=out/name
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe),*args],capture_output=True,text=True,check=True)
        checks+=result.stdout.splitlines()
    lr=(work/'xnu/files/osfmk/arm64/loader_return.c').read_text()
    at=lr.index('static uint32_t lithium_identity_xml_probes,')
    end=lr.index('\n}\n',lr.index('lithium_ui_save(void)\n{',at))+3
    retained=lr[at:end]
    log_head='''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#define os_atomic_store(p,v,o) (*(p)=(v))
#define os_atomic_load(p,o) (*(p))
#define os_atomic_inc(p,o) (++*(p))
static uint32_t lithium_ui_layers,lithium_ui_backboard,lithium_ui_sbqueue,lithium_ui_svc,lithium_ui_surf,lithium_ui_extensions;
static char output[2048];
static void loader_log_line(const char *s) {assert(strlen(output)+strlen(s)<sizeof(output));strcat(output,s);}
'''
    log_main='''
int main(void) {
    lithium_ui_save();assert(strstr(output,"sb_xml_probes=0 copies=0 bytes=0 rc=-1"));
    assert(strstr(output,"persona_boot_rc=-1 id=4294967295 find_calls=0 find_rc=-1 count=0"));
    puts("PASS retained identity summary distinguishes unobserved calls from success");
    output[0]=0;lithium_identity_xml_observed(11,1234,ERANGE,1);lithium_identity_xml_observed(11,1234,0,0);
    lithium_identity_persona_bootstrap_observed(501,0);lithium_identity_persona_find_observed(0,1);lithium_ui_save();
    assert(strstr(output,"sb_xml_probes=1 copies=1 bytes=1234 rc=0"));
    assert(strstr(output,"persona_boot_rc=0 id=501 find_calls=1 find_rc=0 count=1"));
    puts("PASS actual kernel observers preserve real XML and persona results in final summary");
    return 0;
}
'''
    src=out/'retained.c';src.write_text(log_head+retained+log_main)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(src),'-o',str(out/'retained')],check=True)
    result=subprocess.check_output([str(out/'retained')],text=True)
    checks += [s for s in result.splitlines() if s.startswith('PASS ')]
    assert len(checks)==17 and all(s.startswith('PASS ') for s in checks)
    assert 'error = EBADEXEC;' in proc and 'token.val[7] != uidversion' in proc
    assert original['CONFIG_PERSONAS_enabled']
    hakios.write(hakios.ROOT/'reports/hakios-ios14-identity-tests.json',dict(inputs=hakios.inputs(spec),
        checks=checks,passed=True,sanitizers='address,undefined',actual_production_helpers_exercised=True,
        original_SpringBoard_signature_and_XML_checked=True,
        audit_token_validation_preserved=True,original_Apple_binaries_modified=False,
        sources={n:hakios.sha(tree/n) for n in review['before_files']},
        retained_source_sha256=hakios.sha(work/'xnu/files/osfmk/arm64/loader_return.c'),
        adapters={n:hakios.sha(port/'compatibility'/n) for n in ['ios14-xml-entitlements.c','ios14-system-proxy.c']},
        patch_sha256=hakios.sha(port/'compatibility/ios14-identity.patch'),
        test_source_sha256=hakios.sha(Path(__file__)),hardware_verified=False))
    print('\n'.join(checks))


if __name__=='__main__':main()
