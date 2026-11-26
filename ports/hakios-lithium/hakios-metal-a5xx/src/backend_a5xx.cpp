// SPDX-License-Identifier: MIT
// Composition only: generation-specific modules own packet/layout decisions.
// Fixed restore is fd5_emit.c:905-1135 (same A530 subset as render_a5xx).
#include "hakios-metal/backend_a5xx.hpp"
#include "hakios-metal/command.hpp" // Buffer data type only, never CommandQueue
#include "hakios-metal/transfer_a5xx.hpp"
#include "hakios-metal/a5xx.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>

namespace hakios::metal::backend_a5xx {
namespace {
bool fail(std::string* why, const std::string& text) {
    if (why) *why = "A5xx backend: " + text;
    return false;
}
bool success(std::string* why) { if (why) why->clear(); return true; }
bool sameOwner(const std::shared_ptr<const void>& a, const std::shared_ptr<const void>& b) {
    return a && b && !a.owner_before(b) && !b.owner_before(a);
}
bool valid(const BufferRef& r, const Memory& m, std::string* why) {
    constexpr uint64_t limit = uint64_t(1) << 49;
    if (!r.buffer || !sameOwner(r.backingOwner,m.backingOwner()) || !r.sizeBytes ||
        r.buffer->length() > r.buffer->bo().size() || r.offset > r.buffer->length() ||
        r.sizeBytes > r.buffer->length() - r.offset || !r.buffer->gpuAddress() ||
        r.buffer->gpuAddress() >= limit || r.buffer->length() > limit-r.buffer->gpuAddress())
        return fail(why,"buffer is foreign or outside its logical allocation/A530 address space");
    return true;
}
bool overlap(const Buffer& a, const Buffer& b) {
    return a.bo().handle()==b.bo().handle() ||
        (a.gpuAddress()<b.gpuAddress()+b.bo().size() && b.gpuAddress()<a.gpuAddress()+a.bo().size());
}
bool colorFormat(uint32_t f) { return f==70 || f==80; }
BufferRef allocation(Memory& m, uint64_t bytes, std::string* why) {
    if (!bytes || bytes > 256ull*1024*1024 || bytes>SIZE_MAX) {
        fail(why,"allocation outside bounded logical byte range"); return {};
    }
    auto b=m.allocate(bytes,why);
    BufferRef r{m.backingOwner(),b,0,bytes};
    if (!b || b->length()!=bytes || !b->contents() || !valid(r,m,why)) return {};
    return r;
}
Resident resident(const BufferRef& r, bool write) { return {r.backingOwner,r.buffer,write}; }
bool textureValid(const Texture& t, const Memory& m, std::string* why) {
    if (!valid(t.storage,m,why) || t.storage.offset || t.storage.sizeBytes!=t.layout.layerBytes)
        return fail(why,"texture must own one full padded linear level");
    std::array<uint32_t,12> descriptor{};
    return a5xx::textureDescriptor(t.layout,t.storage.buffer->gpuAddress(),t.storage.sizeBytes,&descriptor,why);
}
bool targetOf(const Texture& t, const Memory& m, render_a5xx::ColorTarget* out, std::string* why) {
    if (!textureValid(t,m,why)) return false;
    if (!(t.usage&4) || !colorFormat(uint32_t(t.layout.format)))
        return fail(why,"draw needs RGBA8/BGRA8 render-target usage");
    const auto& l=t.layout;
    *out={t.storage.buffer->gpuAddress(),t.storage.buffer->bo().size(),l.width,l.height,l.pitch,
          l.layerBytes,render_a5xx::ColorFormat(uint32_t(l.format))};
    return render_a5xx::validTarget(*out,why);
}
bool clearBytes(const std::array<double,4>& c, std::array<uint8_t,4>* out, std::string* why) {
    for (unsigned i=0;i<4;++i) {
        if (!std::isfinite(c[i]) || c[i]<0 || c[i]>1)
            return fail(why,"clear color must be finite UNORM in [0,1]");
        (*out)[i]=uint8_t(std::floor(c[i]*255.0+0.5));
    }
    return true;
}
// Strict list of commands emitted by these A5xx modules. CP_MEMCPY and gen6
// LOAD_STATE/WAIT_REG_MEM/SYNC opcodes cannot enter through this facade.
bool a530Packets(const pm4::Stream& stream, std::string* why) {
    using namespace a5xx;
    for (size_t i=0;i<stream.size();) {
        auto h=hakios::metal::pm4::decode(stream.words()[i]);
        if (h.kind==hakios::metal::pm4::Header::kBad || h.count>stream.size()-i-1)
            return fail(why,"bad packet boundary/parity");
        if (h.kind==hakios::metal::pm4::Header::kCommand) {
            switch (h.opcode) {
            case CP_WAIT_FOR_IDLE: case CP_WAIT_MEM_WRITES: case CP_WAIT_FOR_ME:
            case CP_MEM_WRITE: case CP_MEM_TO_MEM: case CP_LOAD_STATE4:
            case CP_EXEC_CS: case CP_EVENT_WRITE: case CP_SET_RENDER_MODE:
            case CP_SET_DRAW_STATE: case CP_SKIP_IB2_ENABLE_GLOBAL:
            case CP_SET_VISIBILITY_OVERRIDE: case CP_DRAW_INDX_OFFSET: break;
            default: return fail(why,"opcode outside supported A530 modules");
            }
        }
        i+=size_t(h.count)+1;
    }
    return true;
}
void reg(pm4::Stream& s,uint32_t r,uint32_t v) { s.registers(r,&v,1); }
template<size_t N> void regs(pm4::Stream& s,uint32_t r,const uint32_t (&v)[N]) { s.registers(r,v,uint32_t(N)); }
template<size_t N> void cmd(pm4::Stream& s,uint32_t op,const uint32_t (&v)[N]) { s.packet(op,v,uint32_t(N)); }
void zeros(pm4::Stream& s,uint32_t r,uint32_t count) { const uint32_t z[16]{}; s.registers(r,z,count); }

// Kept local because render_a5xx's restore helper is intentionally private.
// No old CommandBuffer::dispatch (A6xx) and no fabricated color pass is used
// to initialize compute. Exact source-backed A530 restore is inserted below.
void restore(pm4::Stream& s) {
    using namespace a5xx;
    const uint32_t bypass[]{CP_SET_RENDER_MODE_0_MODE(BYPASS),0,0,0,0};
    cmd(s,CP_SET_RENDER_MODE,bypass);
    const uint32_t invalidate[]{0,0,0,0,0x12};
    regs(s,REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_LO,invalidate);
    s.waitForIdle();
    reg(s, REG_A5XX_HLSQ_UPDATE_CNTL, 0xfffff);
    reg(s, REG_A5XX_PC_RESTART_INDEX, 0xffffffff);
    reg(s, REG_A5XX_PC_RASTER_CNTL, 0x12);
    const uint32_t point[]{A5XX_GRAS_SU_POINT_MINMAX_MIN(1.0f) |
                           A5XX_GRAS_SU_POINT_MINMAX_MAX(4092.0f),
                           A5XX_GRAS_SU_POINT_SIZE(0.5f)};
    regs(s, REG_A5XX_GRAS_SU_POINT_MINMAX, point);
    reg(s, REG_A5XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_SCREEN_SCISSOR_CNTL, 0);
    reg(s, REG_A5XX_SP_VS_CONFIG_MAX_CONST, 0);
    reg(s, REG_A5XX_SP_FS_CONFIG_MAX_CONST, 0);
    zeros(s, REG_A5XX_UNKNOWN_E292, 2);
    reg(s, REG_A5XX_RB_MODE_CNTL, 0x44);
    reg(s, REG_A5XX_RB_DBG_ECO_CNTL, 0x00100000);
    reg(s, REG_A5XX_VFD_MODE_CNTL, 0);
    reg(s, REG_A5XX_PC_MODE_CNTL, 0x1f);
    reg(s, REG_A5XX_SP_MODE_CNTL, 0x1e);
    reg(s, REG_A5XX_SP_DBG_ECO_CNTL, 0x40000800);
    reg(s, REG_A5XX_TPL1_MODE_CNTL, 0x544);
    const uint32_t timeout[]{0x80, 0};
    regs(s, REG_A5XX_HLSQ_TIMEOUT_THRESHOLD_0, timeout);
    reg(s, REG_A5XX_VPC_DBG_ECO_CNTL, 0x400);
    reg(s, REG_A5XX_HLSQ_MODE_CNTL, 1);
    reg(s, REG_A5XX_VPC_MODE_CNTL, 0);
    const uint32_t drawState[]{CP_SET_DRAW_STATE_0_DISABLE_ALL_GROUPS, 0, 0};
    cmd(s, CP_SET_DRAW_STATE, drawState);
    reg(s, REG_A5XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_BIN_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_BIN_CNTL, 0);
    reg(s, REG_A5XX_VPC_FS_PRIMITIVEID_CNTL, 0xff);
    reg(s, REG_A5XX_VPC_SO_OVERRIDE, A5XX_VPC_SO_OVERRIDE_SO_DISABLE);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_BASE_LO(0), 3);
    zeros(s, REG_A5XX_VPC_SO_FLUSH_BASE_LO(0), 2);
    reg(s, REG_A5XX_PC_GS_PARAM, 0);
    reg(s, REG_A5XX_PC_HS_PARAM, 0);
    reg(s, REG_A5XX_TPL1_TP_FS_ROTATION_CNTL, 0);
    reg(s, REG_A5XX_UNKNOWN_E004, 0);
    reg(s, REG_A5XX_GRAS_SU_LAYERED, 0);
    reg(s, REG_A5XX_VPC_SO_BUF_CNTL, 0);
    reg(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(0), 0);
    reg(s, REG_A5XX_PC_GS_LAYERED, 0);
    reg(s, REG_A5XX_UNKNOWN_E5AB, 0);
    reg(s, REG_A5XX_UNKNOWN_E5C2, 0);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_BASE_LO(1), 3);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(1), 6);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(2), 6);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(3), 3);
    reg(s, REG_A5XX_UNKNOWN_E5DB, 0);
    reg(s, REG_A5XX_SP_HS_CTRL_REG0, 0);
    reg(s, REG_A5XX_SP_GS_CTRL_REG0, 0);
    zeros(s, REG_A5XX_TPL1_VS_TEX_COUNT, 4);
    zeros(s, REG_A5XX_TPL1_FS_TEX_COUNT, 2);
    for (uint32_t index : {REG_A5XX_UNKNOWN_E7C0, REG_A5XX_UNKNOWN_E7C5,
                          REG_A5XX_UNKNOWN_E7CA, REG_A5XX_UNKNOWN_E7CF,
                          REG_A5XX_UNKNOWN_E7D4, REG_A5XX_UNKNOWN_E7D9})
        zeros(s, index, 3);
    reg(s, REG_A5XX_RB_CLEAR_CNTL, 0);
}


