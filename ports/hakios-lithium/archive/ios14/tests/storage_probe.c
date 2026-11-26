/*
 * One-shot read-only test of complete HakiOS UFS/HFS integration on MIX.
 * Uses the complete upstream libmini syscall entry and HFS mount arguments.
 * Enters through the complete upstream libmini startup, which aligns SP before C.
 * This is a diagnostic PID1, not an iOS launchd or a replacement service.
 */
#include "storage_expected.h"
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
static long sys6(long n,long a,long b,long c,long d,long e,long f) {
  /* The pinned mini.h prototype; start.S already supplies this entry.
   * A normal C call handles x1 and all other caller-saved registers.
   * Keep this diagnostic's negative-errno convention at its boundary. */
  extern long mini_syscall(long,long,long,long,long,long,long);
  extern int errno;
  long result=mini_syscall(n,a,b,c,d,e,f);
  return result==-1 ? -errno : result;
}
#define sys3(n,a,b,c) sys6(n,(long)(a),(long)(b),(long)(c),0,0,0)
enum { SYS_read=3,SYS_write=4,SYS_open=5,SYS_close=6,SYS_select=93,SYS_unmount=159,
  SYS_mount=167,SYS_statfs64=345 };
static long console=-1;
static char line[256], buffer[65536];
static unsigned used;
static void append(const char *s) { while (*s && used+1<sizeof(line)) line[used++]=*s++; }
static void number(u64 n,unsigned base) {
  char b[32]; unsigned i=sizeof(b); do { b[--i]="0123456789abcdef"[n%base]; n/=base; } while (n);
  while (i<sizeof(b) && used+1<sizeof(line)) line[used++]=b[i++];
}
static void begin(const char *s) { used=0; append("LITHIUM P2 "); append(s); }
static void end(void) { append("\n"); sys3(SYS_write,console,line,used); }
static void status(const char *s,long result) {
  begin(s); append(" rc="); if (result<0) { append("-"); number(-result,10); } else number(result,10); end();
}
static u64 hash(u64 h,const char *p,unsigned n) {
  for (unsigned i=0;i<n;i++) h=(h^(unsigned char)p[i])*0x100000001b3UL;
  return h;
}
static int check_file(const char *label,const char *path,u64 expected_bytes,u64 expected_fnv) {
  long fd=sys3(SYS_open,path,0,0), got=0, r=-1;
  u64 h=0xcbf29ce484222325UL;
  if (fd>=0) {
    while ((r=sys3(SYS_read,fd,buffer,sizeof(buffer)))>0) {
      h=hash(h,buffer,(unsigned)r); got+=r;
      if ((u64)got>expected_bytes) break;
    }
    sys3(SYS_close,fd,0,0);
  }
  int match=fd>=0 && r==0 && (u64)got==expected_bytes && h==expected_fnv;
  begin(label); append(" bytes="); number(got,10); append(" match="); number(match,10);
  append(" fnv="); number(h,16); end();
  return match;
}
struct hfs_args {
  const char *fspec; u32 uid,gid; u16 mask,pad; u32 encoding; int tz[2];
  int flags,journal_tbuffer_size,journal_flags,journal_disable;
};
static int run(void) {
  char sfs[2168];
  /* HakiOS publishes a VBLK node for mounts and a raw VCHR node for reads. */
  struct hfs_args a={.fspec="/dev/ufs_userdata"};
  long fd=sys3(SYS_open,"/dev/rufs_userdata",0,0), n=fd<0?fd:sys3(SYS_read,fd,buffer,8192);
  if (fd>=0) sys3(SYS_close,fd,0,0);
  int header=n==8192 && hash(0xcbf29ce484222325UL,buffer,8192)==EXPECTED_HEADER_FNV;
  begin("HEADER"); append(" read="); if (n<0) append("-"); number(n<0?-n:n,10);
  append(" match="); number(header,10); end();
  if (!header) return 1;
  long r=sys6(SYS_mount,(long)"hfs",(long)"/mnt",1,(long)&a,0,0);
  status("MOUNT",r);
  if (r<0) return 2;
  r=sys3(SYS_statfs64,"/mnt",sfs,0);
  int readonly=r==0 && (*(u32 *)(sfs+64)&1) && sfs[72]=='h' && sfs[73]=='f' && sfs[74]=='s';
  if (!readonly) { status("MOUNT READONLY CHECK",r<0?r:-1); return 3; }
  begin("MOUNT"); append(" rc=0 readonly=1"); end();
  int version=check_file("VERSION","/mnt/System/Library/CoreServices/SystemVersion.plist",
    EXPECTED_VERSION_BYTES,EXPECTED_VERSION_FNV);
  int dyld=check_file("DYLD","/mnt/usr/lib/dyld",EXPECTED_DYLD_BYTES,EXPECTED_DYLD_FNV);
  int launchd=check_file("LAUNCHD","/mnt/sbin/launchd",EXPECTED_LAUNCHD_BYTES,EXPECTED_LAUNCHD_FNV);
  r=sys3(SYS_unmount,"/mnt",0,0); status("UNMOUNT",r);
  return version && dyld && launchd && r==0 ? 0 : 4;
}
int main(int argc, char **argv, char **envp) {
  (void)argc; (void)argv; (void)envp;
  console=sys3(SYS_open,"/dev/console",2,0);
  begin("START"); append(" 18H17 READONLY UFS/HFS"); end();
  int result=run();
  begin("RESULT"); append(result==0 ? " PASS read_only_mount_and_files" : " FAIL stage=");
  if (result) number(result,10); end();
  // PID1 stays alive. HakiOS's existing return timer retains the summary.
  for (;;) { struct {long sec;int usec;} tv={1,0}; sys6(SYS_select,0,0,0,0,(long)&tv,0); }
}
