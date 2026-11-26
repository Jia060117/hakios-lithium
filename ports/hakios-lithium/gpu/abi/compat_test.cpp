/* Link the two ORIGINAL metal .cpp files against host fixtures. No device API.
 * IOKit names are intercepted by local test_shims; tokens aren't Darwin values.
 */
#include "hakios-metal/gpu.hpp"
#include "validator.hpp"
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>

using namespace hakios::metal;
using hakios::gpu_abi::Status;
static unsigned checks, cases;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(#x); } while (0)
template<class F> void test(const char* name, F f) {
    f(); ++cases; std::printf("PASS %s\n", name);
}
static_assert(sizeof(MsmGemSubmit) == sizeof(HakiosGpuSubmit), "original header size");
static_assert(offsetof(MsmGemSubmit, queueid) == offsetof(HakiosGpuSubmit, queueid), "original queueid");
static_assert(sizeof(MsmGemCpuFini) == sizeof(HakiosGpuCpuFini), "original fini");

struct Mock : Transport {
    std::function<int(uint32_t, void*, size_t)> inspect;
    std::vector<uint32_t> calls;
    uint32_t fail_request = 0;
    int fail_error = EINVAL;
    uint64_t requested_size = 0;
    uint32_t requested_flags = 0, mapped_handle = 0;
    uint64_t mapped_size = 0;
    unsigned maps = 0, unmaps = 0;
    bool map_failure = false;
    std::array<uint8_t, 0x4000> bytes{};
    int64_t now = 2000000007;
    int call(uint32_t r, void* data, size_t n) override {
        calls.push_back(r);
        if (r == fail_request) return fail_error;
        if (inspect) { int e = inspect(r,data,n); if (e) return e; }
        switch (r) {
        case kGemNew: {
            CHECK(n == sizeof(MsmGemNew)); auto& s = *static_cast<MsmGemNew*>(data);
            requested_size = s.size; requested_flags = s.flags; s.handle = 7; break;
        }
        case kGemInfo: {
            CHECK(n == sizeof(MsmGemInfo)); auto& s = *static_cast<MsmGemInfo*>(data);
            CHECK(s.handle == 7); CHECK(s.info == kInfoGetIova); s.value = 0x12345000; break;
        }
        case kHakiosSurface: {
            CHECK(n == sizeof(HakiosSurface)); auto& s = *static_cast<HakiosSurface*>(data);
            s.handle=7; s.size=0x4000; s.offset=64; s.rowBytes=256; s.width=64; s.height=32;
            s.pixelFormat=0x42475241; s.planes=1; break;
        }
        default: break;
        }
        return 0;
    }
    void* map(uint32_t h, uint64_t s) override {
        ++maps; mapped_handle=h; mapped_size=s; return map_failure ? nullptr : bytes.data();
    }
    void unmap(uint32_t h, void* p) override { CHECK(h==7); CHECK(p==bytes.data()); ++unmaps; }
    int64_t nowNs() override { return now; }
};
static std::unique_ptr<Mock> mock() { return std::make_unique<Mock>(); }
static std::unique_ptr<Bo> make_bo(Gpu& g) {
    int e=-1; auto b=g.newBo(1,kBoWriteCombined,&e); CHECK(e==0 && b); return b;
}

struct Kit {
    bool present=true, shorten=false;
    int open_status=0, call_status=0, map_status=0;
    unsigned closed=0, released=0, unmapped=0;
    uint32_t opened_type=999, request=0, map_type=0, map_options=0;
    size_t called_bytes=0;
    uint64_t map_bytes=3; // Deliberately not the caller's requested size.
    uint64_t map_address=0x90000000;
    const char* matched=nullptr;
} kit;
extern "C" {
const char* IOServiceMatching(const char* name) { kit.matched=name; return name; }
io_service_t IOServiceGetMatchingService(uint32_t, const char*) { return kit.present?1:0; }
kern_return_t IOServiceOpen(io_service_t, uint32_t, uint32_t type, io_connect_t* out) {
    kit.opened_type=type; *out=9; return kit.open_status;
}
kern_return_t IOObjectRelease(io_service_t) { ++kit.released; return 0; }
kern_return_t IOServiceClose(io_connect_t c) { CHECK(c==9); ++kit.closed; return 0; }
uint32_t mach_task_self() { return 22; }
int mach_timebase_info(mach_timebase_info_data_t* t) { t->numer=3; t->denom=2; return 0; }
uint64_t mach_continuous_time() { return 100; }
kern_return_t IOConnectCallStructMethod(io_connect_t c, uint32_t r, const void* in, size_t n, void* out, size_t* size) {
    CHECK(c==9); CHECK(in==out); CHECK(n==*size);
    kit.request=r; kit.called_bytes=n;
    if (kit.shorten) *size=0;
    return kit.call_status;
}
kern_return_t IOConnectMapMemory64(io_connect_t c, uint32_t h, uint32_t task,
                                  mach_vm_address_t* address, mach_vm_size_t* size, uint32_t options) {
    CHECK(c==9 && task==22); CHECK(*address==0 && *size==0);
    kit.map_type=h; kit.map_options=options;
    *address=kit.map_address; *size=kit.map_bytes;
    return kit.map_status;
}
kern_return_t IOConnectUnmapMemory64(io_connect_t c, uint32_t h, uint32_t task, mach_vm_address_t address) {
    CHECK(c==9 && h==7 && task==22 && address==kit.map_address);
    ++kit.unmapped; return kIOReturnBadArgument; // Original ignores unmap status.
}
}

int main() try {
    test("canonical records reject null/short/oversize; valid execution unsupported", [] {
        std::array<uint8_t,72> bytes{};
        const uint32_t selectors[]={0x09,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x4a,0x4b,0x4c,0x7d,0x7e,0x7f};
        for (auto s:selectors) {
            auto n=hakios_gpu_record_size(s);
            CHECK(n>0 && n<=72);
            CHECK(hakios::gpu_abi::validate_record(s,bytes.data(),n).status==Status::shape_valid);
            CHECK(hakios::gpu_abi::validate_record(s,nullptr,n).status==Status::malformed);
            CHECK(hakios::gpu_abi::validate_record(s,bytes.data(),n-1).status==Status::malformed);
            CHECK(hakios::gpu_abi::validate_record(s,bytes.data(),n+1).status==Status::malformed);
            CHECK(hakios::gpu_abi::execute(s,bytes.data(),n).status==Status::unsupported);
        }
        CHECK(hakios::gpu_abi::execute(0xdead,bytes.data(),72).status==Status::unknown_selector);
    });
    test("resolved submit views reject invalid shape; A530 never issues a fence", [] {
        HakiosGpuSubmit s{}; s.nr_bos=1; s.nr_cmds=1; s.bos=1; s.cmds=1;
        // Invalid wire addresses are intentionally NOT dereferenced by checker.
        HakiosGpuSubmitBo bo{1,7,0x12345000};
        HakiosGpuSubmitCmd c{1,0,0,16,0,0,0};
        uint64_t size=0x4000; uint32_t fence=0xfeedcafe;
        auto validate=[&]{return hakios::gpu_abi::validate_submit(s,&bo,1,&c,1,&size).status;};
        CHECK(validate()==Status::shape_valid);
        CHECK(hakios::gpu_abi::a530_submit(s,&bo,1,&c,1,&size,&fence).status==Status::unsupported);
        CHECK(fence==0xfeedcafe);
        c.submit_offset=2; CHECK(validate()==Status::malformed); c.submit_offset=0;
        c.size=3; CHECK(validate()==Status::malformed); c.size=16;
        c.nr_relocs=1; CHECK(validate()==Status::malformed); c.nr_relocs=0;
        c.submit_idx=1; CHECK(validate()==Status::malformed); c.submit_idx=0;
        c.submit_offset=0x4000; CHECK(validate()==Status::malformed); c.submit_offset=0;
        c.type=2; CHECK(validate()==Status::malformed); c.type=1;
        bo.handle=0; CHECK(validate()==Status::malformed); bo.handle=7;
        s.nr_cmds=0; CHECK(validate()==Status::malformed);
        s.nr_cmds=129; CHECK(validate()==Status::malformed); s.nr_cmds=1;
        s.nr_bos=8193; CHECK(validate()==Status::malformed); s.nr_bos=1;
        CHECK(hakios::gpu_abi::validate_submit(s,nullptr,1,&c,1,&size).status==Status::malformed);
        c.size=0; CHECK(validate()==Status::shape_valid); // Kernel has no positive-size predicate.
    });
    test("original param pipe/id/value and failure sentinel behavior", [] {
        auto m=mock(); auto p=m.get();
        m->inspect=[](uint32_t r,void* data,size_t n){
            CHECK(r==kGetParam && n==24); auto& a=*static_cast<MsmParam*>(data);
            CHECK(a.pipe==1 && a.len==0 && a.pad==0); a.value=0x1234567806030500ull; return 0;
        };
        Gpu g(std::move(m)); uint64_t v=0; CHECK(g.param(kParamChipId,&v)==0);
        CHECK(v==0x1234567806030500ull); CHECK(g.chipId()==0x06030500);
        p->fail_request=kGetParam; v=99; CHECK(g.param(kParamFaults,&v)==EINVAL && v==99);
        CHECK(g.chipId()==0 && g.faults()==0);
    });
    test("original GEMnew 16KiB rounding, IOVA, map and owned cleanup", [] {
        auto m=mock(); auto p=m.get(); Gpu g(std::move(m));
        auto b=make_bo(g); CHECK(p->requested_size==0x4000 && p->requested_flags==kBoWriteCombined);
        CHECK(b->handle()==7 && b->size()==0x4000 && b->gpuAddress()==0x12345000);
        CHECK(p->mapped_handle==7 && p->mapped_size==0x4000 && g.allocatedBytes()==0x4000);
        b.reset(); CHECK(g.allocatedBytes()==0 && p->unmaps==1);
        CHECK(p->calls.back()==kGemClose);
    });
    test("original zero/new/info/map failure cleanup", [] {
        for (int stage=0;stage<4;++stage) {
            auto m=mock(); auto p=m.get(); Gpu g(std::move(m)); int e=0;
            if (stage==0) { CHECK(!g.newBo(0,0,&e) && e==EINVAL && p->calls.empty()); continue; }
            if (stage==1) { p->fail_request=kGemNew; p->fail_error=ENOMEM; }
            if (stage==2) p->fail_request=kGemInfo;
            if (stage==3) p->map_failure=true;
            CHECK(!g.newBo(1,0,&e)); CHECK(e==(stage==2?EINVAL:ENOMEM)); CHECK(g.allocatedBytes()==0);
            if (stage==1) CHECK(p->calls.size()==1);
            else CHECK(p->calls.back()==kGemClose);
        }
    });
    test("original surface metadata, nullable CPU mapping, disown avoids close", [] {
        auto m=mock(); auto p=m.get(); Gpu g(std::move(m)); SurfaceInfo info{}; int e=0;
        auto b=g.surfaceBo(42,&info,&e); CHECK(e==0 && b && info.offset==64 && info.width==64);
        CHECK(info.rowBytes==256 && info.height==32 && info.planes==1 && info.pixelFormat==0x42475241);
        CHECK(g.allocatedBytes()==0); auto before=p->calls.size(); g.disown(std::move(b));
        CHECK(p->unmaps==1 && p->calls.size()==before);
        p->map_failure=true; b=g.surfaceBo(42,&info,&e); CHECK(e==0 && b && !b->cpu());
    });
    test("original queue id/prio and close-zero suppression", [] {
        auto m=mock(); auto p=m.get();
        m->inspect=[](uint32_t r,void* d,size_t n){
            if (r==kSubmitQueueNew) { CHECK(n==12); auto& q=*static_cast<MsmSubmitQueue*>(d);
                CHECK(q.flags==0 && q.prio==3); q.id=9; }
            if (r==kSubmitQueueClose) CHECK(n==4 && *static_cast<uint32_t*>(d)==9);
            return 0;
        };
        Gpu g(std::move(m)); uint32_t id=0; CHECK(g.newQueue(3,&id)==0 && id==9);
        g.closeQueue(0); CHECK(p->calls.size()==1); g.closeQueue(9); CHECK(p->calls.size()==2);
    });
    test("original submit array wire fields, duplicate BO merge and fence failure", [] {
        auto m=mock(); auto p=m.get(); Gpu g(std::move(m)); auto b=make_bo(g);
        // Distinct wrappers of the same handle exercise the duplicate-surface path.
        p->inspect=[&](uint32_t r,void* d,size_t n){
            if (r!=kGemSubmit) return 0;
            CHECK(n==72); auto& s=*static_cast<MsmGemSubmit*>(d);
            CHECK(s.flags==1 && s.queueid==9 && s.nr_cmds==1 && s.nr_bos==1 && s.fence_fd==-1);
            CHECK(!s.in_syncobjs && !s.out_syncobjs && !s.nr_in_syncobjs && !s.nr_out_syncobjs);
            auto bos=reinterpret_cast<const MsmSubmitBo*>(uintptr_t(s.bos));
            auto cmd=reinterpret_cast<const MsmSubmitCmd*>(uintptr_t(s.cmds));
            CHECK(bos[0].handle==7 && bos[0].flags==3 && bos[0].presumed==0x12345000);
            CHECK(cmd->type==1 && cmd->submit_idx==0 && cmd->submit_offset==4 && cmd->size==16 && !cmd->nr_relocs);
            s.fence=123; return 0;
        };
        auto duplicate=make_bo(g);
        uint32_t fence=99;
        CHECK(g.submit(9,*b,4,4,{{b.get(),true},{duplicate.get(),false},{duplicate.get(),true}},&fence)==0 && fence==123);
        auto before=p->calls.size();
        CHECK(g.submit(9,*b,0,0,{},&fence)==EINVAL);
        CHECK(g.submit(9,*b,2,4,{},&fence)==EINVAL);
        CHECK(g.submit(9,*b,0x4000,1,{},&fence)==EINVAL); CHECK(p->calls.size()==before);
        p->fail_request=kGemSubmit; p->fail_error=ETIMEDOUT; fence=99;
        CHECK(g.submit(9,*b,0,1,{},&fence)==ETIMEDOUT && fence==99);
    });
    test("original wait absolute deadline and CPU prep/fini error forwarding", [] {
        auto m=mock(); auto p=m.get(); Gpu g(std::move(m)); auto b=make_bo(g);
        p->inspect=[](uint32_t r,void* d,size_t n){
            if (r==kWaitFence) {
                CHECK(n==32); auto& w=*static_cast<MsmWaitFence*>(d);
                CHECK(w.fence==5 && w.queueid==9 && w.flags==0);
                CHECK(w.timeout.seconds==2 && w.timeout.nanoseconds==7);
            }
            if (r==kGemCpuPrep) {
                CHECK(n==24); auto& q=*static_cast<MsmGemCpuPrep*>(d);
                CHECK(q.handle==7 && q.op==4 && q.timeout.seconds==3 && q.timeout.nanoseconds==12);
            }
            if (r==kGemCpuFini) CHECK(n==4 && static_cast<MsmGemCpuFini*>(d)->handle==7);
            return 0;
        };
        CHECK(g.wait(9,5,-100)==0); CHECK(g.cpuPrep(*b,4,1000000005)==0);
        p->fail_request=kGemCpuPrep; p->fail_error=EBUSY; CHECK(g.cpuPrep(*b,4,0)==EBUSY);
        p->fail_request=kGemCpuFini; p->fail_error=EIO; g.cpuFini(*b); // Return intentionally discarded.
        p->fail_request=0; g.cpuFini(*b);
    });
    test("original IOKit no-service and failed-open lifecycle", [] {
        kit=Kit{}; kit.present=false; std::string why;
        CHECK(!openKernelGpu(&why) && why.find("no HakiosGPU")!=std::string::npos);
        CHECK(kit.released==0 && kit.closed==0);
        kit=Kit{}; kit.open_status=kIOReturnBusy;
        CHECK(!openKernelGpu(&why) && why.find("would not open")!=std::string::npos);
        CHECK(kit.released==1 && kit.closed==0 && kit.opened_type==0);
    });
    test("original IOKit symbolic returns including NotReady/VMError default EIO", [] {
        kit=Kit{}; std::string why;
        {
            auto t=openKernelGpu(&why); CHECK(t && std::strcmp(kit.matched,"HakiosGPU")==0);
            const int input[]={KERN_SUCCESS,kIOReturnNoMemory,kIOReturnBadArgument,kIOReturnTimeout,
                               kIOReturnBusy,kIOReturnNotFound,kIOReturnUnsupported,kIOReturnNotReady,kIOReturnVMError,999};
            const int expected[]={0,ENOMEM,EINVAL,ETIMEDOUT,EBUSY,ENOENT,ENOSYS,EIO,EIO,EIO};
            MsmParam param{};
            for (unsigned i=0;i<10;++i) { kit.call_status=input[i]; CHECK(t->call(kGetParam,&param,24)==expected[i]); }
            CHECK(kit.request==kGetParam && kit.called_bytes==24 && t->nowNs()==150);
            kit.call_status=0; kit.shorten=true; CHECK(t->call(kGetParam,&param,24)==0);
        }
        CHECK(kit.closed==1 && kit.released==1);
    });
    test("original IOKit map uses handle/cache default and ignores returned length", [] {
        kit=Kit{}; std::string why; auto t=openKernelGpu(&why); CHECK(bool(t));
        void* p=t->map(7,0x4000);
        CHECK(uintptr_t(p)==kit.map_address && kit.map_type==7 && kit.map_options==kIOMapAnywhere);
        CHECK(kit.map_bytes==3); t->unmap(7,p); CHECK(kit.unmapped==1);
        kit.map_status=kIOReturnNoMemory; CHECK(t->map(7,0x4000)==nullptr);
    });
    std::printf("PASS compat: %u cases, %u checks; original metal gpu.cpp + iokit_transport.cpp; A530 submit UNSUPPORTED\n",cases,checks);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr,"FAIL after %u cases/%u checks: %s\n",cases,checks,e.what());
    return 1;
}
