from pathlib import Path
import hashlib, json, subprocess, sys
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
out = root / 'evidence/cpu-r40-20261123/profile-fix'
port = root / 'ports/hakios-lithium/return-fastboot'
log = root / 'reports/local/r40-attempts/r40-ui-614-01/console.log'
headers = ('ufs-profile.h', 'ufs-return-profile.h')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
before = {n: sha(port/n) for n in headers}
lines = [(i, s.split(b'boot-args = ', 1)[1]) for i, s in enumerate(log.read_bytes().splitlines(), 1) if b'boot-args = ' in s]
assert len(lines) == 1
line, actual = lines[0]
mailbox = b'ufs_shared_pa=0x000000004085e000'
assert actual.split().count(mailbox) == 1
(out/'actual-cmdline.txt').write_bytes(actual)
cases = [('actual_bootargs', actual, 1), ('duplicate_same', actual+b' '+mailbox, 0), ('duplicate_other', actual+b' ufs_shared_pa=0x0000000040860000', 0)]
for name, token in [('bare',b'ufs_shared_pa'),('empty',b'ufs_shared_pa='),('zero',b'ufs_shared_pa=0x0000000000000000'),('short',b'ufs_shared_pa=0x4085e000'),('long',b'ufs_shared_pa=0x0000000004085e000'),('uppercase',b'ufs_shared_pa=0x000000004085E000'),('prefix',b'ufs_shared_pa=0X000000004085e000'),('unaligned',b'ufs_shared_pa=0x000000004085f000'),('badhex',b'ufs_shared_pa=0x000000004085g000'),('suffix',b'ufs_shared_pa=0x000000004085e000x'),('quoted_value',b'ufs_shared_pa="0x000000004085e000"')]:
    cases.append((name, actual.replace(mailbox,token), 0))
for name, token in [('unknown_ufs',b'ufs_unknown=1'),('mailbox_key_suffix',b'ufs_shared_pa_extra=0x000000004085e000')]:
    cases.append((name,actual+b' '+token,0))
# Loader seeds before injecting this token: preserve that existing admission.
cases.append(('loader_before_injection',actual.replace(mailbox+b' ',b''),1))
source = ['#include <stdio.h>', '#include <stdlib.h>', '#include <string.h>', '#include "ufs-return-profile.h"', 'int main(void) {', 'unsigned tests=0;']
for name,data,expected in cases:
    # Exact-size heap allocation makes ASan check bounded, non-NUL-terminated input.
    values=','.join(str(v) for v in data)
    source.extend(['{',f'const unsigned char bytes[] = {{{values}}};', 'char *p = (char *)malloc(sizeof bytes);', 'if (!p) return 2;', 'memcpy(p,bytes,sizeof bytes);', f'int a=rfbt_ufs_profile(p,sizeof bytes,1), b=rfbt_ufs_return_profile(p,sizeof bytes,1);', f'if (a!={expected} || b!={expected}) {{ fprintf(stderr,"FAIL {name} generic=%d return=%d expected={expected}\\n",a,b); free(p); return 1; }}', 'free(p); tests+=2;', f'puts("PASS {name}");','}'])
source.extend(['printf("SUMMARY cases=%u assertions=%u\\n", tests/2,tests);','return 0;','}'])
(out/'focused.c').write_text('\n'.join(source)+'\n',encoding='ascii')
def linux(p):
    p=str(p.resolve()); return '/mnt/'+p[0].lower()+p[2:].replace('\\','/')
def run(argv):
    return subprocess.run(['wsl.exe','-d','Ubuntu','-u','jia','--exec',*argv],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=60)
command=['/usr/bin/clang','-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',linux(port),linux(out/'focused.c'),'-o',linux(out/'focused')]
built=run(command)
(out/'clang-build.log').write_text(json.dumps(command)+'\nexit='+str(built.returncode)+'\n'+built.stdout,encoding='utf-8')
assert built.returncode==0,built.stdout
executed=run(['/usr/bin/env','ASAN_OPTIONS=detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1',linux(out/'focused')])
(out/'asan-ubsan-output.txt').write_text(executed.stdout,encoding='utf-8')
print(executed.stdout,flush=True)
assert executed.returncode==0,executed.stdout
after={n:sha(port/n) for n in headers}
assert before==after,'Production headers changed during test'
result={'status':'passed','scope':'minimal actual R40 bootargs / duplicate / malformed / unknown ufs tokens','cases':len(cases),'assertions':2*len(cases),'sanitizers':['address','undefined'],'console_line':line,'console_sha256':sha(log),'actual_cmdline_sha256':hashlib.sha256(actual).hexdigest(),'production_frozen':True,'production_sha256':after,'original_sha256':{n:sha(out/'original'/n) for n in headers},'device_operations':0,'hardware_auto_return_verified':False}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
(out/'result.md').write_text('# R40 mailbox profile focused test\n\nPASS: '+str(len(cases))+' cases / '+str(2*len(cases))+' assertions, Clang ASan+UBSan.\n\nExact captured bootargs at console.log line '+str(line)+' accepted by both production profiles. Identical and conflicting duplicate mailbox tokens, malformed/noncanonical/zero/unaligned values, unknown ufs_* and mailbox-key suffixes rejected. Loader admission without the injected mailbox remains accepted.\n\nSource contract: upstream/hakios-xnu-kernel/xnuloader/loader.c:1618 aligns block_shared to 0x2000; lines 3150-3162 emit ufs_shared_pa=0x followed by exactly 16 lowercase hex digits. The actual prepared loader source has matching alignment at line 1815 and emission starting line 3436. Production header hashes unchanged throughout test. Original headers preserved in original/.\n\nHost argument admission only; hardware UFS finalize/automatic Fastboot return remains unverified. No device operations.\n',encoding='utf-8')
print(json.dumps(result),flush=True)
