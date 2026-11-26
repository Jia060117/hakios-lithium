#!/usr/bin/env python3
"""Run the actual fixed-command reader with simulated Linux syscalls under ASan/UBSan."""
from pathlib import Path
import subprocess
import hakios

TEST=r'''
static long uid=2000;static int ro=1,bad_geometry,opened,block_reads,protects;
static U offsets[4],out_bytes;static char output_data[65536];
static const char* paths[32];static U positions[32];static int nextfd=3;
static long sc(long n,long a,long b,long c,long d){
 if(n==175)return uid;
 if(n==56){assert(a==-100&&c==0&&d==0);assert(nextfd<32);paths[nextfd]=(const char*)b;positions[nextfd]=0;opened++;return nextfd++;}
 if(n==57)return 0;
 if(n==64){assert(a==1&&out_bytes+(U)c<=sizeof(output_data));memcpy(output_data+out_bytes,(void*)b,c);out_bytes+=c;return c;}
 assert(a>=3&&a<nextfd);const char*p=paths[a];
 if(n==62){assert(c==0);positions[a]=b;return b;}
 if(n==29){
  if(b==0x125d){assert(uid==0&&*(int*)c==1);protects++;ro=1;return 0;}
  if(b==0x125e){*(int*)c=ro;return 0;}
  assert(!strcmp(p,"/dev/block/sda15"));
  if(b==0x80081272){*(U*)c=250710192128UL;return 0;}
  if(b==0x1268){*(int*)c=4096;return 0;}
  assert(0);
 }
 if(n==63){
  const char*text="\n";
  if(!strcmp(p,"/dev/block/sda")){assert(positions[a]==0&&c==24576);memset((void*)b,0x41,c);block_reads++;positions[a]+=c;return c;}
  if(!strcmp(p,"/dev/block/sda15")){assert(c==8192&&block_reads<4);offsets[block_reads++]=positions[a];memset((void*)b,0x42,c);positions[a]+=c;return c;}
  if(!strcmp(p,"/sys/class/block/sda15/partition"))text=bad_geometry?"14\n":"15\n";
  else if(!strcmp(p,"/sys/class/block/sda15/start"))text="786432\n";
  else if(!strcmp(p,"/sys/class/block/sda15/size"))text="489668344\n";
  else if(strstr(p,"/ro"))text=ro?"1\n":"0\n";
  U size=strlen(text);if(positions[a]>=size)return 0;
  U count=size-positions[a];if(count>(U)c)count=c;memcpy((void*)b,text+positions[a],count);positions[a]+=count;return count;
 }
 assert(0);return -1;
}
static int invoke(const char*command){
 U stack[4]={3,(U)"/system/bin/sh",(U)"-c",(U)command};
 out_bytes=0;block_reads=0;opened=0;nextfd=3;return run(stack);
}
int main(void){
 assert(invoke("setup")==71&&protects==0); // shell must not set block flags
 assert(invoke("info")==0&&out_bytes>0&&!memcmp(output_data,"MIX1_IOS16_STORAGE_READER_V1\n",28));
 assert(invoke("gpt")==0&&out_bytes==24576&&block_reads==1);
 assert(invoke("headers")==0&&out_bytes==32768&&block_reads==4);
 assert(offsets[0]==0&&offsets[1]==SYSTEM_BYTES&&offsets[2]==SYSTEM_BYTES+OS_CRYPTEX_BYTES&&offsets[3]==DATA_OFFSET);
 assert(invoke("arbitrary-command")==64&&block_reads==0);
 ro=0;assert(invoke("gpt")==72&&block_reads==0);
 uid=0;assert(invoke("setup")==0&&protects==2&&ro==1);
 bad_geometry=1;assert(invoke("headers")==70&&block_reads==0);assert(invoke("setup")==70&&protects==2);
 puts("PASS: actual reader shell read access, root-only protection, geometry/RO guards and fixed bounded samples");
}
'''


def main():
    source=hakios.ROOT/'maintenance/hakios-lithium/ios16-storage-reader.c'
    text=source.read_text();start=text.index('static long sc(');end=text.index('static U len(')
    text=text[:start]+'static long sc(long,long,long,long,long);\n'+text[end:]
    text=text[:text.index('__asm__(".global _start')]
    output=hakios.ROOT/'artifacts/hakios/lithium-ios16/storage-reader'
    host=output/'reader-host.c';host.write_text('#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n'+text+TEST)
    binary=output/'reader-host'
    subprocess.run(['clang','-O1','-g','-fno-strict-aliasing','-fsanitize=address,undefined',str(host),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True)
    hakios.require(result.returncode==0,'Actual reader host checks failed: '+result.stdout+result.stderr)
    hakios.write(hakios.ROOT/'reports/hakios-ios16-storage-reader-host-checks.json',dict(passed=True,
        source_sha256=hakios.sha(source),test_sha256=hakios.sha(Path(__file__)),asan_ubsan_passed=True,
        shell_read_access_verified=True,root_only_block_protection_verified=True,bounded_read_commands_verified=True,
        device_operations=False,hardware_verified=False))
    print(result.stdout.strip())


if __name__=='__main__':main()
