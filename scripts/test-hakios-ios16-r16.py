"""Exercise the production profile/DT branch, retaining the complete R15 bodies."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import hakios
import hakios_r16 as r16
import hakios_r17
import hakios_r18

SDK = r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
static std::string output;
static void IOLog(const char *format, ...) {
 char line[2048];va_list ap;va_start(ap,format);int n=vsnprintf(line,sizeof(line),format,ap);va_end(ap);
 assert(n>=0 && (size_t)n<sizeof(line));output+=line;
}
struct OSData {
 std::vector<uint8_t> bytes;
 unsigned getLength(){return bytes.size();}
 const void *getBytesNoCopy(){return bytes.data();}
};
struct IORegistryEntry {
 static std::map<std::string,IORegistryEntry> nodes;
 std::map<std::string,OSData> properties;
 static IORegistryEntry *fromPath(const char *path,int){auto i=nodes.find(path);return i==nodes.end()?nullptr:&i->second;}
 OSData *getProperty(const char *key){auto i=properties.find(key);return i==properties.end()?nullptr:&i->second;}
 bool setProperty(const char *key,const void *p,size_t n){auto b=(const uint8_t*)p;properties[key].bytes.assign(b,b+n);return true;}
 void release(){}
};
std::map<std::string,IORegistryEntry> IORegistryEntry::nodes;
static const int gIODTPlane=0;
#define OSDynamicCast(type, object) static_cast<type*>(object)
#define LITHIUM_ARMV8_0 1
#define kFBDefaultScale 3
#define kFBPointsWide 375
#define kFBPointsHigh 812
static uint32_t gFBWidth,gFBHeight,gFBScale,gFBSubtype;
static uint32_t fWidth,fHeight,fPanelWidth=1080,fPanelHeight=2040,fRowBytes=3240,fPixelBytes=3;
'''
TEST = r'''
static void reset(uint32_t w,uint32_t h,uint32_t scale,uint32_t subtype) {
 output.clear();IORegistryEntry::nodes.clear();
 uint32_t a=3,b=2436;
 IORegistryEntry::nodes["/chosen"].setProperty("display-scale",&a,4);
 IORegistryEntry::nodes["/product"].setProperty("artwork-scale-factor",&a,4);
 IORegistryEntry::nodes["/product"].setProperty("artwork-device-subtype",&b,4);
 gFBWidth=w;gFBHeight=h;gFBScale=scale;gFBSubtype=subtype;
}
static uint32_t value(const char *path,const char *key) {
 uint32_t result=0;assert(lithium_display_dt_u32(path,key,&result));return result;
}
int main() {
 reset(750,1334,0,0);apply();
 assert(fWidth==750 && fHeight==1334 && fRowBytes==3240 && fPixelBytes==3);
 assert(value("/chosen","display-scale")==2 && value("/product","artwork-scale-factor")==2);
 assert(value("/product","artwork-device-subtype")==1334);
 assert(output.find("phase=before chosen_scale=3 artwork_scale=3 subtype=2436")!=std::string::npos);
 assert(output.find("phase=after chosen_scale=2 artwork_scale=2 subtype=1334")!=std::string::npos);
 assert(output.find("panel_stride=3240 panel_bpp=24 auto_profile=1")!=std::string::npos);
 reset(1125,2436,0,0);apply();assert(fWidth==1125 && fHeight==2436 && value("/chosen","display-scale")==3);
 reset(750,1334,2,0);apply();assert(fWidth==750 && fHeight==1624); // original explicit-scale behavior
 reset(828,1792,2,1792);apply();assert(fWidth==828 && fHeight==1792 && value("/product","artwork-device-subtype")==1792);
 reset(750,1334,3,1334);apply();assert(value("/chosen","display-scale")==3 && value("/product","artwork-device-subtype")==2436);
 reset(0,0,0,0);apply();assert(fWidth==1080 && fHeight==2040 && value("/chosen","display-scale")==3);
 reset(750,1334,0,0);IORegistryEntry::nodes.erase("/chosen");apply();assert(output.find("valid=0/1/1")!=std::string::npos);
 reset(750,1334,0,0);IORegistryEntry::nodes["/chosen"].properties["display-scale"].bytes={1};
 lithium_display_dt_log("malformed");assert(output.find("valid=0/1/1")!=std::string::npos);
 puts("PASS actual display initialization: coherent default, explicit upstream profiles, original geometry, DT before/after and missing/malformed properties");
}
'''


def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    port=hakios.ROOT/spec['port_path'];contract=hakios.read(port/'diagnostics/r16.json')
    sb_source=work/'userland/src/inject_sb.c'
    sb_text=hakios_r18.historical_text(work,'userland/src/inject_sb.c',sb_source.read_text())
    if (work/'devices/lithium/diagnostics/r17.json').exists():
        r17=hakios.read(work/'devices/lithium/diagnostics/r17.json')
        hakios.require(hashlib.sha256(sb_text.encode()).hexdigest()==r17['after_sha256']['userland/src/inject_sb.c'],
            'R17 SpringBoard source differs from its exact contract')
        with tempfile.TemporaryDirectory(prefix='r16-source-reconstruct-') as temp:
            base=Path(temp);target=base/'userland/src/inject_sb.c';target.parent.mkdir(parents=True,exist_ok=True)
            target.write_text(sb_text,encoding='utf-8')
            subprocess.run(['git','apply','--reverse',str(port/'patches/0017-lithium-native-display-profile.patch')],cwd=base,check=True)
            sb_text=target.read_text()
    for name,digest in contract['after_sha256'].items():
        actual=hashlib.sha256(sb_text.encode()).hexdigest() if name=='userland/src/inject_sb.c' else hakios.sha(work/name)
        hakios.require(actual==digest,'R16 exact source differs: '+name)
    # Reverse the actual patch and compare every byte to the backed-up R15.
    with tempfile.TemporaryDirectory(prefix='r16-r15-preservation-') as temp:
        base=Path(temp)
        for name in contract['after_sha256']:
            p=base/name;p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(sb_text.encode() if name=='userland/src/inject_sb.c' else (work/name).read_bytes())
        subprocess.run(['git','apply','--reverse',str(port/'patches/0016-lithium-display-profile.patch')],cwd=base,check=True)
        for name,digest in contract['before_sha256'].items():
            hakios.require(hakios.sha(base/name)==digest,'Original R15 body differs: '+name)
    fb=(work/'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp').read_text()
    start=fb.index('\tfWidth = gFBWidth ?');end=fb.index('\tfColumn = IONewZero',start)
    body=fb[start:end]
    folder=work/'build/r16-host-checks';folder.mkdir(parents=True,exist_ok=True)
    src=folder/'display.cpp';src.write_text(SDK+'\n#include "display-kernel.inc"\nstatic void apply(){\n'+body+'}\n'+TEST,encoding='utf-8')
    subprocess.run(['clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I',str(port/'diagnostics'),str(src),'-o',str(folder/'display')],check=True)
    result=subprocess.run([str(folder/'display')],check=True,capture_output=True,text=True);print(result.stdout,end='')
    sb=sb_text
    # Constructor only gates a fallback by registry values; UIKit queries are deferred.
    hakios.require('if (lithium_display_native_statusbar()) return;' in sb,'Native fallback guard missing')
    body=(port/'diagnostics/display-userspace.inc').read_text()
    before=body[:body.index('static bool\nlithium_display_selector')]
    hakios.require('mainScreen' not in before,'Early status-bar profile gate must not touch UIScreen')
    for key in ('ScreenDimensions','main-screen-width','main-screen-height','main-screen-scale','main-screen-class','ArtworkDeviceSubType'):
        hakios.require('"'+key+'"' in body,'Missing actual MG measurement: '+key)
    prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')['cmdline']
    hakios.require(r16.command_line(prior)==contract['payload_boot_args']==prior,'R15 payload arguments changed')
    paths=[Path(__file__),Path(r16.__file__)]+[port/n for n in sorted(r16.PORT_DELTA)]+[work/n for n in contract['after_sha256']]
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r16-checks.json',dict(passed=True,revision=16,
        inputs=hakios.inputs(spec),actual_production_functions=True,sanitizers=['ASan','UBSan'],
        stdout=result.stdout,r15_source_reconstructed=True,boot_args_unchanged=True,
        compositor_unchanged=True,hardware_verified=False,phone_operations=False,
        evidence_sha256={str(p):hakios.sha(p) for p in paths}))


if __name__=='__main__':main()
