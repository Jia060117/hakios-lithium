#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/return-fastboot/ufs-return-receipt.h"
int main(void)
{
    unsigned checks=0;
    unsigned char wire[LUR_SIZE]=LUR_INITIALIZER,valid[LUR_SIZE];
    struct lur_result out;
    assert(lur_initial(wire,0)); ++checks;
    assert(!lur_decode(wire,42,&out)); ++checks;
    assert(!lur_seed(wire,0)); ++checks;
    assert(lur_seed(wire,42)); ++checks;
    assert(!lur_seed(wire,42)); ++checks;
    assert(lur_initial(wire,42)); ++checks;
    assert(!lur_finish(wire,41,3,8)); ++checks;
    assert(!lur_finish(wire,42,3,0)); ++checks;
    assert(!lur_finish(wire,42,UINT32_MAX,8)); ++checks;
    assert(lur_finish(wire,42,3,8)); ++checks;
    assert(!lur_finish(wire,42,3,8)); ++checks;
    assert(lur_decode(wire,42,&out)); ++checks;
    assert(out.cookie==42 && out.generation==42 && out.data_dev==3 &&
        out.flush_request==8 && out.flags==7 && out.error==0); ++checks;
    assert(!lur_decode(wire,43,&out)); ++checks;
    assert(!lur_decode(NULL,42,&out)); ++checks;
    assert(!lur_decode(wire,42,NULL)); ++checks;
    assert(!lur_decode(wire,42,(void *)wire)); ++checks;
    memcpy(valid,wire,LUR_SIZE);
    for (unsigned i=0;i<LUR_SIZE;i++) {
        for (unsigned bit=0;bit<8;bit++) {
            memcpy(wire,valid,LUR_SIZE);
            wire[i]^=1u<<bit;
            assert(!lur_decode(wire,42,&out)); ++checks;
        }
    }
    memcpy(wire,valid,LUR_SIZE);
    assert(!lur_decode(wire,0,&out)); ++checks;
    const char args[]="ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1 "
        "bootcpu=1 cpus=3 rd=md0 ufs_return=1 return_fastboot=1";
    puts("receipt byte corruption, seeding, stale generation and overlap checks passed");
    printf("PASS checks=%u example_argument_bytes=%zu\n",checks,sizeof(args)-1);
    return 0;
}
