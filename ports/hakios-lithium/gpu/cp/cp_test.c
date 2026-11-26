/* Host simulation only: dynamically allocated BOs, caller-generated IOVA
 * mappings, independent ring interpreter. Not an SMMU/GPU/firmware test.
 */
#include "cp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks, cases;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static uint32_t get(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t odd(uint32_t v) { unsigned n=0; while(v){n+=v&1u;v>>=1;}return !(n&1u); }
static uint32_t hdr(uint32_t op,uint32_t n) { return 0x70000000u|n|odd(n)<<15|op<<16|odd(op)<<23; }
struct sim {
    struct gc_context c; struct gc_config cfg; struct gc_ops ops;
    struct gc_bo bo[4]; int pins[4];
    uint32_t *staging, rptr, published;
    uint64_t now;
    unsigned publishes, resolves, pin_calls, releases, to_calls, from_calls, reads, quiesces;
    unsigned fail_pin, fail_to;
    int publish_error, stop_error, stopped, proof_error, clock_error, rptr_error, read_error, from_error;
    int mutate_command, flip_commit, reenter;
    HakiosGpuSubmit request; HakiosGpuSubmitBo views[2]; HakiosGpuSubmitCmd commands[2];
};
static struct gc_bo *find(struct sim *s,uint32_t handle) {
    unsigned i;for(i=0;i<4;i++)if(s->bo[i].handle==handle)return &s->bo[i];return NULL;
}
static int resolve(void *cookie,uint32_t handle,uint32_t access,struct gc_bo *out) {
    struct sim *s=cookie;struct gc_bo *b=find(s,handle);s->resolves++;
    if(!b || (b->access&access)!=access)return -1;
    *out=*b;return 0;
}
static int pin(void *cookie,const struct gc_bo *b) {
    struct sim *s=cookie;unsigned i;s->pin_calls++;
    if(s->reenter)CHECK(gc_quiesce(&s->c)==GC_BUSY);
    if(s->fail_pin==s->pin_calls)return -1;
    for(i=0;i<4;i++)if(s->bo[i].handle==b->handle){s->pins[i]++;return 0;}return -1;
}
static void release(void *cookie,const struct gc_bo *b) {
    struct sim *s=cookie;unsigned i;s->releases++;
    if(s->reenter)CHECK(gc_quiesce(&s->c)==GC_BUSY);
    for(i=0;i<4;i++)if(s->bo[i].handle==b->handle){CHECK(s->pins[i]>0);s->pins[i]--;return;}
    CHECK(0);
}
static int to_device(void *cookie,const struct gc_bo *b,uint64_t offset,size_t bytes) {
    struct sim *s=cookie;s->to_calls++;CHECK(offset<=b->bytes && bytes<=b->bytes-offset);
    if(s->mutate_command && b->handle==s->bo[2].handle)put(s->bo[2].cpu,hdr(0x75,0));
    return s->fail_to==s->to_calls ? -1 : 0;
}
static int from_device(void *cookie,const struct gc_bo *b,uint64_t offset,size_t bytes) {
    struct sim *s=cookie;s->from_calls++;CHECK(offset<=b->bytes && bytes<=b->bytes-offset);return s->from_error ? -1 : 0;
}
static int read32(void *cookie,const struct gc_bo *b,uint64_t offset,uint32_t *out) {
    struct sim *s=cookie;s->reads++;CHECK(offset<=b->bytes && 4<=b->bytes-offset);
    if(s->read_error)return -1;
    *out=get(b->cpu+offset);
    if(s->flip_commit && s->cfg.fence_width==GC_FENCE64 && offset==s->cfg.fence_offset+8 && (s->reads%4)==0)
        *out^=1u;
    return 0;
}
static int rptr(void *cookie,uint32_t *out) { struct sim *s=cookie;if(s->rptr_error)return -1;*out=s->rptr;return 0; }
static int publish(void *cookie,uint32_t wptr) {
    struct sim *s=cookie;s->publishes++;CHECK(s->c.state==GC_BUSY_STATE);CHECK(wptr<s->cfg.ring_words);
    s->published=wptr;s->stopped=0;
    if(s->reenter)CHECK(gc_poll(&s->c)==GC_BUSY);
    return s->publish_error ? -1 : 0;
}
static int now(void *cookie,uint64_t *out) { struct sim *s=cookie;if(s->clock_error)return -1;*out=s->now;return 0; }
static int stop(void *cookie) { struct sim *s=cookie;s->quiesces++;return s->stop_error ? -1 : 0; }
static int proof(void *cookie) { struct sim *s=cookie;return s->proof_error ? -1 : s->stopped; }
static void setup(struct sim *s,unsigned seed,enum gc_fence_width width,uint32_t words,uint32_t initial) {
    unsigned i;uint64_t aperture=UINT64_C(0x100000000)+(uint64_t)seed*0x100000;
    memset(s,0,sizeof(*s));s->now=100;s->stopped=1;s->rptr=initial;s->published=initial;
    for(i=0;i<4;i++) {
        s->bo[i].handle=10u+seed*4u+i;s->bo[i].access=3;
        s->bo[i].bytes=4096;s->bo[i].cpu_bytes=4096;s->bo[i].cpu=calloc(1,4096);CHECK(s->bo[i].cpu);
        s->bo[i].iova=aperture+0x10000u+(uint64_t)i*0x2000;
        s->bo[i].address_space=7u+seed;s->bo[i].generation=11u+seed;
        s->bo[i].pin_token=(uint64_t)seed*4u+i+1u;
    }
    s->staging=calloc(4096,sizeof(uint32_t));CHECK(s->staging);
    s->cfg=(struct gc_config){s->bo[0].handle,s->bo[1].handle,words,initial,3,
        64,32,s->bo[0].address_space,s->bo[0].generation,aperture,0x80000,width,
        0,0,s->staging,4096};
    s->ops=(struct gc_ops){s,resolve,pin,release,to_device,from_device,read32,rptr,publish,now,stop,proof};
    s->views[0]=(HakiosGpuSubmitBo){GC_READ,s->bo[2].handle,UINT64_MAX};
    s->views[1]=(HakiosGpuSubmitBo){GC_WRITE,s->bo[3].handle,1};
    s->request=(HakiosGpuSubmit){0};s->request.flags=HAKIOS_GPU_PIPE_3D;s->request.nr_bos=2;s->request.nr_cmds=1;
    s->request.fence=0xfedcba98;s->request.fence_fd=-1;s->request.queueid=3;
    /* Deliberately unusable VAs: core must consume resolved copyin views only. */
    s->request.bos=1;s->request.cmds=UINT64_MAX;
    s->commands[0]=(HakiosGpuSubmitCmd){HAKIOS_GPU_SUBMIT_COMMAND_BUFFER,0,0,4,0,0,0};
    put(s->bo[2].cpu,hdr(0x10,0));cases++;
}
static void cleanup(struct sim *s) {
    unsigned i;s->stop_error=0;s->proof_error=0;s->stopped=1;
    if(s->c.state!=GC_EMPTY && s->c.state!=GC_STOPPED)CHECK(gc_quiesce(&s->c)==GC_OK);
    for(i=0;i<4;i++){CHECK(!s->pins[i]);free(s->bo[i].cpu);}free(s->staging);
}
static enum gc_status submit(struct sim *s) { return gc_submit_abi(&s->c,&s->request,s->views,2,s->commands,s->request.nr_cmds,200); }
/* Independent simulated consumer reads a modular byte ring, writes only into
 * actual registered host BO views. This is NOT a GPU interpreter/backend. */