int fini(Gpu& gpu,const Bo& bo) {
    MsmGemCpuFini f{bo.handle()};
    return gpu.transport().call(kGemCpuFini,&f,sizeof(f));
}
struct Quarantined { std::shared_ptr<const void> work; Quarantined* next=nullptr; };
class GpuMemory final : public Memory {
public:
    explicit GpuMemory(std::shared_ptr<Gpu> gpu):gpu_(std::move(gpu)) {}
    std::shared_ptr<const void> backingOwner() const override { return gpu_; }
    Gpu* gpu() const noexcept override { return gpu_.get(); }
    bool canSubmit() const noexcept override { return !blocked_; }
    std::shared_ptr<Buffer> allocate(uint64_t bytes,std::string* why) override {
        int e=0; auto bo=gpu_->newBo(bytes,kBoWriteCombined,&e);
        if (!bo) { fail(why,"GEM allocation: "+std::to_string(e)); return {}; }
        return std::make_shared<Buffer>(std::move(bo),bytes);
    }
    bool snapshot(const Buffer& b,uint64_t offset,uint64_t bytes,void* destination,std::string* why) override {
        if (!destination || !b.contents() || offset>b.length() || bytes>b.length()-offset || bytes>SIZE_MAX)
            return fail(why,"CPU snapshot range invalid");
        int e=gpu_->cpuPrep(b.bo(),1,100000000); // MSM_PREP_READ
        if (e) return fail(why,"CPU read prep: "+std::to_string(e));
        if (bytes) std::memcpy(destination,static_cast<const uint8_t*>(b.contents())+offset,size_t(bytes));
        e=fini(*gpu_,b.bo());
        return !e ? success(why) : fail(why,"CPU read fini: "+std::to_string(e));
    }
    bool upload(const Buffer& b,const void* data,uint64_t bytes,std::string* why) override {
        if (!data || !b.contents() || bytes>b.length() || bytes>SIZE_MAX)
            return fail(why,"CPU upload range invalid");
        int e=gpu_->cpuPrep(b.bo(),2,100000000); // MSM_PREP_WRITE
        if (e) return fail(why,"CPU write prep: "+std::to_string(e));
        if (bytes) std::memcpy(b.contents(),data,size_t(bytes));
        e=fini(*gpu_,b.bo());
        return !e ? success(why) : fail(why,"CPU write fini: "+std::to_string(e));
    }
    bool publish(const std::vector<Resident>& rs,std::string* why) override {
        for (const auto& r:rs) {
            int e=gpu_->cpuPrep(r.buffer->bo(),2,100000000);
            if (!e) e=fini(*gpu_,r.buffer->bo());
            if (e) return fail(why,"resident CPU publish: "+std::to_string(e));
        }
        return success(why);
    }
    void quarantine(std::shared_ptr<const void> work) noexcept override {
        blocked_=true;
        // Deliberate process-lifetime pin; its destructor must never free DMA
        // pages while the transport's completion/ownership is uncertain.
        static std::mutex mutex;
        static Quarantined* head=nullptr;
        auto* entry=new(std::nothrow) Quarantined{std::move(work),nullptr};
        if (!entry) std::terminate(); // cannot safely release uncertain DMA
        std::lock_guard<std::mutex> lock(mutex); entry->next=head; head=entry;
    }
private:
    std::shared_ptr<Gpu> gpu_;
    std::atomic<bool> blocked_{false};
};
} // namespace

