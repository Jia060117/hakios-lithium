/* Fixed two-block HFSX maintenance, executed once by stock init as root.
 * This is not a HakiOS runtime or a general-purpose block writer.
 * Syscall numbers/ioctl ABI: Linux ARM64 asm-generic/unistd.h, fs.h.
 * Payload comes from complete HakiOS's integrated hfs-627.100.6 journal_create.
 */
#include "journal-payload.h"
typedef unsigned long U;
#ifdef MAINTENANCE_HOST_TEST
static long sc(long n,long a,long b,long c,long d);
#else
static long sc(long n,long a,long b,long c,long d) {
 register long x8 __asm__("x8")=n,x0 __asm__("x0")=a;
 register long x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d;
 __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8),"r"(x1),"r"(x2),"r"(x3) : "memory");return x0;
}
#endif
static U len(const char*s){U n=0;while(s[n])n++;return n;}
static int eq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int same(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;for(U i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
static long op(const char*p,long f,long m){return sc(56,-100,(long)p,f,m);}
static void cl(long f){if(f>=0)sc(57,f,0,0,0);}
static int wr(long f,const void*p,U n){const char*s=p;while(n){long k=sc(64,f,(long)s,n,0);if(k<=0)return 0;s+=k;n-=k;}return 1;}
static void msg(const char*s){wr(1,s,len(s));}
static unsigned char buffer[65536];
static int content(const char*p,const char*w){char b[128];long f=op(p,0,0);if(f<0)return 0;long n=sc(63,f,(long)b,127,0);cl(f);if(n<0)return 0;b[n]=0;return eq(b,w);}
static int valid(void){return content("/sys/class/block/sda15/partition","15\n")&&content("/sys/class/block/sda15/start","786432\n")&&content("/sys/class/block/sda15/size","489668344\n");}
static int rd(long f,U off,void*p,U n){char*s=p;if(sc(62,f,off,0,0)!=(long)off)return 0;while(n){long k=sc(63,f,(long)s,n,0);if(k<=0)return 0;s+=k;n-=k;}return 1;}
static int readonly(int ro){const char*paths[]={"/dev/block/sda","/dev/block/sda15"};int ok=1;for(int i=0;i<2;i++){long f=op(paths[i],0,0);int got=-1;if(f<0||sc(29,f,0x125d,(long)&ro,0)<0||sc(29,f,0x125e,(long)&got,0)<0||got!=ro)ok=0;cl(f);}return ok;}
static int nomounts(void){long f=op("/proc/mounts",0,0);if(f<0)return 0;long n=sc(63,f,(long)buffer,sizeof(buffer)-1,0);cl(f);if(n<=0||n==(long)sizeof(buffer)-1)return 0;buffer[n]=0;for(long i=0;i+5<=n;i++)if((i==0||buffer[i-1]=='\n')&&same(buffer+i,"/dev/",5))return 0;return 1;}
static int geometry(long f){U size=0;int block=0;return valid()&&sc(29,f,0x80081272,(long)&size,0)==0&&size==250710192128UL&&sc(29,f,0x1268,(long)&block,0)==0&&block==4096;}
static int writeblock(long f,U off,const void*p){return sc(62,f,off,0,0)==(long)off&&wr(f,p,4096)&&sc(82,f,0,0,0)==0&&rd(f,off,buffer,4096)&&same(p,buffer,4096);}
static int zerojournal(long f,int skipheader){U off=532480+(skipheader?4096:0),end=532480+8388608;while(off<end){U n=end-off;if(n>sizeof(buffer))n=sizeof(buffer);if(!rd(f,off,buffer,n))return 0;for(U i=0;i<n;i++)if(buffer[i])return 0;off+=n;}return 1;}
static int repair(long log){
 long f=-1;int rc=70,header_ready=0,jib_ready=0;
 #define LOG(s) do{if(!wr(log,s,sizeof(s)-1)){rc=79;goto done;}}while(0)
 LOG("MIX1_HAKIOS_JOURNAL_V1\n");
 if(sc(175,0,0,0,0)!=0||!valid()||!nomounts()){LOG("FAIL ROOT GEOMETRY OR MOUNT GUARD\n");goto done;}
 if(!readonly(1)){LOG("FAIL INITIAL PROTECTION\n");goto done;}
 f=op("/dev/block/sda15",0,0);
 if(f<0||!geometry(f)||!rd(f,0,buffer,8192)||!same(buffer,expected_volume,8192)){LOG("FAIL VOLUME OR DEVICE GUARD\n");goto done;}
 if(!rd(f,528384,buffer,4096)){LOG("FAIL JIB READ\n");goto done;}
 jib_ready=same(buffer,initialized_jib,4096);
 if(!jib_ready&&!same(buffer,expected_jib,4096)){LOG("FAIL JIB PREIMAGE\n");goto done;}
 if(!rd(f,532480,buffer,4096)){LOG("FAIL HEADER READ\n");goto done;}
 header_ready=same(buffer,initialized_header,4096);
 if(!header_ready){for(U i=0;i<4096;i++)if(buffer[i]){LOG("FAIL HEADER PREIMAGE\n");goto done;}}
 if((jib_ready&&!header_ready)||!zerojournal(f,header_ready)){LOG("FAIL JOURNAL HAS TRANSACTIONS OR INVALID ORDER\n");goto done;}
 LOG("LIVE GEOMETRY VOLUME JIB AND EMPTY JOURNAL VERIFIED\n");
 cl(f);f=-1;
 if(!header_ready||!jib_ready){
  if(!readonly(0)){LOG("FAIL WRITE ENABLE\n");goto done;}
  f=op("/dev/block/sda15",2,0);
  if(f<0||!geometry(f)){LOG("FAIL WRITE DEVICE\n");goto done;}
  if(!header_ready&&!writeblock(f,532480,initialized_header)){LOG("FAIL HEADER WRITE SYNC OR READBACK\n");goto done;}
  LOG("JOURNAL HEADER SYNC AND READBACK PASS\n");
  if(!jib_ready&&!writeblock(f,528384,initialized_jib)){LOG("FAIL JIB WRITE SYNC OR READBACK\n");goto done;}
  LOG("JIB NEEDINIT CLEARED SYNC AND READBACK PASS\n");
  cl(f);f=-1;
 }else{LOG("ALREADY INITIALIZED NO WRITES\n");}
 if(!readonly(1)){LOG("FAIL FINAL PROTECTION\n");goto done;}
 f=op("/dev/block/sda15",0,0);
 if(f<0||!rd(f,0,buffer,8192)||!same(buffer,expected_volume,8192)||!rd(f,528384,buffer,4096)||!same(buffer,initialized_jib,4096)||!rd(f,532480,buffer,4096)||!same(buffer,initialized_header,4096)){LOG("FAIL FINAL READBACK\n");goto done;}
 LOG("PASS INITIALIZED EMPTY HFSX JOURNAL READONLY RESTORED\n");rc=0;
done:
 cl(f);
 if(!readonly(1)){wr(log,"FAIL CLEANUP PROTECTION\n",24);rc=78;}
 return rc;
 #undef LOG
}
static void cat(const char*p){long f=op(p,0,0);msg(p);msg(":\n");if(f<0){msg("UNAVAILABLE\n");return;}long n;while((n=sc(63,f,(long)buffer,sizeof(buffer),0))>0)wr(1,buffer,n);cl(f);msg("\n");}
int run(U*sp){int argc=sp[0];char**argv=(char**)(sp+1);const char*c=argc==3&&eq(argv[1],"-c")?argv[2]:argc==2?argv[1]:"info";
 if(eq(c,"repair")){if(sc(175,0,0,0,0)!=0)return 66;long log=op("/journal-repair-result",577,0444);if(log<0)return 67;int rc=repair(log);cl(log);return rc;}
 if(eq(c,"info")){msg("MIX1_HAKIOS_JOURNAL_V1\n");const char*p[]={"/journal-repair-result","/proc/mounts","/proc/cmdline","/sys/class/block/sda15/uevent","/sys/class/block/sda15/start","/sys/class/block/sda15/size","/sys/class/block/sda/ro","/sys/class/block/sda15/ro"};for(U i=0;i<sizeof(p)/sizeof(p[0]);i++)cat(p[i]);return 0;}
 if(eq(c,"result")){cat("/journal-repair-result");return 0;}
 if(eq(c,"metadata")){if(!valid()||!content("/sys/class/block/sda15/ro","1\n"))return 69;long f=op("/dev/block/sda15",0,0);if(f<0||!geometry(f))return 71;int ok=rd(f,0,buffer,8192)&&wr(1,buffer,8192)&&rd(f,528384,buffer,8192)&&wr(1,buffer,8192);cl(f);return ok?0:72;}
 msg("UNSUPPORTED MAINTENANCE COMMAND\n");return 64;
}
#ifndef MAINTENANCE_HOST_TEST
__asm__(".global _start\n_start:\nmov x0,sp\nbl run\nmov x8,#93\nsvc #0\nb .\n");
#endif
