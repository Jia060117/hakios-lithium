// SPDX-License-Identifier: MIT
// Actual sampled-render TU, recording-only BO transport. No hardware PM4 run.
#include "hakios-metal/sampled_render_a5xx.hpp"
#include "hakios-metal/command.hpp"
#include "hakios-metal/a5xx.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
using namespace hakios::metal;
namespace sr = hakios::metal::sampled_render_a5xx;
namespace hw = hakios::metal::a5xx;
namespace {
unsigned checks = 0, cases = 0;
void check(bool yes, const std::string& why) {
    ++checks;
    if (!yes) { std::fprintf(stderr, "FAIL %s\n", why.c_str()); std::exit(1); }
}
struct Stats { unsigned submits = 0, waits = 0, opens = 0, closes = 0; bool destroyed = false; };
class Recording final : public Transport {
public:
    explicit Recording(std::shared_ptr<Stats> s) : s_(std::move(s)) {}
    ~Recording() override { s_->destroyed = true; }
    int call(uint32_t op, void* data, size_t) override {
        if (op == kGemNew) {
            auto& n = *static_cast<MsmGemNew*>(data); n.handle = ++last_;
            bytes_[n.handle].resize(size_t(n.size), 0); ++s_->opens; return 0;
        }
        if (op == kGemInfo) {
            auto& i = *static_cast<MsmGemInfo*>(data);
            i.value = 0x1200000000ull + uint64_t(i.handle) * 0x100000; return 0;
        }
        if (op == kGemClose) {
            bytes_.erase(static_cast<DrmGemClose*>(data)->handle); ++s_->closes; return 0;
        }
        if (op == kGemSubmit) ++s_->submits;
        if (op == kWaitFence) ++s_->waits;
        return ENOTSUP; // test never queries chip or submits/executes commands
    }
    void* map(uint32_t id, uint64_t) override { return bytes_.at(id).data(); }
    void unmap(uint32_t, void*) override {}
    int64_t nowNs() override { return 0; }
private:
    std::shared_ptr<Stats> s_;
    uint32_t last_ = 0;
    std::map<uint32_t, std::vector<uint8_t>> bytes_;
};
struct Host {
    std::shared_ptr<Stats> stats = std::make_shared<Stats>();
    std::shared_ptr<Gpu> gpu = std::make_shared<Gpu>(std::make_unique<Recording>(stats));
    sr::BufferRef buffer(uint64_t n, uint64_t offset = 0) {
        int error = 0; auto bo = gpu->newBo(n + offset, 0, &error);
        check(bool(bo) && !error, "recording BO allocation");
        return {gpu, std::make_shared<Buffer>(std::move(bo), n + offset), offset, n};
    }
};
struct Pair { CompiledStage vs, fs; };
void sampled(CompiledStage& c, unsigned tex, unsigned samp) {
    auto& k = c.stage.k;
    k.textures[tex] = {kTexSampled, kTexDim2D, 0, kElemFloat, 0, 1, 0, 0, 0};
    k.numTextureSlots = tex + 1; k.samplerMask = 1u << samp;
    k.numSamp = std::max(tex + 1, samp + 1);
    Resource t{}; t.kind = Resource::kTexture; t.index = tex; t.read = true;
    t.textureDim = kTexDim2D; t.element = kElemFloat; t.typeName = "texture2d<float, sample>";
    Resource s{}; s.kind = Resource::kSampler; s.index = samp;
    c.resources = {t, s};
}
Pair fixture() {
    Pair p;
    for (unsigned i = 0; i < 2; ++i) {
        auto& c = i ? p.fs : p.vs; auto& s = c.stage; auto& k = s.k;
        c.name = i ? "fixture_fs" : "fixture_vs";
        c.words.assign(32, 0); // metadata fixture, deliberately NOT executable
        c.writesMemory = false; s.kind = i ? kStageFragment : kStageVertex;
        k.numWords = 32; k.instrlen = 1; k.maxReg = 3; k.maxHalfReg = -1;
        k.numwgConstRegid = kNoConstRegid; k.driverParamsOffsetVec4 = -1;
        k.compiledChipId = 0x05030004; k.a5xxBoundUboPushes = true;
        s.vertexIdRegid = s.instanceIdRegid = s.positionRegid = s.pointSizeRegid = kNoRegid;
        s.fragCoordRegid = s.faceRegid = s.sampleIdRegid = s.sampleMaskInRegid = kNoRegid;
        s.depthRegid = s.sampleMaskRegid = s.stencilRefRegid = kNoRegid;
        std::fill(std::begin(s.ijRegid), std::end(s.ijRegid), kNoRegid);
        std::fill(std::begin(s.colorRegid), std::end(s.colorRegid), kNoRegid);
        c.varyingNames = {"generated(5colorDv4_f)"};
    }
    auto& v = p.vs.stage; auto& f = p.fs.stage;
    v.positionRegid = 4; v.numInputs = 1; v.inputs[0] = {12, 0, 1, 0, 1, 0, 0, 0};
    v.numOutputs = 2; v.outputs[0] = {0, 4, 0, kElemFloat}; v.outputs[1] = {32, 8, 0, kElemFloat};
    f.totalIn = 4; f.numInputs = 2;
    f.inputs[0] = {32, kNoRegid, 15, 0, 0, 1, 0, 0}; f.inputs[1] = {56, 0, 3, 4, 1, 0, 0, 0};
    f.ijRegid[0] = 0; f.numOutputs = 1; f.outputs[0] = {4, 4, 0, kElemFloat};
    f.numColorOutputs = 1; f.colorRegid[0] = 4;
    sampled(p.vs, 2, 5); sampled(p.fs, 1, 3);
    v.needPixlod = 1; f.needPixlod = f.needFullQuad = 1;
    return p;
}
struct Setup {
    Host host;
    Pair pair = fixture();
    sr::Images images{host.buffer(4096, 128), host.buffer(4096)};
    sr::Bindings bindings;
    sr::BufferRef target = host.buffer(16384), scratch = host.buffer(4);
    Setup() {
        hw::LinearTexture l;
        check(hw::linearTexture(8, 3, hw::LinearFormat::RGBA8, &l, nullptr), "RGBA layout");
        bindings.vertex.textures.push_back({2, l, host.buffer(l.layerBytes, 256)});
        check(hw::linearTexture(8, 3, hw::LinearFormat::BGRA8, &l, nullptr), "BGRA layout");
        bindings.fragment.textures.push_back({1, l, host.buffer(l.layerBytes)});
        SamplerDesc nearest, linear; linear.minFilter = linear.magFilter = 1;
        bindings.vertex.samplers.push_back({5, nearest}); bindings.fragment.samplers.push_back({3, linear});
    }
    std::unique_ptr<sr::Program> link(std::string* why = nullptr) {
        return sr::Program::link(pair.vs, pair.fs, images, link_a5xx::ColorFormat::RGBA8Unorm,
            link_a5xx::VaryingInterface::MatchingNames, why);
    }
    render_a5xx::ColorTarget color() const {
        return {target.buffer->gpuAddress(), target.sizeBytes, 64, 64, 256, 16384};
    }
};
struct Decoded {
    std::map<uint32_t, uint32_t> regs;
    std::map<std::pair<unsigned, unsigned>, std::vector<uint32_t>> tables;
    unsigned draws = 0, loads = 0, flushes = 0, waits = 0;
    size_t flushAt = 0, firstTable = SIZE_MAX, drawAt = SIZE_MAX;
};
Decoded decode(const pm4::Stream& s) {
    Decoded d;
    for (size_t i = 0; i < s.size();) {
        const auto h = pm4::decode(s.words()[i]);
        check(h.kind != pm4::Header::kBad && h.count <= s.size() - i - 1, "PM4 parity/framing");
        const auto* p = s.words().data() + i + 1;
        if (h.kind == pm4::Header::kRegisters) {
            for (unsigned n = 0; n < h.count; ++n) d.regs[h.reg + n] = p[n];
            if (h.reg == hw::REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_LO) {
                check(h.count == 5 && !p[0] && !p[1] && !p[2] && !p[3] && p[4] == 0x12, "exact fd5 UCHE flush");
                ++d.flushes; d.flushAt = i;
            }
        } else {
            if (h.opcode == pm4::kWaitForIdle) ++d.waits;
            if (h.opcode == hw::CP_DRAW_INDX_OFFSET) { ++d.draws; d.drawAt = i; }
            check(h.opcode != 0x30 && h.opcode != 0x34, "no CP_LOAD_STATE6 geometry/fragment fallback");
            if (h.opcode == hw::CP_LOAD_STATE4) {
                check(h.count >= 3, "load header extent");
                const auto block = (p[0] & hw::CP_LOAD_STATE4_0_STATE_BLOCK__MASK) >> hw::CP_LOAD_STATE4_0_STATE_BLOCK__SHIFT;
                const auto source = (p[0] & hw::CP_LOAD_STATE4_0_STATE_SRC__MASK) >> hw::CP_LOAD_STATE4_0_STATE_SRC__SHIFT;
                const auto units = (p[0] & hw::CP_LOAD_STATE4_0_NUM_UNIT__MASK) >> hw::CP_LOAD_STATE4_0_NUM_UNIT__SHIFT;
                const auto type = (p[1] & hw::CP_LOAD_STATE4_1_STATE_TYPE__MASK) >> hw::CP_LOAD_STATE4_1_STATE_TYPE__SHIFT;
                if (source == hw::SS4_INDIRECT) { ++d.loads; }
                if (source == hw::SS4_DIRECT && (block == hw::SB4_VS_TEX || block == hw::SB4_FS_TEX)) {
                    check(p[1] == type && p[2] == 0 && (p[0] & hw::CP_LOAD_STATE4_0_DST_OFF__MASK) == 0, "direct load destination0/source0");
                    check(h.count == 3 + units * (type == hw::ST4_SHADER ? 4u : 12u), "fd5 table stride/extent");
                    d.tables[{block, type}] = {p + 3, p + h.count}; d.firstTable = std::min(i, d.firstTable);
                }
            }
        }
        i += size_t(h.count) + 1;
    }
    return d;
}
void tableChecks(const sr::Prepared& p, const sr::Bindings& b) {
    const auto d = decode(p.stream);
    check(d.loads == 2 && d.tables.size() == 4 && d.flushes == 1 && d.waits >= 1, "two shaders + four fd5 tables + flush/WFI");
    check(d.flushAt < d.firstTable && d.firstTable < d.drawAt, "flush before descriptor loads before draw");
    const StageBindings* unused = nullptr; (void)unused;
    const sr::StageBindings* stages[]{&b.vertex, &b.fragment};
    for (unsigned n = 0; n < 2; ++n) {
        const auto block = unsigned(n ? hw::SB4_FS_TEX : hw::SB4_VS_TEX);
        const auto& t = stages[n]->textures[0]; const auto& s = stages[n]->samplers[0];
        const auto& tw = d.tables.at({block, hw::ST4_CONSTANTS});
        const auto& sw = d.tables.at({block, hw::ST4_SHADER});
        check(tw.size() == (t.slot + 1) * 12 && sw.size() == (s.slot + 1) * 4, "independent sparse texture/sampler slots");
        check(std::all_of(tw.begin(), tw.begin() + t.slot * 12, [](auto v){return !v;}), "texture holes zero");
        check(std::all_of(sw.begin(), sw.begin() + s.slot * 4, [](auto v){return !v;}), "sampler holes zero");
        std::array<uint32_t, 12> expected{};
        check(hw::textureDescriptor(t.layout, t.storage.buffer->gpuAddress() + t.storage.offset,
            t.storage.sizeBytes, &expected, nullptr), "descriptor oracle");
        check(std::equal(expected.begin(), expected.end(), tw.begin() + t.slot * 12), "exact 12 descriptor words + real high IOVA + offset");
    }
}
void samplerCases() {
    ++cases; SamplerDesc d; std::array<uint32_t, 4> w{};
    check(sr::samplerDescriptor(d, &w) && w == std::array<uint32_t, 4>{{0x920, 0x2010, 0, 0}}, "fd5 nearest/clamp nonmip maxLOD1/8 golden");
    ++cases; d.minFilter = d.magFilter = 1; d.normalizedCoordinates = false;
    check(sr::samplerDescriptor(d, &w) && w[0] == 0x92a && w[1] == (0x2010 | hw::A5XX_TEX_SAMP_1_UNNORM_COORDS), "linear/pixel golden");
    const std::function<void(SamplerDesc&)> bad[]{
        [](auto& a){a.minFilter=2;}, [](auto& a){a.mipFilter=1;}, [](auto& a){a.sAddress=2;},
        [](auto& a){a.maxAnisotropy=2;}, [](auto& a){a.compareFunction=1;},
        [](auto& a){a.lodMinClamp=std::numeric_limits<float>::quiet_NaN();},
        [](auto& a){a.lodMinClamp=-1;}, [](auto& a){a.lodMaxClamp=std::numeric_limits<float>::infinity();}};
    for (const auto& change : bad) { ++cases; SamplerDesc a; change(a); const auto before=w;
        check(!sr::samplerDescriptor(a, &w) && w==before, "sampler refusal atomic"); }
}
void encodingCases() {
    Setup f; std::string why; auto p = f.link(&why); check(bool(p), why);
    ++cases; sr::Prepared out;
    check(p->encodeState(f.bindings, &out, &why), why); tableChecks(out, f.bindings);
    const auto decoded = decode(out.stream);
    check(decoded.regs.at(hw::REG_A5XX_TPL1_VS_TEX_COUNT)==3 && decoded.regs.at(hw::REG_A5XX_TPL1_FS_TEX_COUNT)==2 &&
        decoded.regs.at(hw::REG_A5XX_TPL1_CS_TEX_COUNT)==0, "fd5 stage texture counts");
    check((decoded.regs.at(hw::REG_A5XX_SP_VS_CTRL_REG0)&hw::A5XX_SP_VS_CTRL_REG0_PIXLODENABLE) &&
        (decoded.regs.at(hw::REG_A5XX_SP_FS_CTRL_REG0)&hw::A5XX_SP_FS_CTRL_REG0_PIXLODENABLE), "compiler PIXLODENABLE preserved");
    check(out.residents.size()==4 && out.boUses().size()==4, "real shader/texture BO references");
    for(const auto& use:out.boUses()) check(use.bo && !use.write, "only read references");
    check(p->vertex().stage.k.numTextureSlots==3 && p->fragment().stage.needFullQuad==1 &&
        p->vertex().words==f.pair.vs.words, "original compiler facts/binary preserved");
    f.pair.vs.words.clear(); f.pair.vs.resources.clear();
    check(p->vertex().words.size()==32 && p->vertex().resources.size()==2 &&
        p->vertex().stage.k.words==p->vertex().words.data(), "immutable owning original stage snapshot");
    ++cases; pm4::Stream stream; render_a5xx::SysmemPass pass(stream);
    check(pass.begin(f.color(), {f.scratch.buffer->gpuAddress(), 4}, &why), why);
    const auto begin=stream.words();
    check(p->drawTriangles(stream,pass,f.scratch,f.bindings,0,3,&out,&why),why);
    check(std::equal(begin.begin(),begin.end(),stream.words().begin()),"pass prefix unchanged");
    tableChecks(out,f.bindings); check(decode(out.stream).draws==1,"one real fd5 draw after resources");
    check(pass.end(&why),why);
    check(f.host.stats->submits==0 && f.host.stats->waits==0,"host encoder has zero submits/waits");
    const std::function<void(sr::Bindings&)> bad[]{
        [](auto& b){b.fragment.textures.clear();}, [](auto& b){b.fragment.samplers.clear();},
        [](auto& b){b.vertex.textures[0].slot=0;},
        [](auto& b){b.vertex.textures.push_back(b.vertex.textures[0]);},
        [](auto& b){b.fragment.textures[0].storage.sizeBytes=4;},
        [](auto& b){b.fragment.textures[0].layout.pitch+=256;},
        [](auto& b){b.fragment.textures[0].layout.format=hw::LinearFormat::R32Uint;},
        [](auto& b){b.fragment.samplers[0].descriptor.tAddress=4;},
        [](auto& b){b.fragment.textures[0].storage.backingOwner.reset();}};
    for (const auto& change:bad) { ++cases; auto b=f.bindings; change(b); const auto prior=out.stream.words();
        check(!p->encodeState(b,&out,&why) && !why.empty() && out.stream.words()==prior,"binding refusal/output atomic"); }
    ++cases; const auto prior=out.stream.words();
    check(!p->encodeState(f.bindings,&out,&why,32) && out.stream.words()==prior,"word budget refusal atomic");
    ++cases; Host other; auto b=f.bindings; b.fragment.textures[0].storage=other.buffer(4096);
    check(!p->encodeState(b,&out,&why),"cross-context texture refused");
    ++cases; b=f.bindings; b.fragment.textures[0].storage=f.target;
    pm4::Stream aliasStream; render_a5xx::SysmemPass aliasPass(aliasStream);
    check(aliasPass.begin(f.color(),{f.scratch.buffer->gpuAddress(),4},&why),why);
    const auto aliasBefore=aliasStream.words();
    check(!p->drawTriangles(aliasStream,aliasPass,f.scratch,b,0,3,&out,&why) &&
        aliasStream.words()==aliasBefore && out.stream.words()==prior,"target feedback refused atomically");
    ++cases; check(!p->drawTriangles(aliasStream,aliasPass,f.images.vertex,f.bindings,0,3,&out,&why),"shader/scratch alias refused");
    ++cases; b=f.bindings; b.fragment.textures[0].storage=b.vertex.textures[0].storage;
    check(p->encodeState(b,&out,&why) && out.residents.size()==3,"shared texture BO dedup retains both stage slots");
}
void metadataCases() {
    const std::function<void(Pair&)> bad[]{
        [](auto& p){p.fs.stage.k.compiledChipId=0x06030500;},
        [](auto& p){p.vs.stage.k.compiledChipId=0;},
        [](auto& p){p.fs.stage.k.numTextureSlots=1;},
        [](auto& p){p.fs.resources.pop_back();},
        [](auto& p){p.fs.stage.k.textures[1].isArray=1;},
        [](auto& p){p.fs.stage.numSamplerPrefetch=1;},
        [](auto& p){p.fs.inlineSamplers.push_back({3,{}});},
        [](auto& p){p.fs.resources[0].argbufSet=0;},
        [](auto& p){p.fs.stage.k.numUbos=1;},
        [](auto& p){p.fs.varyingNames[0]="different";}};
    for(const auto& change:bad) { ++cases; Setup f; change(f.pair); std::string why;
        check(!f.link(&why) && !why.empty(),"metadata refusal before emission"); }
}
void lifetimeCase() {
    ++cases; sr::Prepared kept; std::shared_ptr<Stats> stats;
    {
        Setup f; stats=f.host.stats; auto p=f.link(); check(bool(p),"lifetime link");
        check(p->encodeState(f.bindings,&kept),"lifetime encode");
        p.reset(); f.host.gpu.reset();
        check(!stats->destroyed,"prepared retains context");
    }
    check(!stats->destroyed && kept.boUses().size()==4,"BOs alive after all inputs destroyed");
    sr::Prepared copied=kept; kept=sr::Prepared{};
    check(!stats->destroyed,"prepared copy retains context"); copied=sr::Prepared{};
    check(stats->destroyed && stats->opens==stats->closes,"all BOs released before context");
}
#ifdef HAKIOS_METAL_MESA
std::shared_ptr<CompiledStage> compile(const char* path) {
    std::ifstream file(path,std::ios::binary); std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    std::string why; auto c=compileStage(bytes.data(),bytes.size(),&why);
    check(bool(c),"actual compileStage: "+why); return c;
}
void actualCase(const char* vpath,const char* fpath,const char* output) {
    ++cases; Setup f; auto vs=compile(vpath),fs=compile(fpath);
    auto bind=[&](const CompiledStage& c,sr::StageBindings& b,hw::LinearFormat fmt) {
        b={}; hw::LinearTexture l; check(hw::linearTexture(8,3,fmt,&l,nullptr),"actual layout");
        for(unsigned i=0;i<kMaxTextureSlots;++i) {
            if(c.stage.k.textures[i].kind==kTexSampled) b.textures.push_back({i,l,f.host.buffer(l.layerBytes)});
            if(c.stage.k.samplerMask&(1u<<i)) { SamplerDesc s; s.minFilter=s.magFilter=1; b.samplers.push_back({i,s}); }
        }
        std::printf("ACTUAL %s chip=%08x tex=%u samplerMask=%x numSamp=%u pixlod=%u fullquad=%u prefetch=%u resources=%zu words=%zu\n",
            c.name.c_str(),c.stage.k.compiledChipId,c.stage.k.numTextureSlots,c.stage.k.samplerMask,c.stage.k.numSamp,
            c.stage.needPixlod,c.stage.needFullQuad,c.stage.numSamplerPrefetch,c.resources.size(),c.words.size());
    };
    bind(*vs,f.bindings.vertex,hw::LinearFormat::RGBA8); bind(*fs,f.bindings.fragment,hw::LinearFormat::BGRA8);
    check(!f.bindings.vertex.textures.empty() && !f.bindings.fragment.textures.empty(),"actual sample instruction in BOTH stages");
    check(vs->stage.k.numWords*4<=f.images.vertex.sizeBytes && fs->stage.k.numWords*4<=f.images.fragment.sizeBytes,"actual code allocation");
    std::memcpy(static_cast<uint8_t*>(const_cast<void*>(f.images.vertex.buffer->contents()))+f.images.vertex.offset,vs->words.data(),vs->words.size()*4);
    std::memcpy(f.images.fragment.buffer->contents(),fs->words.data(),fs->words.size()*4);
    std::string why; auto p=sr::Program::link(*vs,*fs,f.images,link_a5xx::ColorFormat::RGBA8Unorm,
        link_a5xx::VaryingInterface::MatchingNames,&why); check(bool(p),"actual link: "+why);
    sr::Prepared prepared; pm4::Stream stream; render_a5xx::SysmemPass pass(stream);
    check(pass.begin(f.color(),{f.scratch.buffer->gpuAddress(),4},&why),why);
    check(p->drawTriangles(stream,pass,f.scratch,f.bindings,0,3,&prepared,&why),why);
    check(pass.end(&why),why); tableChecks(prepared,f.bindings);
    std::ofstream binary(output,std::ios::binary);
    binary.write(reinterpret_cast<const char*>(stream.words().data()),std::streamsize(stream.size()*4));
    check(bool(binary),"actual command receipt");
    check(f.host.stats->submits==0,"actual compiler test never submits");
    std::printf("ACTUAL PASS nativeMesaLink=%u words=%zu residents=%zu hardware_executed=0\n",link_a5xx::usesMesaLinker(),stream.size(),prepared.residents.size());
}
#endif
} // namespace
int main(int argc,char** argv) {
    samplerCases(); encodingCases(); metadataCases(); lifetimeCase();
#ifdef HAKIOS_METAL_MESA
    if(argc==4) actualCase(argv[1],argv[2],argv[3]); else check(false,"provide actual VS/FS bitcode and output path");
#else
    (void)argc; (void)argv;
#endif
    std::printf("PASS A530 sampled-render cases=%u checks=%u ASan/UBSan module; device_operations=0 hardware_executed=0\n",cases,checks);
}