struct Batch::State {
    std::shared_ptr<Memory> memory; // destroyed after residents/BOs
    pm4::Stream stream;
    std::vector<Resident> residents;
    BufferRef commands;
    size_t maxWords;
    Status status=Status::Ready;
    uint32_t queue=0,fence=0;
    uint64_t faults=0;
    bool pinned=false;
    State(std::shared_ptr<Memory> m,size_t max):memory(std::move(m)),maxWords(max) {}
};

std::shared_ptr<Backend> Backend::onGpu(std::shared_ptr<Gpu> gpu,std::string* why) {
    uint64_t chip=0;
    if (!gpu || gpu->param(kParamChipId,&chip) || chip!=kChipId) {
        fail(why,"exact kernel CHIP_ID 0x05030004 required; unknown/A6xx refused"); return {};
    }
    return onMemory(kChipId,std::make_shared<GpuMemory>(std::move(gpu)),why);
}
std::shared_ptr<Backend> Backend::onMemory(uint32_t chip,std::shared_ptr<Memory> memory,std::string* why) {
    if (chip!=kChipId || !memory || !memory->backingOwner()) {
        fail(why,"A530 identity and owning Memory required"); return {};
    }
    success(why); return std::shared_ptr<Backend>(new Backend(std::move(memory)));
}
BufferRef Backend::buffer(uint64_t bytes,std::string* why) {
    return allocation(*memory_,bytes,why);
}
std::shared_ptr<Texture> Backend::texture(uint32_t width,uint32_t height,uint32_t format,uint32_t usage,std::string* why) {
    a5xx::LinearTexture l;
    if ((usage&~5u) || !usage || !a5xx::linearTexture(width,height,a5xx::LinearFormat(format),&l,why)) {
        fail(why,"only 2D linear RGBA8/BGRA8/R32Uint, usage read/render-target supported"); return {};
    }
    if ((usage&4) && !colorFormat(format)) { fail(why,"integer render target unsupported"); return {}; }
    auto b=buffer(l.layerBytes,why); if (!b.buffer) return {};
    auto t=std::make_shared<Texture>(); t->storage=std::move(b); t->layout=l; t->usage=usage;
    if (!textureValid(*t,*memory_,why)) return {};
    success(why); return t;
}
bool Backend::upload(const Texture& t,const void* rows,size_t bytes,size_t pitch,std::string* why) {
    if (!textureValid(t,*memory_,why)) return false;
    std::vector<uint8_t> staging(size_t(t.layout.layerBytes),0);
    if (!a5xx::uploadRows(t.layout,staging.data(),staging.size(),rows,bytes,pitch,why)) return false;
    return memory_->upload(*t.storage.buffer,staging.data(),staging.size(),why);
}
bool Backend::download(const Texture& t,void* rows,size_t bytes,size_t pitch,std::string* why) {
    if (!textureValid(t,*memory_,why)) return false;
    std::vector<uint8_t> staging(size_t(t.layout.layerBytes));
    if (!memory_->snapshot(*t.storage.buffer,0,staging.size(),staging.data(),why)) return false;
    return a5xx::downloadRows(t.layout,rows,bytes,pitch,staging.data(),staging.size(),why);
}
std::shared_ptr<const CompiledKernel> Backend::compileCompute(const void* air,size_t bytes,
    const FunctionConstants& constants,std::string* why) {
    if (!air || !bytes || !hasCompiler()) { fail(why,"compute needs actual AIR compiler/input"); return {}; }
    std::string reason; auto c=compileAir(air,bytes,&reason,constants);
    air_dispatch_a5xx::Metadata metadata;
    if (!c || !air_dispatch_a5xx::metadataFromKernel(c->kernel,&metadata,&reason)) {
        fail(why,reason); return {};
    }
    success(why); return c;
}
std::shared_ptr<Program> Backend::compileProgram(const void* vs,size_t vsBytes,const void* fs,size_t fsBytes,
    uint32_t format,const FunctionConstants& vc,const FunctionConstants& fc,std::string* why) {
    if (!vs || !fs || !vsBytes || !fsBytes || !hasCompiler()) {
        fail(why,"draw needs actual AIR VS and FS; null/depth-only FS unsupported"); return {};
    }
    std::string reason;
    auto v=compileStage(vs,vsBytes,&reason,vc);
    if (!v) { fail(why,reason); return {}; }
    auto f=compileStage(fs,fsBytes,&reason,fc);
    if (!f) { fail(why,reason); return {}; }
    return link(*v,*f,format,why);
}
std::shared_ptr<Program> Backend::link(const CompiledStage& v,const CompiledStage& f,uint32_t format,std::string* why) {
    if (!colorFormat(format) || v.words.empty() || f.words.empty() || v.words.size()>2048 || f.words.size()>2048) {
        fail(why,"bounded float color0 program binaries required"); return {};
    }
    auto vb=buffer(v.words.size()*4,why), fb=buffer(f.words.size()*4,why);
    if (!vb.buffer || !fb.buffer) return {};
    if (overlap(*vb.buffer,*fb.buffer)) { fail(why,"shader allocations alias"); return {}; }
    auto linked=link_a5xx::Program::link(v,f,{{vb.buffer->gpuAddress(),vb.sizeBytes},
        {fb.buffer->gpuAddress(),fb.sizeBytes}},render_a5xx::ColorFormat(format),
        link_a5xx::VaryingInterface::MatchingNames,why);
    if (!linked) return {};
    if (!memory_->upload(*vb.buffer,linked->vertex().words.data(),vb.sizeBytes,why) ||
        !memory_->upload(*fb.buffer,linked->fragment().words.data(),fb.sizeBytes,why)) return {};
    auto p=std::make_shared<Program>(); p->linked=std::move(linked);
    p->vertexImage=std::move(vb); p->fragmentImage=std::move(fb);
    success(why); return p;
}
std::unique_ptr<Batch> Backend::batch(size_t maxWords) {
    if (!maxWords || maxWords>kMaxBatchWords) return {};
    return std::unique_ptr<Batch>(new Batch(memory_,maxWords));
}
Batch::Batch(std::shared_ptr<Memory> memory,size_t max):state_(std::make_shared<State>(std::move(memory),max)) {}
Batch::~Batch() {
    if (state_->status==Status::Submitted && !state_->pinned) {
        state_->pinned=true; state_->memory->quarantine(state_);
    }
}
Batch::Status Batch::status() const noexcept { return state_->status; }
uint32_t Batch::fence() const noexcept { return state_->fence; }
const std::vector<uint32_t>& Batch::words() const noexcept { return state_->stream.words(); }
std::vector<BoUse> Batch::boUses() const {
    std::vector<BoUse> uses;
    for (const auto& r:state_->residents) uses.push_back({&r.buffer->bo(),r.write});
    return uses;
}
bool Batch::append(const pm4::Stream& added,const std::vector<Resident>& residents,std::string* why) {
    auto& s=*state_;
    if (s.status!=Status::Ready) return fail(why,"recording after submit/rejection is forbidden");
    if (s.stream.size()>s.maxWords || added.size()>s.maxWords-s.stream.size() || !a530Packets(added,why))
        return fail(why,"bounded A530 stream append rejected");
    try {
        auto merged=s.stream; auto retained=s.residents;
        for (const auto& r:residents) {
            if (!r.buffer || !sameOwner(r.backingOwner,s.memory->backingOwner())) return fail(why,"foreign resident");
            auto it=std::find_if(retained.begin(),retained.end(),[&](const Resident& a){return a.buffer.get()==r.buffer.get();});
            if (it==retained.end()) retained.push_back(r); else it->write|=r.write;
        }
        for (size_t i=0;i<added.size();) {
            auto h=pm4::decode(added.words()[i]); const auto* payload=added.words().data()+i+1;
            if (h.kind==pm4::Header::kRegisters) merged.registers(h.reg,payload,h.count);
            else merged.packet(h.opcode,payload,h.count);
            i+=size_t(h.count)+1;
        }
        s.stream=std::move(merged); s.residents=std::move(retained); return success(why);
    } catch (const std::bad_alloc&) { return fail(why,"append allocation failed"); }
}
bool Batch::copy(const BufferRef& from,uint64_t fromOffset,const BufferRef& to,uint64_t toOffset,uint64_t bytes,std::string* why) {
    if (state_->status!=Status::Ready || !valid(from,*state_->memory,why) || !valid(to,*state_->memory,why)) return false;
    a5xx::pm4::Stream transfer(state_->maxWords);
    a5xx::TransferEncoder encoder(transfer);
    auto result=encoder.copy({from.buffer->gpuAddress()+from.offset,from.sizeBytes,true,false},fromOffset,
        {to.buffer->gpuAddress()+to.offset,to.sizeBytes,true,true},toOffset,bytes);
    if (!result) return fail(why,a5xx::pm4::errorString(result.error));
    pm4::Stream added;
    for (size_t i=0;i<transfer.size();) {
        auto h=a5xx::pm4::decode(transfer.words()[i]);
        added.packet(h.opcode,transfer.words().data()+i+1,h.count); i+=size_t(h.count)+1;
    }
    return append(added,{resident(from,false),resident(to,true)},why);
}
bool Batch::fill(const BufferRef& to,uint64_t offset,uint64_t bytes,uint8_t value,std::string* why) {
    if (state_->status!=Status::Ready || !valid(to,*state_->memory,why)) return false;
    a5xx::pm4::Stream transfer(state_->maxWords); a5xx::TransferEncoder encoder(transfer);
    auto result=encoder.fill({to.buffer->gpuAddress()+to.offset,to.sizeBytes,true,true},offset,bytes,value);
    if (!result) return fail(why,a5xx::pm4::errorString(result.error));
    pm4::Stream added;
    for (size_t i=0;i<transfer.size();) {
        auto h=a5xx::pm4::decode(transfer.words()[i]);
        added.packet(h.opcode,transfer.words().data()+i+1,h.count); i+=size_t(h.count)+1;
    }
    return append(added,{resident(to,true)},why);
}
bool Batch::dispatch(const CompiledKernel& c,const compute_a5xx::Grid& grid,const std::vector<BufferRef>& args,std::string* why) {
    if (state_->status!=Status::Ready) return fail(why,"dispatch after recording closed");
    for (const auto& a:args) if (a.buffer && !valid(a,*state_->memory,why)) return false;
    // air_dispatch stages pushed UBOs on the CPU NOW, before any recorded PM4
    // executes. Do not silently replace a GPU producer -> constant consumer
    // dependency with old host bytes. Whole-BO rejection is conservative: the
    // resident list tracks allocation writes rather than precise byte ranges.
    for (const auto& r:c.resources) {
        if (r.kind!=Resource::kConstantBuffer || r.index>=args.size() || !args[r.index].buffer) continue;
        const auto& source=*args[r.index].buffer;
        for (const auto& prior:state_->residents)
            if (prior.write && overlap(source,*prior.buffer))
                return fail(why,"constant snapshot depends on an earlier batch write; complete producer before recording");
        for (const auto& device:c.resources)
            if (device.kind==Resource::kDeviceBuffer && (device.write || device.irWrite) &&
                device.index<args.size() && args[device.index].buffer && overlap(source,*args[device.index].buffer))
                return fail(why,"constant snapshot aliases a writable dispatch buffer");
    }
    air_dispatch_a5xx::Prepared prepared;
    if (!air_dispatch_a5xx::prepare(c,grid,args,*state_->memory,&prepared,why)) return false;
    pm4::Stream added; restore(added);
    for (size_t i=0;i<prepared.stream.size();) {
        auto h=pm4::decode(prepared.stream.words()[i]); const auto* p=prepared.stream.words().data()+i+1;
        if (h.kind==pm4::Header::kRegisters) added.registers(h.reg,p,h.count); else added.packet(h.opcode,p,h.count);
        i+=size_t(h.count)+1;
    }
    return append(added,prepared.residents,why);
}
bool Batch::draw(const Program& program,const Texture& texture,const Draw& draw,std::string* why) {
    if (state_->status!=Status::Ready || !program.linked) return fail(why,"draw needs linked program and open recorder");
    if (draw.primitive!=3 || draw.first || draw.instances!=1 || draw.storeAction!=1 || draw.loadAction>2)
        return fail(why,"only direct triangle first0 instance1 single stored color pass supported");
    auto& memory=*state_->memory;
    if (!valid(program.vertexImage,memory,why) || !valid(program.fragmentImage,memory,why)) return false;
    const auto& images=program.linked->images();
    if (program.vertexImage.offset || program.fragmentImage.offset ||
        images.vs.address!=program.vertexImage.buffer->gpuAddress() || images.vs.bytes!=program.vertexImage.sizeBytes ||
        images.fs.address!=program.fragmentImage.buffer->gpuAddress() || images.fs.bytes!=program.fragmentImage.sizeBytes)
        return fail(why,"program image owners disagree with linked addresses");
    render_a5xx::ColorTarget target;
    if (!targetOf(texture,memory,&target,why)) return false;
    if (overlap(*program.vertexImage.buffer,*texture.storage.buffer) || overlap(*program.fragmentImage.buffer,*texture.storage.buffer))
        return fail(why,"shader and target backing allocations alias");
    auto scratch=allocation(memory,4,why); if (!scratch.buffer) return false;
    if (overlap(*scratch.buffer,*texture.storage.buffer) || overlap(*scratch.buffer,*program.vertexImage.buffer) || overlap(*scratch.buffer,*program.fragmentImage.buffer))
        return fail(why,"scratch aliases resident backing allocation");
    pm4::Stream added; render_a5xx::SysmemPass pass(added,4096);
    if (!pass.begin(target,{scratch.buffer->gpuAddress(),scratch.sizeBytes},why)) return false;
    if (draw.viewport && !pass.setViewport(*draw.viewport,why)) return false;
    if (draw.scissor && !pass.setScissor(*draw.scissor,why)) return false;
    if (draw.loadAction==2) {
        std::array<uint8_t,4> color;
        if (!clearBytes(draw.clearColor,&color,why) || !pass.clearColor(color,why)) return false;
    }
    if (!program.linked->drawTriangles(added,pass,draw.first,draw.count,why) || !pass.end(why)) return false;
    return append(added,{resident(program.vertexImage,false),resident(program.fragmentImage,false),resident(texture.storage,true),resident(scratch,true)},why);
}
bool Batch::clear(const Texture& texture,const std::array<double,4>& rgba,std::string* why) {
    if (state_->status!=Status::Ready) return fail(why,"clear after recording closed");
    render_a5xx::ColorTarget target; std::array<uint8_t,4> color;
    if (!targetOf(texture,*state_->memory,&target,why) || !clearBytes(rgba,&color,why)) return false;
    auto scratch=allocation(*state_->memory,4,why); if (!scratch.buffer) return false;
    if (overlap(*scratch.buffer,*texture.storage.buffer)) return fail(why,"clear scratch aliases target");
    pm4::Stream added; render_a5xx::SysmemPass pass(added,4096);
    if (!pass.begin(target,{scratch.buffer->gpuAddress(),scratch.sizeBytes},why) || !pass.clearColor(color,why) || !pass.end(why)) return false;
    return append(added,{resident(texture.storage,true),resident(scratch,true)},why);
}
bool Batch::submit(uint32_t queue,std::string* why) {
    auto& s=*state_; Gpu* gpu=s.memory->gpu();
    if (s.status!=Status::Ready || !gpu || !s.memory->canSubmit() || !s.stream.size())
        return fail(why,"submit requires ready nonempty admitted transport context");
    pm4::Stream tail; tail.waitMemWrites(); tail.waitForIdle();
    if (!append(tail,{},why)) return false;
    s.status=Status::Rejected; // no retries after an attempted submit, even prep failure
    s.commands=allocation(*s.memory,s.stream.size()*4,why);
    if (!s.commands.buffer) return false;
    for (const auto& r:s.residents)
        if (overlap(*s.commands.buffer,*r.buffer)) return fail(why,"command allocation aliases resident");
    if (!s.memory->upload(*s.commands.buffer,s.stream.words().data(),s.commands.sizeBytes,why)) return false;
    auto all=s.residents; all.push_back(resident(s.commands,false));
    if (!s.memory->publish(all,why)) return false;
    if (gpu->param(kParamFaults,&s.faults)) return fail(why,"cannot establish pre-submit FAULTS");
    s.queue=queue; s.status=Status::Submitted;
    const int error=gpu->submit(queue,s.commands.buffer->bo(),0,uint32_t(s.stream.size()),boUses(),&s.fence);
    if (error || !s.fence) {
        s.status=Status::Uncertain; s.pinned=true; s.memory->quarantine(state_);
        return fail(why,"submission uncertain; retained without retry: "+std::to_string(error));
    }
    return success(why);
}
bool Batch::wait(int64_t timeout,std::string* why) {
    auto& s=*state_;
    if (s.status!=Status::Submitted || timeout<0 || timeout>5000000000ll)
        return fail(why,"one bounded wait requires submitted batch");
    Gpu* gpu=s.memory->gpu(); int error=gpu->wait(s.queue,s.fence,timeout); uint64_t faults=0;
    if (!error) error=gpu->param(kParamFaults,&faults);
    if (error || faults!=s.faults) {
        s.status=Status::Uncertain; s.pinned=true; s.memory->quarantine(state_);
        return fail(why,"wait/fault proof failed; retained without retry: "+std::to_string(error));
    }
    s.status=Status::FenceConfirmed; return success(why);
}

} // namespace hakios::metal::backend_a5xx
