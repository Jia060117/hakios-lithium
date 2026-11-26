/* Read-only userdata/GPT capture under the exact stock Linux kernel.
 * This auxiliary image never mounts a block filesystem or writes a block.
 * It is not a HakiOS runtime or acceptance test. ADB gets only these commands.
 */
#include "ios16-storage-layout.h"
typedef unsigned long U;
static long sc(long n,long a,long b,long c,long d){
 register long x8 __asm__("x8")=n,x0 __asm__("x0")=a;
 register long x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d;
 __asm__ volatile("svc 0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3):"memory");return x0;
}
static U len(const char*s){U n=0;while(s[n])n++;return n;}
static int eq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static long op(const char*p){return sc(56,-100,(long)p,0,0);} // O_RDONLY, always
static void cl(long f){if(f>=0)sc(57,f,0,0,0);}
static int output(const void*p,U n){const char*s=p;while(n){long k=sc(64,1,(long)s,n,0);if(k<=0)return 0;s+=k;n-=k;}return 1;}
static void msg(const char*s){output(s,len(s));}
static unsigned char buffer[32768];
static int content(const char*p,const char*w){char b[128];long f=op(p);if(f<0)return 0;long n=sc(63,f,(long)b,127,0);cl(f);if(n<0)return 0;b[n]=0;return eq(b,w);}
static int valid(void){return content("/sys/class/block/sda15/partition","15\n")&&content("/sys/class/block/sda15/start","786432\n")&&content("/sys/class/block/sda15/size","489668344\n");}
static int rd(long f,U off,U n){if(n>sizeof(buffer)||sc(62,f,off,0,0)!=(long)off)return 0;U at=0;while(at<n){long k=sc(63,f,(long)buffer+at,n-at,0);if(k<=0)return 0;at+=k;}return 1;}
static int protect(void){const char*p[]={"/dev/block/sda","/dev/block/sda15"};int ro=1,ok=1;for(int i=0;i<2;i++){long f=op(p[i]);int got=0;if(f<0||sc(29,f,0x125d,(long)&ro,0)<0||sc(29,f,0x125e,(long)&got,0)<0||got!=1)ok=0;cl(f);}return ok;}
static void cat(const char*p){msg(p);msg(":\n");long f=op(p);if(f>=0){long n;while((n=sc(63,f,(long)buffer,sizeof(buffer),0))>0)output(buffer,n);}cl(f);msg("\n");}
static int geometry(long f){U bytes=0;int bs=0;return sc(29,f,0x80081272,(long)&bytes,0)==0&&bytes==250710192128UL&&sc(29,f,0x1268,(long)&bs,0)==0&&bs==4096;}
int run(U*stack){long argc=*stack;char**argv=(char**)(stack+1);const char*cmd=argc>1?argv[1]:"";if(eq(cmd,"-c"))cmd=argc>2?argv[2]:"";
 if(!valid())return 70;
 if(eq(cmd,"setup"))return sc(175,0,0,0,0)==0&&protect()?0:71;
 if(!content("/sys/class/block/sda/ro","1\n")||!content("/sys/class/block/sda15/ro","1\n"))return 72;
 if(eq(cmd,"info")){msg("MIX1_IOS16_STORAGE_READER_V1\n");const char*p[]={"/proc/mounts","/sys/class/block/sda15/uevent","/sys/class/block/sda15/start","/sys/class/block/sda15/size","/sys/class/block/sda/ro","/sys/class/block/sda15/ro"};for(U i=0;i<sizeof(p)/sizeof(p[0]);i++)cat(p[i]);return 0;}
 if(eq(cmd,"gpt")){long f=op("/dev/block/sda");int ok=f>=0&&rd(f,0,24576)&&output(buffer,24576);cl(f);return ok?0:73;}
 if(eq(cmd,"headers")){long f=op("/dev/block/sda15");if(f<0||!geometry(f)){cl(f);return 74;}const U offsets[]={0,SYSTEM_BYTES,SYSTEM_BYTES+OS_CRYPTEX_BYTES,DATA_OFFSET};for(U i=0;i<4;i++)if(!rd(f,offsets[i],8192)||!output(buffer,8192)){cl(f);return 75;}cl(f);return 0;}
 msg("UNSUPPORTED READ-ONLY COMMAND\n");return 64;
}
__asm__(".global _start\n_start:\nmov x0,sp\nbl run\nmov x8,#93\nsvc #0\nb .\n");
