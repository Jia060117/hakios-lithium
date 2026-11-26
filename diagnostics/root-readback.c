/* Minimal AArch64 Linux reader: no filesystem mount or block-write operation.
 * Runs as the recovery adbd shell. The init-only protect action sets BLKROSET.
 * Linux syscall ABI: include/uapi/asm-generic/unistd.h (ARM64).
 */
typedef unsigned long U;
static long sc(long n,long a,long b,long c,long d) {
 register long x8 __asm__("x8")=n, x0 __asm__("x0")=a;
 register long x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d;
 __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8),"r"(x1),"r"(x2),"r"(x3) : "memory");
 return x0;
}
static U len(const char*s){U n=0;while(s[n])n++;return n;}
static int eq(const char*a,const char*b){while(*a && *a==*b){a++;b++;}return *a==*b;}
static void die(int e){sc(93,e,0,0,0);for(;;);}
static void putfd(int fd,const char*s,U n){while(n){long k=sc(64,fd,(long)s,n,0);if(k<=0)die(70);s+=k;n-=k;}}
static void msg(const char*s){putfd(1,s,len(s));}
static long op(const char*p,long f,long m){return sc(56,-100,(long)p,f,m);}
static void closefd(long f){sc(57,f,0,0,0);}
static void number(U n){char b[24];int p=24;do{b[--p]='0'+n%10;n/=10;}while(n);putfd(1,b+p,24-p);}
static char buffer[1048576];
static void cat(const char*p){long f=op(p,0,0);msg(p);msg(":\n");if(f<0){msg("unavailable\n");return;}long n;while((n=sc(63,f,(long)buffer,sizeof(buffer),0))>0)putfd(1,buffer,n);closefd(f);msg("\n");}
static int content(const char*p,const char*want){char b[128];long f=op(p,0,0);if(f<0)return 0;long n=sc(63,f,(long)b,127,0);closefd(f);if(n<0)return 0;b[n]=0;return eq(b,want);}
static int valid(void){return content("/sys/class/block/sda15/partition","15\n") && content("/sys/class/block/sda15/start","786432\n") && content("/sys/class/block/sda15/size","489668344\n");}
static U decimal(const char**s){U n=0;int k=0;while(**s>='0'&&**s<='9'){if(n>17179869184UL)die(65);n=n*10+*(*s)++-'0';k++;}if(!k)die(65);return n;}
int run(U*sp){
 int argc=sp[0];char**argv=(char**)(sp+1);const char*c=argc==3&&eq(argv[1],"-c")?argv[2]:argc==2?argv[1]:"info";
 if(eq(c,"protect")){
  if(sc(175,0,0,0,0)!=0||!valid())return 66;
  const char*paths[]={"/dev/block/sda","/dev/block/sda15"};
  for(int i=0;i<2;i++){long f=op(paths[i],0,0);int one=1,ro=0;if(f<0||sc(29,f,0x125d,(long)&one,0)<0||sc(29,f,0x125e,(long)&ro,0)<0||ro!=1)return 67;closefd(f);}
  long f=op("/readback-protected",65,0444);if(f<0)return 68;putfd(f,"MIX1_READBACK_V1\n",17);closefd(f);return 0;
 }
 if(eq(c,"info")){
  msg("MIX1_READBACK_V1 uid=");number(sc(174,0,0,0,0));msg(" euid=");number(sc(175,0,0,0,0));msg("\n");
  const char*paths[]={"/proc/mounts","/proc/cmdline","/sys/class/block/sda15/uevent","/sys/class/block/sda15/start","/sys/class/block/sda15/size","/sys/class/block/sda/ro","/sys/class/block/sda15/ro","/readback-protected"};
  for(U i=0;i<sizeof(paths)/sizeof(paths[0]);i++)cat(paths[i]);return 0;
 }
 if(c[0]=='r'&&c[1]=='e'&&c[2]=='a'&&c[3]=='d'&&c[4]==' '){
  c+=5;U off=decimal(&c);if(*c++!=' ')return 65;U count=decimal(&c);
  if(*c||off>17179869184UL||count>17179869184UL-off||!valid()||!content("/readback-protected","MIX1_READBACK_V1\n"))return 69;
  long f=op("/dev/block/sda15",0,0);int ro=0;U size=0;
  if(f<0||sc(29,f,0x125e,(long)&ro,0)<0||ro!=1||sc(29,f,0x80081272,(long)&size,0)<0||size!=250710192128UL)return 71;
  if(sc(62,f,off,0,0)!=(long)off)return 72;
  while(count){U want=count>sizeof(buffer)?sizeof(buffer):count;long n=sc(63,f,(long)buffer,want,0);if(n<=0)return 73;putfd(1,buffer,n);count-=n;}
  closefd(f);return 0;
 }
 msg("Unsupported readback command\n");return 64;
}
__asm__(".global _start\n_start:\nmov x0,sp\nbl run\nmov x8,#93\nsvc #0\nb .\n");
