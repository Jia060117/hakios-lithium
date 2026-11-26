#include "smmu.h"
#include "linux-facts.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
static unsigned cases, checks;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(#x); } while (0)
template<class F> void test(const char* name,F f) { f(); ++cases; std::printf("PASS %s\n",name); }

struct Event { char type; uint64_t address,bytes; };
struct Fixture {
    std::unique_ptr<gs_arena> arena=std::make_unique<gs_arena>();
    gs_domain d{};
    gs_config cfg{0x40000000,0x80000000,0x10000000,32,40,24};
    std::vector<Event> events;
    char fail=0;
    unsigned fail_after=0;
    bool reenter=false;
    int event(char c,uint64_t address=0,uint64_t bytes=0) {
        events.push_back({c,address,bytes});
        if (reenter) {
            CHECK(gs_map(&d,cfg.iova_base,0,1,GS_READ)==GS_BAD_STATE);
            CHECK(gs_build_pool(&d,arena.get(),&cfg,GS_READ)==GS_BAD_STATE);
            auto callbacks=cb();
            CHECK(gs_bind_callbacks(&d,&callbacks)==GS_BAD_STATE);
            struct gs_table_view view{};
            struct gs_layout layout{};
            CHECK(gs_table_view(&d,0,&view)==GS_BAD_STATE);
            CHECK(gs_layout(&d,&layout)==GS_BAD_STATE);
        }
        if (c==fail) {
            if (!fail_after) return 1;
            --fail_after;
        }
        return 0;
    }
    static int begin(void* p,gs_op,uint64_t a,uint64_t n) { return ((Fixture*)p)->event('B',a,n); }
    static int data(void* p,uint64_t a,uint64_t n,gs_direction direction) {
        return ((Fixture*)p)->event(direction==GS_TO_DEVICE?'D':'R',a,n);
    }
    static int clean(void* p,const void* cpu,uint64_t a,size_t n) {
        auto& f=*(Fixture*)p;
        CHECK(n==4096 && a>=f.cfg.table_pa && (a-f.cfg.table_pa)%4096==0);
        auto index=(a-f.cfg.table_pa)/4096;
        CHECK(index<f.d.tables_used && cpu==f.arena->pte[index]);
        return f.event('C',a,n);
    }
    static int barrier(void* p) { return ((Fixture*)p)->event('W'); }
    static int invalidate(void* p,uint64_t a,uint64_t n) { return ((Fixture*)p)->event('I',a,n); }
    static int sync(void* p) { return ((Fixture*)p)->event('S'); }
    static void end(void* p,gs_op,int ok) { (void)((Fixture*)p)->event(ok?'E':'A'); }
    gs_callbacks cb(){return {this,begin,data,clean,barrier,invalidate,sync,end};}
    gs_status init(){auto callbacks=cb();return gs_init(&d,arena.get(),&cfg,&callbacks);}
    void ready(){CHECK(init()==GS_OK); CHECK(gs_publish(&d)==GS_OK); events.clear();}
    std::string sequence() const {std::string s; for (const auto& e:events)s+=e.type;return s;}
};

/* Independent walk of literal 64-bit descriptors, not gs_lookup internals. */
static uint64_t walk(const Fixture& f,uint64_t va,uint64_t* desc=nullptr) {
    unsigned levels=(f.cfg.iova_bits-12+8)/9, table=0;
    for(unsigned l=4-levels;l<=3;++l) {
        auto index=(va>>(12+9*(3-l)))&511;
        uint64_t pte=f.arena->pte[table][index];
        if (!pte) return UINT64_MAX;
        CHECK((pte&3)==3);
        if (l==3) {if(desc)*desc=pte;return (pte&UINT64_C(0x0000fffffffff000))+(va&4095);}
        auto pa=pte&UINT64_C(0x0000fffffffff000);
        CHECK(pa>=f.cfg.table_pa);
        table=(unsigned)((pa-f.cfg.table_pa)/4096); CHECK(table<f.d.tables_used);
    }
    throw std::runtime_error("no L3 leaf");
}
static void untouched(Fixture& f,uint64_t va,unsigned p,unsigned n,unsigned perm,gs_status expect) {
    auto snapshot=*f.arena;
    auto before=f.events.size(); auto mapped=f.d.mapped_pages;
    CHECK(gs_map(&f.d,va,p,n,perm)==expect);
    CHECK(!std::memcmp(&snapshot,f.arena.get(),sizeof(snapshot)));
    CHECK(f.events.size()==before && f.d.mapped_pages==mapped);
}
int main() try {
    test("Linux descriptor facts: S1 AP/AF/XN/nG/MAIR and no S2 fields",[]{
        CHECK(GS_PTE_ADDRESS==LF_ARM_LPAE_PTE_ADDR_MASK);
        CHECK(GS_MAIR==((uint64_t)LF_ARM_LPAE_MAIR_ATTR_NC |
            ((uint64_t)LF_ARM_LPAE_MAIR_ATTR_WBRWA<<8) |
            ((uint64_t)LF_ARM_LPAE_MAIR_ATTR_DEVICE<<16) |
            ((uint64_t)LF_ARM_LPAE_MAIR_ATTR_INC_OWBRWA<<24)));
        for (unsigned permission: {GS_READ,GS_READ|GS_WRITE,GS_READ|GS_CACHE,GS_READ|GS_PRIVILEGED,
                                   GS_READ|GS_WRITE|GS_CACHE|GS_PRIVILEGED}) {
            Fixture f;f.ready();CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,permission)==GS_OK);
            uint64_t descriptor=0;CHECK(walk(f,f.cfg.iova_base,&descriptor)==f.cfg.pool_pa);
            uint64_t expected=f.cfg.pool_pa | LF_ARM_LPAE_PTE_TYPE_PAGE | LF_ARM_LPAE_PTE_AF |
                LF_ARM_LPAE_PTE_nG | LF_ARM_LPAE_PTE_XN;
            if (!(permission&GS_WRITE))expected|=LF_ARM_LPAE_PTE_AP_RDONLY;
            if (!(permission&GS_PRIVILEGED))expected|=LF_ARM_LPAE_PTE_AP_UNPRIV;
            expected|=(permission&GS_CACHE)?((1ull<<2)|LF_ARM_LPAE_PTE_SH_IS):LF_ARM_LPAE_PTE_SH_OS;
            CHECK(descriptor==expected);
            CHECK(!(descriptor & ((1ull<<51)|(1ull<<55)|(1ull<<5))));
        }
    });
    test("all PA widths and TTBR0 IOVA widths; top PA preserved",[]{
        const unsigned widths[]={32,36,40,42,44,48};
        for(unsigned bits:widths)for(unsigned ias:{25u,30u,32u,39u,40u,48u}) {
            Fixture f;f.cfg.pa_bits=bits;f.cfg.iova_bits=ias;
            f.cfg.iova_base=(1ull<<ias)-GS_POOL_BYTES;
            f.cfg.pool_pa=(1ull<<bits)-GS_POOL_BYTES;
            f.ready();struct gs_layout out{};CHECK(gs_layout(&f.d,&out)==GS_OK);
            CHECK(out.root_pa==f.cfg.table_pa && out.t0sz==64-ias);
            CHECK(out.start_level==4-(ias-12+8)/9 && out.mair==GS_MAIR);
            CHECK(gs_map(&f.d,f.cfg.iova_base+GS_POOL_BYTES-4096,8191,1,GS_READ)==GS_OK);
            CHECK(walk(f,f.cfg.iova_base+GS_POOL_BYTES-1)==(1ull<<bits)-1);
            uint64_t pa=0,desc=0;CHECK(gs_lookup(&f.d,f.cfg.iova_base+GS_POOL_BYTES-1,&pa,&desc)==GS_OK);
            CHECK(pa==(1ull<<bits)-1);
        }
    });
    test("full 32MiB map, 8192 exact 4KiB translations and full revoke",[]{
        Fixture f;f.ready();CHECK(f.d.tables_used==18);
        CHECK(gs_map_pool(&f.d,GS_READ|GS_WRITE)==GS_OK && f.d.mapped_pages==8192);
        CHECK(f.sequence()==std::string("BD")+std::string(16,'C')+"WISE");
        for(unsigned p=0;p<8192;++p) {
            CHECK(walk(f,f.cfg.iova_base+(uint64_t)p*4096+123)==f.cfg.pool_pa+(uint64_t)p*4096+123);
            uint64_t pa=0,desc=0;CHECK(gs_lookup(&f.d,f.cfg.iova_base+(uint64_t)p*4096,&pa,&desc)==GS_OK);
            CHECK(pa==f.cfg.pool_pa+(uint64_t)p*4096);
        }
        f.events.clear();CHECK(gs_unmap(&f.d,f.cfg.iova_base,8192)==GS_OK);
        CHECK(f.sequence()==std::string("B")+std::string(16,'C')+"WIS"+std::string(8192,'R')+"E");
        CHECK(f.d.mapped_pages==0);
        for(auto bitmap:f.d.physical_used)CHECK(bitmap==0);
        uint64_t pa=77,desc=99;CHECK(gs_lookup(&f.d,f.cfg.iova_base,&pa,&desc)==GS_NOT_MAPPED);
        CHECK(pa==77 && desc==99);
    });
    test("cross L3/L2/L1/L0 boundaries; bounded table exhaustion",[]{
        for(uint64_t boundary: {1ull<<21,1ull<<30,1ull<<39}) {
            Fixture f;f.cfg.iova_bits=48;f.cfg.iova_base=boundary-4096;f.ready();
            CHECK(f.d.tables_used<=22);
            CHECK(gs_map_pool(&f.d,GS_READ)==GS_OK);
            CHECK(walk(f,boundary-1)==f.cfg.pool_pa+4095);
            CHECK(walk(f,boundary)==f.cfg.pool_pa+4096);
            CHECK(walk(f,f.cfg.iova_base+GS_POOL_BYTES-1)==f.cfg.pool_pa+GS_POOL_BYTES-1);
        }
        Fixture f;f.cfg.table_capacity=17;CHECK(f.init()==GS_NO_TABLES);
        CHECK(f.d.state==GS_EMPTY && f.events.empty());
        f.cfg.table_capacity=18;CHECK(f.init()==GS_OK);
    });
    test("init rejects width, alignment, overlap and overflowing apertures",[]{
        for(unsigned bits:{0u,31u,33u,52u,64u}) {Fixture f;f.cfg.pa_bits=bits;CHECK(f.init()==GS_INVALID);}
        for(unsigned bits:{0u,24u,49u,64u}) {Fixture f;f.cfg.iova_bits=bits;CHECK(f.init()==GS_INVALID);}
        {Fixture f;f.cfg.iova_base=UINT64_MAX-4095;CHECK(f.init()==GS_RANGE);}
        {Fixture f;f.cfg.pool_pa=(1ull<<40)-4096;CHECK(f.init()==GS_RANGE);}
        {Fixture f;f.cfg.table_pa=(1ull<<40)-4096;CHECK(f.init()==GS_RANGE);}
        {Fixture f;f.cfg.table_pa=f.cfg.pool_pa;CHECK(f.init()==GS_RANGE);}
        {Fixture f;f.cfg.pool_pa++;CHECK(f.init()==GS_INVALID);}
        {Fixture f;f.cfg.iova_base++;CHECK(f.init()==GS_INVALID);}
        {Fixture f;auto cb=f.cb();cb.tlb_sync=nullptr;CHECK(gs_init(&f.d,f.arena.get(),&f.cfg,&cb)==GS_INVALID);}
        {Fixture f;f.d.cfg=f.cfg;f.d.cb=f.cb();
            CHECK(gs_init(&f.d,f.arena.get(),&f.d.cfg,&f.d.cb)==GS_OK);
            CHECK(f.d.cfg.pool_pa==f.cfg.pool_pa && gs_publish(&f.d)==GS_OK);}
    });
    test("map rejects overlap, PA alias, write-only and boundary without edits",[]{
        Fixture f;f.ready();auto va=f.cfg.iova_base;
        untouched(f,va,0,0,GS_READ,GS_RANGE);
        untouched(f,va,0,8193,GS_READ,GS_RANGE);
        untouched(f,va,8192,1,GS_READ,GS_RANGE);
        untouched(f,va,8191,2,GS_READ,GS_RANGE);
        untouched(f,va+1,0,1,GS_READ,GS_RANGE);
        untouched(f,va-4096,0,1,GS_READ,GS_RANGE);
        untouched(f,va+GS_POOL_BYTES-4096,0,2,GS_READ,GS_RANGE);
        untouched(f,UINT64_MAX-4095,0,1,GS_READ,GS_RANGE);
        untouched(f,va,0,1,GS_WRITE,GS_INVALID);
        untouched(f,va,0,1,0x81,GS_INVALID);
        CHECK(gs_map(&f.d,va,0,2,GS_READ)==GS_OK);
        untouched(f,va,2,1,GS_READ,GS_EXISTS);
        untouched(f,va+8192,1,1,GS_READ,GS_PHYS_BUSY);
        untouched(f,va+4096,3,2,GS_READ,GS_EXISTS);
        auto before=f.events.size();CHECK(gs_unmap(&f.d,va,3)==GS_NOT_MAPPED);
        CHECK(f.events.size()==before && f.d.mapped_pages==2 && walk(f,va)==f.cfg.pool_pa);
    });
    test("begin/data failure before PTE is retryable; callback reentry blocked",[]{
        for(char fail:{'B','D'}) {
            Fixture f;f.ready();f.fail=fail;auto snapshot=*f.arena;
            CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,GS_READ)==GS_CALLBACK);
            CHECK(f.d.state==GS_READY && f.d.mapped_pages==0);
            CHECK(!std::memcmp(&snapshot,f.arena.get(),sizeof(snapshot)));
            CHECK(f.sequence()==(fail=='B'?"B":"BDA"));
            f.fail=0;f.reenter=true;CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,GS_READ)==GS_OK);
        }
        Fixture f;CHECK(f.init()==GS_OK);f.fail='B';CHECK(gs_publish(&f.d)==GS_CALLBACK);
        CHECK(f.d.state==GS_OFFLINE);f.fail=0;CHECK(gs_publish(&f.d)==GS_OK);
    });
    test("post-PTE map failures quarantine; retain all storage, never recycle",[]{
        for(char fail:{'C','W','I','S'}) {
            Fixture f;f.ready();f.fail=fail;
            CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,GS_READ)==GS_POISONED);
            CHECK(f.d.state==GS_QUARANTINED && f.events.back().type=='A');
            CHECK(walk(f,f.cfg.iova_base)==f.cfg.pool_pa);
            CHECK(gs_unmap(&f.d,f.cfg.iova_base,1)==GS_POISONED);
            uint64_t va=99;unsigned p=88;CHECK(gs_alloc(&f.d,1,GS_READ,&va,&p)==GS_POISONED);
            CHECK(va==99 && p==88);CHECK(f.init()==GS_BAD_STATE);
        }
    });
    test("unmap synchronize/data failures retain allocation quarantine",[]{
        for(char fail:{'C','W','I','S','R'}) {
            Fixture f;f.ready();CHECK(gs_map(&f.d,f.cfg.iova_base,3,2,GS_READ|GS_WRITE)==GS_OK);
            f.events.clear();f.fail=fail;
            CHECK(gs_unmap(&f.d,f.cfg.iova_base,2)==GS_POISONED);
            CHECK(f.d.state==GS_QUARANTINED && f.d.mapped_pages==2);
            CHECK((f.d.physical_used[0]&24)==24);
            CHECK(walk(f,f.cfg.iova_base)==UINT64_MAX);
            CHECK(gs_map(&f.d,f.cfg.iova_base,3,2,GS_READ)==GS_POISONED);
        }
        Fixture f;f.ready();CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,GS_READ)==GS_OK);
        f.fail='B';CHECK(gs_unmap(&f.d,f.cfg.iova_base,1)==GS_CALLBACK);
        CHECK(walk(f,f.cfg.iova_base)==f.cfg.pool_pa && f.d.state==GS_READY);
    });
    test("publish errors quarantine; no silent fake hardware success",[]{
        for(char fail:{'C','W','I','S'}) {
            Fixture f;CHECK(f.init()==GS_OK);f.fail=fail;
            CHECK(gs_publish(&f.d)==GS_POISONED && f.d.state==GS_QUARANTINED);
        }
        Fixture f;CHECK(gs_map(&f.d,0,0,1,GS_READ)==GS_BAD_STATE);
        CHECK(f.init()==GS_OK);CHECK(gs_map(&f.d,f.cfg.iova_base,0,1,GS_READ)==GS_BAD_STATE);
        CHECK(gs_publish(&f.d)==GS_OK);CHECK(gs_publish(&f.d)==GS_BAD_STATE);
    });
    test("dynamic pool allocation/revoke/reuse 3000 transactions",[]{
        Fixture f;f.ready();uint64_t seed=0x66ac530;unsigned rounds=3000;
        for(unsigned n=0;n<rounds;++n) {
            seed=seed*6364136223846793005ull+1;
            unsigned pages=1+(unsigned)((seed>>32)%97);
            uint64_t va=UINT64_MAX;unsigned physical=99999;
            CHECK(gs_alloc(&f.d,pages,GS_READ|GS_WRITE,&va,&physical)==GS_OK);
            CHECK(va==f.cfg.iova_base && physical==0);
            CHECK(walk(f,va+(uint64_t)pages*4096-1)==f.cfg.pool_pa+(uint64_t)pages*4096-1);
            CHECK(gs_unmap(&f.d,va,pages)==GS_OK && f.d.mapped_pages==0);
            f.events.clear();
        }
        CHECK(gs_map_pool(&f.d,GS_READ)==GS_OK);
        uint64_t va=11;unsigned physical=22;CHECK(gs_alloc(&f.d,1,GS_READ,&va,&physical)==GS_PHYS_BUSY);
        CHECK(va==11 && physical==22);
        CHECK(gs_unmap(&f.d,f.cfg.iova_base+4096,2)==GS_OK);
        CHECK(gs_alloc(&f.d,2,GS_READ,&va,&physical)==GS_OK && va==f.cfg.iova_base+4096 && physical==1);
    });
    test("independent IOVA/physical holes, release physical page not IOVA index",[]{
        Fixture f;f.ready();CHECK(gs_map(&f.d,f.cfg.iova_base,9,1,GS_READ)==GS_OK);
        CHECK(gs_map(&f.d,f.cfg.iova_base+8192,0,1,GS_READ)==GS_OK);
        uint64_t va=0;unsigned phys=0;CHECK(gs_alloc(&f.d,1,GS_READ,&va,&phys)==GS_OK);
        CHECK(va==f.cfg.iova_base+4096 && phys==1);
        CHECK(walk(f,va)==f.cfg.pool_pa+4096);
        CHECK(gs_unmap(&f.d,f.cfg.iova_base,1)==GS_OK);
        CHECK(!(f.d.physical_used[0]&(1ull<<9)) && (f.d.physical_used[0]&3)==3);
        CHECK(gs_map(&f.d,f.cfg.iova_base,9,1,GS_READ|GS_WRITE)==GS_OK);
    });
    test("zero-callback offline pool and read-only table views; no fake publish",[]{
        Fixture f;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ|GS_WRITE)==GS_OK);
        CHECK(f.events.empty() && f.d.state==GS_OFFLINE && f.d.mapped_pages==8192);
        CHECK(!f.d.cb.begin && !f.d.cb.table_clean && !f.d.cb.tlb_sync);
        CHECK(gs_publish(&f.d)==GS_BAD_STATE && f.d.state==GS_OFFLINE);
        CHECK(gs_map_pool(&f.d,GS_READ)==GS_BAD_STATE);
        CHECK(gs_unmap(&f.d,f.cfg.iova_base,1)==GS_BAD_STATE);
        struct gs_layout layout{};
        CHECK(gs_layout(&f.d,&layout)==GS_OK && layout.tables_used==18);
        CHECK(layout.table_bytes==18*4096 && layout.mapped_pages==8192);
        auto copy=std::make_unique<gs_arena>();
        for(unsigned t=0;t<layout.tables_used;++t) {
            struct gs_table_view view{};
            CHECK(gs_table_view(&f.d,t,&view)==GS_OK);
            CHECK(view.cpu==f.arena->pte[t] && view.bytes==4096);
            CHECK(view.pa==f.cfg.table_pa+(uint64_t)t*4096);
            std::memcpy(copy->pte[t],view.cpu,view.bytes);
        }
        CHECK(!std::memcmp(copy.get(),f.arena.get(),sizeof(gs_arena)));
        struct gs_table_view sentinel{copy.get(),0xfeed,17};
        CHECK(gs_table_view(&f.d,18,&sentinel)==GS_RANGE);
        CHECK(sentinel.cpu==copy.get() && sentinel.pa==0xfeed && sentinel.bytes==17);
        CHECK(gs_table_view(&f.d,UINT32_MAX,&sentinel)==GS_RANGE);
        for(unsigned p=0;p<8192;++p) {
            uint64_t pa=0,desc=0;
            CHECK(gs_lookup(&f.d,f.cfg.iova_base+(uint64_t)p*4096+4095,&pa,&desc)==GS_OK);
            CHECK(pa==f.cfg.pool_pa+(uint64_t)p*4096+4095);
            CHECK(walk(f,f.cfg.iova_base+(uint64_t)p*4096)==pa-4095);
        }
        for(auto word:f.d.physical_used) CHECK(word==UINT64_MAX);
        auto callbacks=f.cb();
        CHECK(gs_bind_callbacks(&f.d,&callbacks)==GS_OK && f.events.empty());
        f.reenter=true;
        CHECK(gs_publish(&f.d)==GS_OK);
        CHECK(f.sequence()==std::string("BD")+std::string(18,'C')+"WISE");
        CHECK(f.events[1].address==f.cfg.pool_pa && f.events[1].bytes==GS_POOL_BYTES);
        CHECK(gs_bind_callbacks(&f.d,&callbacks)==GS_BAD_STATE);
        CHECK(gs_unmap(&f.d,f.cfg.iova_base,8192)==GS_OK && f.d.mapped_pages==0);
    });
    test("offline generator every IOVA width 25..48 and PA width; high table PA",[]{
        unsigned ips=0;
        for(unsigned bits:{32u,36u,40u,42u,44u,48u}) {
            for(unsigned ias=25;ias<=48;++ias) {
                Fixture f;f.cfg.pa_bits=bits;f.cfg.iova_bits=ias;
                f.cfg.pool_pa=(1ull<<bits)-GS_POOL_BYTES;
                f.cfg.table_pa=f.cfg.pool_pa-GS_TABLES*4096;
                f.cfg.iova_base=(1ull<<ias)-GS_POOL_BYTES;
                CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ|GS_CACHE)==GS_OK);
                CHECK(f.events.empty() && f.d.state==GS_OFFLINE);
                struct gs_layout layout{};CHECK(gs_layout(&f.d,&layout)==GS_OK);
                CHECK(layout.ips==ips && layout.t0sz==64-ias && layout.tables_used<=22);
                for(unsigned p=0;p<8192;++p) {
                    uint64_t desc=0;
                    CHECK(walk(f,f.cfg.iova_base+(uint64_t)p*4096+4095,&desc)==
                        f.cfg.pool_pa+(uint64_t)p*4096+4095);
                    CHECK((desc&LF_ARM_LPAE_PTE_ADDR_MASK)==f.cfg.pool_pa+(uint64_t)p*4096);
                    CHECK((desc&LF_ARM_LPAE_PTE_XN)==LF_ARM_LPAE_PTE_XN);
                }
                uint64_t pa=31,desc=37;
                CHECK(gs_lookup(&f.d,1ull<<ias,&pa,&desc)==GS_RANGE);
                CHECK(pa==31 && desc==37);
            }
            ++ips;
        }
        Fixture f;f.cfg.iova_bits=48;f.cfg.iova_base=(1ull<<39)-4096;f.cfg.table_capacity=22;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_OK && f.d.tables_used==22);
        for(uint64_t va:std::array<uint64_t,3>{f.cfg.iova_base,(1ull<<39),f.cfg.iova_base+GS_POOL_BYTES-1})
            CHECK(walk(f,va)==f.cfg.pool_pa+va-f.cfg.iova_base);
    });
    test("offline failure and incomplete callback binding leave image unpublished",[]{
        Fixture f;auto original=*f.arena;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_WRITE)==GS_INVALID);
        CHECK(f.d.state==GS_EMPTY && !std::memcmp(&original,f.arena.get(),sizeof(original)));
        f.cfg.table_capacity=17;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_NO_TABLES);
        CHECK(f.d.state==GS_EMPTY && f.events.empty());
        f.cfg.table_capacity=24;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_OK);
        auto image=*f.arena;
        CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_BAD_STATE);
        CHECK(gs_bind_callbacks(&f.d,nullptr)==GS_INVALID);
        for(unsigned missing=0;missing<7;++missing) {
            auto cb=f.cb();
            switch(missing) {
            case 0:cb.begin=nullptr;break;case 1:cb.data_sync=nullptr;break;
            case 2:cb.table_clean=nullptr;break;case 3:cb.write_barrier=nullptr;break;
            case 4:cb.invalidate_context=nullptr;break;case 5:cb.tlb_sync=nullptr;break;
            default:cb.end=nullptr;break;
            }
            CHECK(gs_bind_callbacks(&f.d,&cb)==GS_INVALID);
            CHECK(gs_publish(&f.d)==GS_BAD_STATE);
        }
        CHECK(f.events.empty() && f.d.state==GS_OFFLINE);
        CHECK(!std::memcmp(&image,f.arena.get(),sizeof(image)));
    });
    test("prebuilt pool begin/data errors retry before table visibility",[]{
        for(char fail:{'B','D'}) {
            Fixture f;CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_OK);
            auto cb=f.cb();CHECK(gs_bind_callbacks(&f.d,&cb)==GS_OK);f.fail=fail;
            auto image=*f.arena;
            CHECK(gs_publish(&f.d)==GS_CALLBACK && f.d.state==GS_OFFLINE);
            CHECK(f.sequence()==(fail=='B'?"B":"BDA") && f.d.mapped_pages==8192);
            CHECK(!std::memcmp(&image,f.arena.get(),sizeof(image)));
            for(auto word:f.d.physical_used) CHECK(word==UINT64_MAX);
            f.fail=0;f.events.clear();CHECK(gs_publish(&f.d)==GS_OK);
            CHECK(f.sequence()==std::string("BD")+std::string(18,'C')+"WISE");
        }
    });
    test("prebuilt first/last table clean and TLB failures quarantine entire pool",[]{
        for(char fail:{'C','c','W','I','S'}) {
            Fixture f;CHECK(gs_build_pool(&f.d,f.arena.get(),&f.cfg,GS_READ)==GS_OK);
            auto cb=f.cb();CHECK(gs_bind_callbacks(&f.d,&cb)==GS_OK);
            f.fail=fail=='c'?'C':fail;f.fail_after=fail=='c'?f.d.tables_used-1:0;
            CHECK(gs_publish(&f.d)==GS_POISONED && f.d.state==GS_QUARANTINED);
            CHECK(f.events.back().type=='A' && f.d.mapped_pages==8192);
            for(auto word:f.d.physical_used) CHECK(word==UINT64_MAX);
            CHECK(gs_bind_callbacks(&f.d,&cb)==GS_POISONED);
            CHECK(gs_map_pool(&f.d,GS_READ)==GS_POISONED);
            CHECK(gs_unmap(&f.d,f.cfg.iova_base,8192)==GS_POISONED);
            CHECK(gs_publish(&f.d)==GS_POISONED);
            struct gs_table_view view{};CHECK(gs_table_view(&f.d,0,&view)==GS_OK);
            uint64_t pa=0,desc=0;CHECK(gs_lookup(&f.d,f.cfg.iova_base,&pa,&desc)==GS_OK);
            CHECK(pa==f.cfg.pool_pa && view.cpu==f.arena->pte[0]);
        }
    });
    test("fault bits independently extracted from pinned Linux including observed SID0",[]{
        struct FaultBit {uint32_t bit;uint8_t gs_fault::*field;};
        const FaultBit fsr_bits[]={
            {LF_ARM_SMMU_CB_FSR_TF,&gs_fault::translation},
            {LF_ARM_SMMU_CB_FSR_AFF,&gs_fault::access_flag},
            {LF_ARM_SMMU_CB_FSR_PF,&gs_fault::permission},
            {LF_ARM_SMMU_CB_FSR_EF,&gs_fault::external},
            {LF_ARM_SMMU_CB_FSR_TLBMCF,&gs_fault::tlb_multiconflict},
            {LF_ARM_SMMU_CB_FSR_TLBLKF,&gs_fault::tlb_lock},
            {LF_ARM_SMMU_CB_FSR_ASF,&gs_fault::address_size},
            {LF_ARM_SMMU_CB_FSR_UUT,&gs_fault::unsupported_upstream},
            {LF_ARM_SMMU_CB_FSR_SS,&gs_fault::stalled},
            {LF_ARM_SMMU_CB_FSR_MULTI,&gs_fault::multiple}};
        for(const auto& bit:fsr_bits) {
            gs_fault f{};gs_fault_decode(bit.bit,0,0,0,0,&f);
            CHECK(f.*(bit.field)==1 && f.has_fault && !f.unknown_fsr);
            for(const auto& other:fsr_bits) if(other.bit!=bit.bit) CHECK(f.*(other.field)==0);
        }
        const FaultBit syn_bits[]={
            {LF_ARM_SMMU_CB_FSYNR0_WNR,&gs_fault::write},
            {LF_ARM_SMMU_CB_FSYNR0_PNU,&gs_fault::privileged},
            {LF_ARM_SMMU_CB_FSYNR0_IND,&gs_fault::instruction},
            {LF_ARM_SMMU_CB_FSYNR0_NSATTR,&gs_fault::nonsecure},
            {LF_ARM_SMMU_CB_FSYNR0_PTWF,&gs_fault::table_walk},
            {LF_ARM_SMMU_CB_FSYNR0_AFR,&gs_fault::afr}};
        for(const auto& bit:syn_bits) {
            gs_fault f{};gs_fault_decode(0,bit.bit,0,0,0,&f);
            CHECK(f.*(bit.field)==1 && !f.has_fault && !f.sid);
            for(const auto& other:syn_bits) if(other.bit!=bit.bit) CHECK(f.*(other.field)==0);
        }
        for(unsigned b=0;b<32;++b) {
            gs_fault f{};uint32_t raw=UINT32_C(1)<<b;
            gs_fault_decode(raw,raw,raw,raw,UINT64_MAX,&f);
            CHECK(f.has_fault==!!(raw&LF_ARM_SMMU_CB_FSR_FAULT));
            CHECK(f.unknown_fsr==(raw&~(LF_ARM_SMMU_CB_FSR_FAULT|LF_ARM_SMMU_CB_FSR_FORMAT)));
            CHECK(f.level==(raw&LF_ARM_SMMU_CB_FSYNR0_PLVL));
            CHECK(f.s1_context==((raw&LF_ARM_SMMU_CB_FSYNR0_S1CBNDX)>>16));
            CHECK(f.sid==(raw&LF_ARM_SMMU_CBFRSYNRA_SID) && f.far==UINT64_MAX);
        }
    });
    test("literal FSR/FSYNR fields: no IRQ/TZ or SID allocation side effects",[]{
        gs_fault f{};
        gs_fault_decode(0xc00001fe,0x005a0d73,0xabcdef01,0xffff317,0x123456789,&f);
        CHECK(f.translation && f.permission && f.access_flag && f.external && f.address_size);
        CHECK(f.tlb_multiconflict && f.tlb_lock && f.unsupported_upstream && f.multiple && f.stalled);
        CHECK(f.write && f.privileged && f.instruction && f.nonsecure && f.table_walk && f.afr);
        CHECK(f.level==3 && f.s1_context==0x5a && f.sid==0xf317 && f.far==0x123456789);
        CHECK(f.fsynr1==0xabcdef01 && f.has_fault && !f.unknown_fsr);
        gs_fault_decode(1u<<0,0,0,0,0,&f);CHECK(f.unknown_fsr==1 && !f.has_fault && f.sid==0);
        for(unsigned b=1;b<=8;++b) {gs_fault_decode(1u<<b,0,0,0,0,&f);CHECK(f.has_fault && f.fsr==(1u<<b));}
        gs_fault_decode(3u<<9,0,0,0,0,&f);CHECK(f.format==3 && !f.has_fault);
    });
    std::printf("PASS G3: %u cases, %u checks; real 4KiB LPAE; 32MiB; host-only/no MMIO\n",cases,checks);
    return 0;
} catch(const std::exception& e) {
    std::fprintf(stderr,"FAIL G3 after %u cases/%u checks: %s\n",cases,checks,e.what());return 1;
}
