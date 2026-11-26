"""Test R17's exact upstream status-bar profile gate and R16 preservation."""
from pathlib import Path
import subprocess
import hakios
import hakios_r17
import hakios_r18


SDK = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
static uint32_t scale_value, subtype_value;
static bool chosen_valid=true, product_valid=true;
static void say(const char *, ...) {}
static bool lithium_display_user_dt_u32(const char *path,const char *key,uint32_t *value) {
 if(!strcmp(path,"IODeviceTree:/chosen") && !strcmp(key,"display-scale")) { if(!chosen_valid)return false; *value=scale_value; return true; }
 if(!strcmp(path,"IODeviceTree:/product") && !strcmp(key,"artwork-device-subtype")) { if(!product_valid)return false; *value=subtype_value; return true; }
 return false;
}
'''
TEST = r'''
int main(){
 scale_value=3;subtype_value=2436;assert(lithium_display_native_statusbar());
 scale_value=2;subtype_value=1334;assert(!lithium_display_native_statusbar());
 scale_value=3;subtype_value=1792;assert(!lithium_display_native_statusbar());
 chosen_valid=false;assert(!lithium_display_native_statusbar());
 chosen_valid=true;product_valid=false;assert(!lithium_display_native_statusbar());
 puts("PASS R17 native status-bar gate: D22AP 3x/2436 accepted; R16 2x/1334 and invalid/missing profiles rejected");
}
'''


def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    port=hakios.ROOT/spec['port_path'];contract=hakios.read(port/'diagnostics/r17.json')
    for name,digest in contract['after_sha256'].items():
        hakios.require(__import__('hashlib').sha256(hakios_r18.historical_text(work,name,(work/name).read_text()).encode()).hexdigest()==digest,'R17 exact source differs: '+name)
    with __import__('tempfile').TemporaryDirectory(prefix='r17-r16-preservation-') as temp:
        base=Path(temp);name=next(iter(contract['after_sha256']))
        target=base/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(hakios_r18.historical_text(work,name,(work/name).read_text()),encoding='utf-8')
        subprocess.run(['git','apply','--reverse',str(port/contract['patch'])],cwd=base,check=True)
        hakios.require(hakios.sha(target)==contract['before_sha256'][name],'R17 reverse patch did not restore R16')
    hakios.require(hakios.sha(work/'devices/lithium/diagnostics/display-userspace-r17.inc') ==
        contract['after_sha256']['devices/lithium/diagnostics/display-userspace-r17.inc'],
        'R17 native profile include differs')
    hakios.require(hakios.sha(work/'devices/lithium/diagnostics/display-userspace.inc') ==
        contract['before_sha256']['devices/lithium/diagnostics/display-userspace.inc'],
        'R16 display include was overwritten')
    overlay=(work/'devices/lithium/diagnostics/display-userspace-r17.inc').read_text(encoding='utf-8')
    start=overlay.index('static bool\nlithium_display_native_statusbar(void)')
    end=overlay.index('\nstatic bool\nlithium_display_selector',start)
    function=overlay[start:end]
    folder=work/'build/r17-host-checks';folder.mkdir(parents=True,exist_ok=True)
    src=folder/'statusbar.cpp';binary=folder/'statusbar'
    src.write_text(SDK+'\n'+function+'\n'+TEST,encoding='utf-8')
    subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],check=True,capture_output=True,text=True);print(result.stdout,end='')
    r16=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r16/contract.json')
    hakios.require(hakios_r17.command_line(r16['cmdline'])==contract['payload_boot_args'],'R17 command line contract mismatch')
    hakios.require(contract['payload_from_r16']==r16['cmdline'],'R16 payload source changed')
    paths=[Path(__file__),Path(hakios_r17.__file__),port/'patches/0017-lithium-native-display-profile.patch',
        port/'diagnostics/r17.json',port/'diagnostics/display-userspace-r17.inc',
        work/'devices/lithium/diagnostics/display-userspace.inc',work/'devices/lithium/diagnostics/display-userspace-r17.inc']
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r17-checks.json',dict(passed=True,revision=17,
        inputs=hakios.inputs(spec),actual_statusbar_function=True,sanitizers=['ASan','UBSan'],stdout=result.stdout,
        r16_source_reconstructed=True,only_render_boot_args_changed=True,hardware_verified=False,phone_operations=False,
        evidence_sha256={str(p):hakios.sha(p) for p in paths}))


if __name__=='__main__':main()
