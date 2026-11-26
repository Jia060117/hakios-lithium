// SPDX-License-Identifier: MIT
// Composed facade tests. The recording transport NEVER executes any PM4.
#include "hakios-metal/backend_a5xx.hpp"
#include "hakios-metal/command.hpp"
#include "hakios-metal/a5xx.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

using namespace hakios::metal;
namespace be=hakios::metal::backend_a5xx;
namespace {
unsigned checks=0;
void check(bool result,const std::string& why) {
    ++checks;
    if (!result) { std::fprintf(stderr,"FAIL %s\n",why.c_str()); std::exit(1); }
}
struct Stats {
    unsigned submits=0,waits=0,prep=0,fini=0,live=0,closes=0;
    uint64_t chip=be::kChipId,faults=0;
    int submitError=0,waitError=0,paramError=0,publishError=0;
    std::vector<uint32_t> stream;
    std::vector<MsmSubmitBo> bos;
};
class RecordingTransport final : public Transport {
public:
    explicit RecordingTransport(std::shared_ptr<Stats> stats):s(std::move(stats)) {}
    int call(uint32_t req,void* data,size_t) override {
        if (req==kGetParam) {
            auto& p=*static_cast<MsmParam*>(data);
            if (s->paramError) return s->paramError;
            if (p.param==kParamChipId) p.value=s->chip;
            else if (p.param==kParamFaults) p.value=s->faults;
            else return ENOTSUP;
            return 0;
        }
        if (req==kGemNew) {
            auto& n=*static_cast<MsmGemNew*>(data); n.handle=++last;
            bytes[n.handle].resize(size_t(n.size),0xbd); ++s->live; return 0;
        }
        if (req==kGemInfo) {
            auto& i=*static_cast<MsmGemInfo*>(data); i.value=0x1500000000ull+uint64_t(i.handle)*0x100000; return 0;
        }
        if (req==kGemClose) {
            s->live-=unsigned(bytes.erase(static_cast<DrmGemClose*>(data)->handle)); ++s->closes; return 0;
        }
        if (req==kGemCpuPrep) { ++s->prep; return s->publishError; }
        if (req==kGemCpuFini) { ++s->fini; return 0; }
        if (req==kGemSubmit) {
            ++s->submits;
            auto& submit=*static_cast<MsmGemSubmit*>(data);
            const auto* b=reinterpret_cast<const MsmSubmitBo*>(uintptr_t(submit.bos));
            const auto* c=reinterpret_cast<const MsmSubmitCmd*>(uintptr_t(submit.cmds));
            s->bos.assign(b,b+submit.nr_bos);
            check(submit.nr_cmds==1 && c->submit_idx==0 && c->size%4==0,"msm submit ABI");
            auto& storage=bytes.at(b[0].handle);
            check(c->submit_offset+c->size<=storage.size(),"bounded command BO");
            s->stream.resize(c->size/4);
            std::memcpy(s->stream.data(),storage.data()+c->submit_offset,c->size);
            submit.fence=7;
            return s->submitError; // does not execute commands or alter any data
        }
        if (req==kWaitFence) { ++s->waits; return s->waitError; }
        return ENOTSUP;
    }
    void* map(uint32_t handle,uint64_t) override { return bytes.at(handle).data(); }
    void unmap(uint32_t,void*) override {}
    int64_t nowNs() override { return 1000000000; }
private:
    std::shared_ptr<Stats> s;
    uint32_t last=0;
    std::map<uint32_t,std::vector<uint8_t>> bytes;
};
struct HostMemory final : be::Memory {
    std::shared_ptr<Stats> stats=std::make_shared<Stats>();
    std::shared_ptr<Gpu> context=std::make_shared<Gpu>(std::make_unique<RecordingTransport>(stats));
    std::vector<std::shared_ptr<const void>> pinned;
    bool blocked=false;
    std::shared_ptr<const void> backingOwner() const override { return context; }
    Gpu* gpu() const noexcept override { return context.get(); }
    bool canSubmit() const noexcept override { return !blocked; }
    void quarantine(std::shared_ptr<const void> s) noexcept override { blocked=true; pinned.push_back(std::move(s)); }
    std::shared_ptr<Buffer> allocate(uint64_t size,std::string*) override {
        int error=0; auto bo=context->newBo(size,0,&error);
        if (!bo || error) return {};
        return std::make_shared<Buffer>(std::move(bo),size);
    }
    bool snapshot(const Buffer& b,uint64_t off,uint64_t n,void* dest,std::string*) override {
        if (!dest || off>b.length() || n>b.length()-off) return false;
        std::memcpy(dest,static_cast<const uint8_t*>(b.contents())+off,size_t(n)); return true;
    }
    bool upload(const Buffer& b,const void* data,uint64_t n,std::string*) override {
        if (!data || n>b.length()) return false;
        std::memcpy(b.contents(),data,size_t(n)); return true;
    }
    bool publish(const std::vector<be::Resident>&,std::string* why) override {
        if (stats->publishError) { if (why) *why="host publish refusal"; return false; } return true;
    }
};
std::vector<char> read(const char* path) {
    std::ifstream f(path,std::ios::binary);
    std::vector<char> b((std::istreambuf_iterator<char>(f)),{}); check(!b.empty(),"read actual AIR"); return b;
}
std::map<uint32_t,unsigned> commands(const std::vector<uint32_t>& words) {
    std::map<uint32_t,unsigned> counts;
    for (size_t i=0;i<words.size();) {
        auto h=pm4::decode(words[i]); check(h.kind!=pm4::Header::kBad && h.count<=words.size()-i-1,"packet parity and extent");
        if (h.kind==pm4::Header::kCommand) {
            check(h.opcode!=0x75 && h.opcode!=0x36 && h.opcode!=0x32 && h.opcode!=0x34,
                "no A6xx MEMCPY/LOAD_STATE6"); ++counts[h.opcode];
        }
        i+=size_t(h.count)+1;
    }
    return counts;
}
void composition(const std::vector<char>& air,const std::vector<char>& vs,const std::vector<char>& fs) {
    auto memory=std::make_shared<HostMemory>(); std::string why;
    auto backend=be::Backend::onMemory(be::kChipId,memory,&why); check(bool(backend),why);
    check(!be::Backend::onMemory(0,memory,&why),"unknown chip refused");
    check(!be::Backend::onMemory(0x06030500,memory,&why),"A6xx refused");
    auto src=backend->buffer(256,&why),dest=backend->buffer(256,&why),constant=backend->buffer(4,&why);
    check(src.buffer && dest.buffer && constant.buffer,why);
    std::memset(dest.buffer->contents(),0x41,256);
    uint32_t value=42; std::memcpy(constant.buffer->contents(),&value,4);
    auto texture=backend->texture(64,64,70,4,&why); check(bool(texture),why);
    std::vector<uint8_t> rows(64*64*4,0x69),output(rows.size(),0);
    check(backend->upload(*texture,rows.data(),rows.size(),256,&why),why);
    check(backend->download(*texture,output.data(),output.size(),256,&why) && output==rows,"A5xx texture row roundtrip");
    auto kernel=backend->compileCompute(air.data(),air.size(),{},&why); check(bool(kernel),why);
    check(kernel->resources.size()==2 && kernel->resources[1].kind==Resource::kConstantBuffer,
          "real fill_id constant argument is available for snapshot dependency tests");
    auto program=backend->compileProgram(vs.data(),vs.size(),fs.data(),fs.size(),70,{},{},&why); check(bool(program),why);
    check(program->linked->vertex().varyingNames==program->linked->fragment().varyingNames,"frozen compiler MatchingNames retained");
    auto batch=backend->batch(); check(bool(batch),"new batch");
    check(batch->fill(dest,0,256,0x23,&why),why);
    check(batch->copy(src,0,dest,0,256,&why),why);
    compute_a5xx::Grid grid; grid.local[0]=64; grid.groups[0]=1; grid.workDim=1;
    auto dependent=backend->batch();
    check(dependent->fill(constant,0,4,0,&why),why);
    const auto producer=dependent->words(); const auto liveBefore=memory->stats->live;
    check(!dependent->dispatch(*kernel,grid,{dest,constant},&why) &&
          why.find("earlier batch write")!=std::string::npos && dependent->words()==producer &&
          memory->stats->live==liveBefore,"record-time constant snapshot rejects pending producer before allocation");
    auto aliasedConstant=dest; aliasedConstant.sizeBytes=4;
    auto alias=backend->batch();
    check(!alias->dispatch(*kernel,grid,{dest,aliasedConstant},&why) &&
          why.find("aliases a writable")!=std::string::npos && alias->words().empty(),
          "constant snapshot refuses writable SSBO alias using actual compiler resource facts");
    check(batch->dispatch(*kernel,grid,{dest,constant},&why),why);
    be::Draw draw; draw.count=6; draw.loadAction=2; draw.clearColor={1,0,0,1};
    check(batch->draw(*program,*texture,draw,&why),why);
    check(memory->stats->submits==0 && memory->stats->waits==0,"recording never submits");
    auto count=commands(batch->words());
    check(count[a5xx::CP_EXEC_CS]==1 && count[a5xx::CP_DRAW_INDX_OFFSET]==1 && count[a5xx::CP_LOAD_STATE4]>=3,"actual A5xx compute and linked draw composition");
    check(count[a5xx::CP_MEM_TO_MEM]==64 && count[a5xx::CP_MEM_WRITE]>=1,"A5xx copy/fill opcodes");
    auto uses=batch->boUses();
    check(uses.size()>=8,"programs constants scratch and target retained");
    auto before=batch->words(); auto foreign=std::make_shared<HostMemory>();
    auto other=be::Backend::onMemory(be::kChipId,foreign)->buffer(256);
    check(!batch->copy(other,0,dest,0,4,&why) && batch->words()==before,"foreign context refused atomically");
    check(!batch->copy(src,0,dest,1,4,&why) && batch->words()==before,"bad copy atomic");
    draw.instances=2;
    check(!batch->draw(*program,*texture,draw,&why) && batch->words()==before,"instanced draw refused atomically");
    draw.instances=1; draw.clearColor[0]=2;
    check(!batch->draw(*program,*texture,draw,&why) && batch->words()==before,"invalid clear atomic");
    check(!backend->texture(64,64,70,2,&why),"shader-write texture usage refused");
    check(!backend->texture(64,64,53,4,&why),"integer render target refused");
    auto tiny=backend->batch(4);
    check(!tiny->fill(dest,0,256,0,&why) && tiny->words().empty(),"small batch bound atomic");
    check(batch->submit(0,&why),why);
    check(batch->status()==be::Batch::Status::Submitted && batch->fence()==7 && memory->stats->submits==1,"explicit submit only, synthetic fence returned");
    check(!batch->fill(dest,0,4,0,&why) && !batch->submit(0,&why),"immutable submitted batch, no resubmit");
    check(batch->wait(100000000,&why) && batch->status()==be::Batch::Status::FenceConfirmed,"transport proof status only");
    check(!batch->wait(100000000,&why),"no automatic second wait");
    check(static_cast<uint8_t*>(dest.buffer->contents())[0]==0x41,"transport DOES NOT execute fake shader/copy/fill");
    check(memory->stats->bos.size()==uses.size()+1,"all resident BOs included with commands");
    std::printf("COMPOSED real AIR + copy/fill + single-pass draw words=%zu resident_BOs=%zu submit_calls=1 shader_execution=0\n",before.size(),uses.size());
}
void lifetimeAndRefusal() {
    std::string why;
    auto memory=std::make_shared<HostMemory>(); auto backend=be::Backend::onMemory(be::kChipId,memory);
    auto b=backend->buffer(64); auto work=backend->batch(); check(work->fill(b,0,4,1,&why),why);
    auto retained=b.buffer; b={}; backend.reset();
    check(work->submit(0,&why),why); memory->stats->waitError=ETIMEDOUT;
    check(!work->wait(100000000,&why) && work->status()==be::Batch::Status::Uncertain,"timeout retains uncertain DMA");
    check(memory->pinned.size()==1 && memory->blocked,"quarantine once and stop submission admission");
    check(!work->wait(100000000,&why) && !work->submit(0,&why),"uncertain work not retried");
    auto live=memory->stats->live; work.reset(); retained.reset();
    check(memory->stats->live==live,"uncertain resources survive handle free");
    memory->pinned.clear(); // synthetic transport owns no DMA; explicit test cleanup only
    check(memory->stats->live==0,"test-only quarantine cleanup releases all context-bound BOs");
    auto reset=std::make_shared<HostMemory>(); auto driver=be::Backend::onMemory(be::kChipId,reset);
    auto x=driver->buffer(64); auto fault=driver->batch(); check(fault->fill(x,0,4,0,&why),why);
    check(fault->submit(0,&why),why); ++reset->stats->faults;
    check(!fault->wait(100000000,&why),"fault counter change never completion"); fault.reset(); reset->pinned.clear();
    auto prep=std::make_shared<HostMemory>(); auto pb=be::Backend::onMemory(be::kChipId,prep);
    auto y=pb->buffer(64); auto p=pb->batch(); check(p->fill(y,0,4,0,&why),why); prep->stats->publishError=EIO;
    check(!p->submit(0,&why) && prep->stats->submits==0 && p->status()==be::Batch::Status::Rejected,"publish failure before transport");
    check(!p->submit(0,&why),"prep failure not retried automatically");
    auto failed=std::make_shared<HostMemory>(); auto fb=be::Backend::onMemory(be::kChipId,failed);
    auto z=fb->buffer(64); auto f=fb->batch(); check(f->fill(z,0,4,0,&why),why);
    failed->stats->submitError=EIO;
    check(!f->submit(0,&why) && f->status()==be::Batch::Status::Uncertain &&
          failed->pinned.size()==1 && failed->stats->submits==1,"failed submit pins once without resubmit");
    check(!f->submit(0,&why) && failed->stats->submits==1,"failed submit remains closed");
    f.reset(); failed->pinned.clear();
    auto abandoned=std::make_shared<HostMemory>(); auto ab=be::Backend::onMemory(be::kChipId,abandoned);
    auto a=ab->buffer(64); auto pending=ab->batch(); check(pending->fill(a,0,4,0,&why),why);
    check(pending->submit(0,&why),why); pending.reset();
    check(abandoned->pinned.size()==1 && abandoned->blocked && abandoned->stats->waits==0,
          "destroying submitted batch pins without pretending it completed");
    abandoned->pinned.clear();
}
void realAdapter() {
    std::string why;
    auto stats=std::make_shared<Stats>(); stats->chip=0;
    auto gpu=std::make_shared<Gpu>(std::make_unique<RecordingTransport>(stats));
    check(!be::Backend::onGpu(gpu,&why) && stats->live==0,"production chip0 refused before allocations");
    stats->chip=(1ull<<32)|be::kChipId;
    check(!be::Backend::onGpu(gpu,&why) && stats->live==0,"malformed upper CHIP_ID bits refused without truncation");
    stats->chip=be::kChipId;
    auto backend=be::Backend::onGpu(gpu,&why); check(bool(backend),why);
    auto texture=backend->texture(2,2,80,4,&why); check(bool(texture),why);
    const uint8_t rows[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}; uint8_t out[16]{};
    check(backend->upload(*texture,rows,16,8,&why) && backend->download(*texture,out,16,8,&why),why);
    check(!std::memcmp(rows,out,16) && stats->prep==2 && stats->fini==2,"real adapter CPU prep/fini ABI");
    auto work=backend->batch(); check(work->clear(*texture,{0,0,0,1},&why),why);
    check(work->submit(0,&why) && work->wait(100000000,&why),why);
    check(stats->submits==1 && stats->waits==1,"real adapter delegates explicit submit/wait ABI");
}
} // namespace
int main(int argc,char** argv) {
    if (argc!=4 || !hasCompiler()) return 2;
    composition(read(argv[1]),read(argv[2]),read(argv[3])); lifetimeAndRefusal(); realAdapter();
    std::printf("PASS a5xx-backend checks=%u actual_hardware_operations=0 shader_execution=0\n",checks);
}