static void consume(struct sim *s) {
    uint32_t mask=s->cfg.ring_words-1;unsigned steps=0;
    while(s->rptr!=s->published) {
        uint32_t h=get(s->bo[0].cpu+s->cfg.ring_offset+4u*s->rptr);
        uint32_t n=h&0x3fffu,op=(h>>16)&127u,j;uint32_t payload[16384];
        CHECK((h>>28)==7);CHECK(n<s->cfg.ring_words);CHECK(steps++<s->cfg.ring_words);
        for(j=0;j<n;j++)payload[j]=get(s->bo[0].cpu+s->cfg.ring_offset+4u*((s->rptr+1u+j)&mask));
        if(op==0x3d) {
            uint64_t address=(uint64_t)payload[0]|(uint64_t)payload[1]<<32;unsigned b;int found=0;
            CHECK(n>=3);
            for(b=0;b<4;b++)if(address>=s->bo[b].iova && address-s->bo[b].iova<s->bo[b].bytes) {
                uint64_t at=address-s->bo[b].iova;CHECK(s->pins[b]>0);CHECK((uint64_t)(n-2)*4<=s->bo[b].bytes-at);
                for(j=2;j<n;j++)put(s->bo[b].cpu+at+(j-2u)*4u,payload[j]);found=1;
            }
            CHECK(found);
        } else CHECK(op==0x10 || (op==0x12 && n==0));
        s->rptr=(s->rptr+n+1u)&mask;
    }
}
static void packet_tests(void) {
    unsigned op,n,bit;uint32_t h=0;struct gc_packet p={9,9,9,9};
    for(op=0;op<128;op++)for(n=0;n<16400;n+=31) {
        uint32_t before=h;enum gc_status rc=gc_type7(op,n,&h);checks++;
        if(n>16383){CHECK(rc==GC_INVALID && h==before);continue;}
        CHECK(rc==GC_OK);CHECK(h==hdr(op,n));CHECK(gc_decode(h,&p)==GC_OK && p.opcode==op && p.payload==n);
        for(bit=0;bit<32;bit++)if(bit==14 || bit==15 || bit==23 || (bit>=24 && bit<28))CHECK(gc_decode(h^(1u<<bit),&p)!=GC_OK);
    }
    for(op=1;op<0x40000;op+=97)for(n=1;n<128;n+=17) {
        uint32_t before=h;enum gc_status rc=gc_type4(op,n,&h);
        if(n>=127){CHECK(rc==GC_INVALID && h==before);continue;}
        CHECK(rc==GC_OK);CHECK(h==(0x40000000u|n|odd(n)<<7|op<<8|odd(op)<<27));
        CHECK(gc_decode(h,&p)==GC_OK && p.reg==op && p.payload==n);
        CHECK(gc_decode(h^0x04000000u,&p)==GC_CORRUPT);
    }
    CHECK(gc_type7(128,0,&h)==GC_INVALID);CHECK(gc_type7(0,16384,&h)==GC_INVALID);
    CHECK(gc_type4(0,1,&h)==GC_INVALID);CHECK(gc_type4(0x40000,1,&h)==GC_INVALID);
    CHECK(gc_type4(1,127,&h)==GC_INVALID);CHECK(gc_type4(1,0,&h)==GC_INVALID);
    CHECK(gc_decode(0xc0000000u,&p)==GC_UNSUPPORTED);CHECK(gc_decode(0,NULL)==GC_INVALID);
    {
        uint32_t v[9],before[9];size_t written=99;
        const uint32_t expected[9]={0x70c80008,0x2f,3,0x20000000,0,0,0x0b,0,0};
        memset(v,0xa5,sizeof(v));memcpy(before,v,sizeof(v));
        CHECK(gc_me_init_a530(v,8,&written)==GC_NOSPACE && written==99 && !memcmp(v,before,sizeof(v)));
        CHECK(gc_me_init_a530(v,9,&written)==GC_OK && written==9 && !memcmp(v,expected,sizeof(v)));
    }
    CHECK(gc_compare32(0,UINT32_MAX)==GC_AFTER);CHECK(gc_compare32(UINT32_MAX,0)==GC_BEFORE);
    CHECK(gc_compare32(0x80000000,0)==GC_AMBIGUOUS);CHECK(gc_compare32(7,7)==GC_EQUAL);
    CHECK(gc_compare64(0,UINT64_MAX)==GC_AFTER);CHECK(gc_compare64(UINT64_MAX,0)==GC_BEFORE);
    CHECK(gc_compare64(UINT64_C(0x8000000000000000),0)==GC_AMBIGUOUS);
    for(op=2;op<=64;op*=2)for(n=0;n<op;n++)for(bit=0;bit<op;bit++) {
        uint32_t free_words=999;CHECK(gc_ring_free(op,n,bit,&free_words)==GC_OK);
        CHECK(free_words==(bit+op-n-1)%op);
    }
    cases++;
}
static void fw_tests(void) {
    uint8_t *b=calloc(1,128);size_t n;struct gc_firmware v,before;
    CHECK(b);memset(&before,0x4e,sizeof(before));put(b,0xaabbccdd);put(b+4,0xau);put(b+12,1u);
    for(n=0;n<64;n++) {
        v=before;
        if(n>=8 && !(n&3u)) {
            CHECK(gc_firmware_view(GC_PM4,b,n,124,&v)==GC_OK);CHECK(v.payload==b+4 && v.bytes==n-4 && v.file_word0==0xaabbccddu);
            CHECK(gc_firmware_view(GC_PFP,b,n,124,&v)==GC_OK && v.whereami_candidate==(n>=16));
        } else CHECK(gc_firmware_view(GC_PFP,b,n,124,&v)==GC_INVALID && !memcmp(&v,&before,sizeof(v)));
    }
    CHECK(gc_firmware_view(GC_PM4,b,128,123,&v)==GC_INVALID);
    CHECK(gc_firmware_view((enum gc_fw_kind)0,b,16,124,&v)==GC_INVALID);
    CHECK(gc_firmware_view(GC_PFP,NULL,16,124,&v)==GC_INVALID);
    free(b);cases++;
}
static void probe_tests(void) {
    struct sim s;uint32_t output[20],before[20];size_t written;unsigned n,w;
    setup(&s,20,GC_FENCE64,16,14);memset(output,0xc5,sizeof(output));memcpy(before,output,sizeof(output));
    for(w=4;w<=8;w+=4)for(n=0;n<14;n++) {
        written=77;memcpy(output,before,sizeof(output));
        if(n<(w==4?7u:13u))CHECK(gc_probe_stream(&s.bo[1],32,(enum gc_fence_width)w,0,UINT64_MAX,output,n,&written)==GC_NOSPACE && written==77 && !memcmp(output,before,sizeof(output)));
        else {
            uint64_t address=s.bo[1].iova+32;
            CHECK(gc_probe_stream(&s.bo[1],32,(enum gc_fence_width)w,0,UINT64_MAX,output,n,&written)==GC_OK);
            CHECK(output[0]==0x70108000 && output[1]==0x70928000);
            CHECK(output[2]==(w==4?0x703d8003u:0x703d0004u));
            CHECK(output[3]==(uint32_t)address && output[4]==(uint32_t)(address>>32));
            CHECK(output[5]==(w==4?0u:UINT32_MAX));
        }
    }
    CHECK(gc_probe_stream(&s.bo[1],33,GC_FENCE32,1,1,output,20,&written)==GC_RANGE);
    CHECK(gc_probe_stream(&s.bo[1],4092,GC_FENCE64,1,1,output,20,&written)==GC_RANGE);
    s.bo[1].access=GC_READ;CHECK(gc_probe_stream(&s.bo[1],32,GC_FENCE32,1,1,output,20,&written)==GC_INVALID);
    cleanup(&s);
}
static void lifecycle_tests(void) {
    unsigned seed,w,initial;
    for(seed=0;seed<40;seed++)for(w=4;w<=8;w+=4)for(initial=0;initial<16;initial++) {
        struct sim s;uint32_t old;setup(&s,seed,(enum gc_fence_width)w,16,initial);
        s.cfg.last_seq32=UINT32_MAX;s.cfg.last_seq64=UINT64_MAX;
        CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK && s.pins[0]==1 && s.pins[1]==1);
        CHECK(submit(&s)==GC_OK && s.request.fence==1 && s.c.seq64==0 && s.publishes==1);
        CHECK(gc_poll(&s.c)==GC_PENDING && s.c.retain_dma && !s.releases);
        old=s.published;CHECK(submit(&s)==GC_BUSY && s.published==old && s.publishes==1);
        consume(&s);CHECK(s.rptr==s.c.end);CHECK(gc_poll(&s.c)==GC_OK && s.c.observed_complete);
        CHECK(s.c.state==GC_COMPLETE && s.c.retain_dma && !s.releases);
        CHECK(gc_quiesce(&s.c)==GC_RETAIN_DMA && s.quiesces==1 && !s.releases);
        s.stopped=1;CHECK(gc_quiesce(&s.c)==GC_OK && s.c.state==GC_STOPPED && !s.c.retain_dma);
        cleanup(&s);
    }
}
static void abi_tests(void) {
    unsigned mode;
    for(mode=0;mode<28;mode++) {
        struct sim s;uint32_t before;enum gc_status rc;setup(&s,100+mode,GC_FENCE32,32,27);
        CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);before=s.request.fence;
        switch(mode) {
        case 0:s.request.flags=0;break;case 1:s.request.queueid=4;break;case 2:s.request.nr_bos=0;break;
        case 3:s.request.nr_cmds=0;break;case 4:s.request.nr_in_syncobjs=1;break;case 5:s.request.fence_fd=0;break;
        case 6:s.request.pad=1;break;case 7:s.views[0].flags=4;break;case 8:s.views[0].handle=0;break;
        case 9:s.views[1].handle=s.views[0].handle;break;case 10:s.views[0].handle=s.cfg.ring_handle;break;
        case 11:s.commands[0].type=2;break;case 12:s.commands[0].size=0;break;case 13:s.commands[0].size=3;break;
        case 14:s.commands[0].submit_offset=1;break;case 15:s.commands[0].submit_idx=2;break;
        case 16:s.commands[0].nr_relocs=1;break;case 17:s.commands[0].relocs=1;break;
        case 18:s.commands[0].submit_offset=4096;break;case 19:s.bo[2].generation++;break;
        case 20:s.bo[2].address_space++;break;case 21:s.bo[2].iova=s.cfg.aperture_base-4;break;
        case 22:put(s.bo[2].cpu,hdr(0x75,0));break;case 23:put(s.bo[2].cpu,0x70100000);break;
        case 24:put(s.bo[2].cpu,hdr(0x10,2));break;case 25:put(s.bo[2].cpu,0x40e5f001);break;
        case 26:s.request.nr_cmds=2;s.commands[1]=s.commands[0];s.commands[1].size=0;break;
        case 27:s.commands[0].pad=1;break;
        }
        rc=submit(&s);CHECK(rc!=GC_OK);CHECK(s.request.fence==before && !s.publishes && s.c.state==GC_IDLE);
        CHECK(!s.pins[2] && !s.pins[3] && s.pins[0]==1 && s.pins[1]==1);cleanup(&s);
    }
    for(mode=0;mode<4;mode++) {
        struct sim s;uint64_t address;setup(&s,200+mode,GC_FENCE64,32,30);CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);
        address=mode==0?s.bo[3].iova+12:mode==1?s.bo[3].iova+4092:mode==2?s.bo[1].iova+32:s.bo[3].iova+4096;
        put(s.bo[2].cpu,hdr(0x3d,3));put(s.bo[2].cpu+4,(uint32_t)address);put(s.bo[2].cpu+8,(uint32_t)(address>>32));put(s.bo[2].cpu+12,0x89abcdef);
        s.commands[0].size=16;
        if(mode<2){CHECK(submit(&s)==GC_OK);consume(&s);CHECK(get(s.bo[3].cpu+(mode?4092:12))==0x89abcdef);CHECK(gc_poll(&s.c)==GC_OK);}
        else CHECK(submit(&s)==GC_RANGE && !s.publishes);
        cleanup(&s);
    }
}
static void errors_and_torn_tests(void) {
    unsigned mode;
    for(mode=0;mode<13;mode++) {
        struct sim s;uint32_t before;setup(&s,300+mode,GC_FENCE64,32,31);
        CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);before=s.request.fence;
        switch(mode) {
        case 0:s.fail_pin=3;break;case 1:s.fail_pin=4;break;
        case 2:s.fail_to=1;break;case 3:s.fail_to=2;break;case 4:s.fail_to=3;break;case 5:s.fail_to=4;break;
        case 6:s.publish_error=1;break;case 7:s.reenter=1;break;case 8:s.mutate_command=1;break;
        default:break;
        }
        if(mode<6){CHECK(submit(&s)==GC_CALLBACK && !s.publishes && s.request.fence==before);CHECK(s.pins[0]==1 && s.pins[1]==1 && !s.pins[2]);}
        else if(mode==6){CHECK(submit(&s)==GC_RETAIN_DMA && s.publishes==1 && s.request.fence==before && s.c.state==GC_RETAINED);CHECK(s.pins[0] && s.pins[1] && s.pins[2] && s.pins[3] && !s.releases);}
        else {
            CHECK(submit(&s)==GC_OK);
            if(mode==9)s.from_error=1;else if(mode==10)s.read_error=1;else if(mode==11)s.rptr=20;else if(mode==12)s.now=99;
            if(mode>=9){CHECK(gc_poll(&s.c)==GC_RETAIN_DMA && !s.releases);}
            else {consume(&s);CHECK(gc_poll(&s.c)==GC_OK);}
        }
        cleanup(&s);
    }
    {
        struct sim s;setup(&s,400,GC_FENCE64,16,14);CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);CHECK(submit(&s)==GC_OK);
        s.rptr=s.c.end;put(s.bo[1].cpu+32,(uint32_t)s.c.seq64);put(s.bo[1].cpu+36,0);
        /* An exact value without commit is stale, not completion. */
        CHECK(gc_poll(&s.c)==GC_PENDING);put(s.bo[1].cpu+40,s.c.seq32);s.flip_commit=1;s.reads=0;
        CHECK(gc_poll(&s.c)==GC_PENDING && !s.c.observed_complete);s.flip_commit=0;
        CHECK(gc_poll(&s.c)==GC_OK && s.c.retain_dma);cleanup(&s);
    }
    for(mode=0;mode<4;mode++) {
        struct sim s;setup(&s,410+mode,GC_FENCE32,16,15);CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);CHECK(submit(&s)==GC_OK);
        if(mode==1){consume(&s);} /* late completion must not defeat deadline */
        if(mode==2)s.stop_error=1;if(mode==3)s.proof_error=1;
        s.now=200;CHECK(gc_poll(&s.c)==GC_TIMEOUT && s.c.state==GC_RETAINED && s.c.retain_dma && !s.releases && s.quiesces==1);
        CHECK(gc_poll(&s.c)==GC_RETAIN_DMA && s.publishes==1 && s.quiesces==1);cleanup(&s);
    }
}
static void init_failure_tests(void) {
    unsigned mode;
    for(mode=0;mode<20;mode++) {
        struct sim s;uint8_t *old;setup(&s,500+mode,GC_FENCE32,16,4);old=s.bo[1].cpu;
        switch(mode) {
        case 0:s.ops.now_ns=NULL;break;case 1:s.cfg.ring_words=3;break;
        case 2:s.cfg.ring_words=8;break;case 3:s.cfg.initial_wptr=16;break;
        case 4:s.cfg.generation=0;break;case 5:s.cfg.address_space=0;break;
        case 6:s.cfg.aperture_base=UINT64_MAX;s.cfg.aperture_bytes=2;break;
        case 7:s.cfg.fence_handle=s.cfg.ring_handle;break;case 8:s.bo[0].cpu_bytes=4095;break;
        case 9:s.bo[0].iova++;break;case 10:s.bo[0].iova=s.cfg.aperture_base-4;break;
        case 11:s.fail_pin=1;break;case 12:s.fail_pin=2;break;case 13:s.stopped=0;break;
        case 14:s.bo[1].cpu=s.bo[0].cpu;break;case 15:s.bo[1].pin_token=s.bo[0].pin_token;break;
        case 16:s.cfg.staging=(uint32_t *)(s.bo[0].cpu+1);break;
        case 17:s.cfg.staging=(uint32_t *)s.bo[0].cpu;break;
        case 18:s.cfg.fence_offset=4096;break;case 19:s.rptr=5;break;
        }
        CHECK(gc_init(&s.c,&s.cfg,&s.ops)!=GC_OK);CHECK(!s.pins[0] && !s.pins[1] && !s.publishes);
        s.bo[1].cpu=old;cleanup(&s);
    }
    {
        struct sim s;setup(&s,540,GC_FENCE32,16,5);CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_OK);
        CHECK(gc_init(&s.c,&s.cfg,&s.ops)==GC_INVALID && s.pins[0]==1 && s.pins[1]==1);
        s.rptr=(s.c.wptr+2)&15;CHECK(submit(&s)==GC_NOSPACE && !s.publishes);
        cleanup(&s);
    }
}
int main(void) {
    packet_tests();fw_tests();probe_tests();lifecycle_tests();abi_tests();errors_and_torn_tests();init_failure_tests();
    printf("G4 host cases=%lu checks=%lu; simulated transport only; device_operations=0\n",cases,checks);
    return 0;
}
