/* IO fault tests of the actual bounded maintainer. No physical disks opened. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAINTENANCE_HOST_TEST
#include "journal-maintenance.c"
static unsigned char disk[532480+8388608],before[sizeof(disk)];
static char output[8192];static U positions[100];
static int ro_disk,ro_part,writes,syncs,root,geometry_ok,mounted,fail_sync;
static const char *mounts_override;
static long sc(long n,long a,long b,long c,long d){
 (void)d;
 if(n==175)return root?0:2000;
 if(n==56){const char*p=(void*)b;
  if(eq(p,"/dev/block/sda"))return 50;
  if(eq(p,"/dev/block/sda15"))return 51;
  if(eq(p,"/journal-repair-result"))return 52;
  const char*paths[]={"/sys/class/block/sda15/partition","/sys/class/block/sda15/start","/sys/class/block/sda15/size","/proc/mounts"};
  for(int i=0;i<4;i++)if(eq(p,paths[i])){positions[60+i]=0;return 60+i;}return -2;
 }
 if(n==57)return 0;
 if(n==29){if(b==0x125d){if(a==50)ro_disk=*(int*)c;else ro_part=*(int*)c;return 0;}
  if(b==0x125e){*(int*)c=a==50?ro_disk:ro_part;return 0;}
  if(b==0x80081272){*(U*)c=geometry_ok?250710192128UL:1000;return 0;}
  if(b==0x1268){*(int*)c=4096;return 0;}return -1;
 }
 if(n==62){positions[a]=b;return b;}
 if(n==63){
  if(a==51){assert(positions[a]+(U)c<=sizeof(disk));memcpy((void*)b,disk+positions[a],c);positions[a]+=c;return c;}
  const char*data[]={"15\n","786432\n","489668344\n",mounts_override?mounts_override:mounted?"/dev/block/sda15 /data hfsplus rw 0 0\n":"rootfs / rootfs rw 0 0\n"};
  if(a>=60&&a<=63){U left=len(data[a-60])-positions[a],want=(U)c;if(want>left)want=left;memcpy((void*)b,data[a-60]+positions[a],want);positions[a]+=want;return want;}return -1;
 }
 if(n==64){
  if(a==51){assert(!ro_disk&&!ro_part&&c==4096);assert(positions[a]==(writes==0?532480:528384));
   if(writes==1)assert(syncs==1);memcpy(disk+positions[a],(void*)b,c);positions[a]+=c;writes++;return c;}
  if(a==52){assert(positions[a]+(U)c<sizeof(output));memcpy(output+positions[a],(void*)b,c);positions[a]+=c;output[positions[a]]=0;return c;}
  return c;
 }
 if(n==82){syncs++;return fail_sync&&syncs==1?-5:0;}
 assert(!"unexpected syscall");return -1;
}
static void reset(void){memset(disk,0,sizeof(disk));memcpy(disk,expected_volume,8192);memcpy(disk+528384,expected_jib,4096);memcpy(before,disk,sizeof(disk));memset(positions,0,sizeof(positions));memset(output,0,sizeof(output));ro_disk=ro_part=writes=syncs=mounted=fail_sync=0;root=geometry_ok=1;mounts_override=NULL;}
static void rejected(void){assert(repair(52)!=0&&writes==0&&ro_disk&&ro_part);assert(same(disk,before,sizeof(disk)));}
int main(void){
 reset();assert(repair(52)==0&&writes==2&&syncs==2&&ro_disk&&ro_part);assert(strstr(output,"PASS INITIALIZED"));
 memcpy(before+528384,initialized_jib,4096);memcpy(before+532480,initialized_header,4096);assert(same(disk,before,sizeof(disk)));
 writes=syncs=0;assert(repair(52)==0&&writes==0); /* Idempotent completion. */
 reset();root=0;rejected();
 reset();geometry_ok=0;rejected();
 reset();mounted=1;rejected();
 reset();disk[1100]^=1;memcpy(before,disk,sizeof(disk));rejected();
 reset();disk[528390]^=1;memcpy(before,disk,sizeof(disk));rejected();
 reset();disk[532500]=1;memcpy(before,disk,sizeof(disk));rejected();
 reset();disk[532480+4096]=1;memcpy(before,disk,sizeof(disk));rejected();
 reset();fail_sync=1;assert(repair(52)!=0&&writes==1&&syncs==1&&ro_disk&&ro_part);assert(same(disk+528384,expected_jib,4096));
 reset();mounts_override="rootfs / rootfs rw 0 0\ntmpfs /dev tmpfs rw 0 0\ndevpts /dev/pts devpts rw 0 0\nadb /dev/usb-ffs/adb functionfs rw 0 0\n";assert(repair(52)==0&&writes==2&&ro_disk&&ro_part);
 puts("PASS 11 bounded writer guard, readback, idempotence and sync-failure cases");return 0;
}
