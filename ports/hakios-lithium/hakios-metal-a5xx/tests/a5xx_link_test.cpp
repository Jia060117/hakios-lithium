// SPDX-License-Identifier: MIT
// Synthetic metadata exercises host encoding only. No instruction execution.
#include "hakios-metal/link_a5xx.hpp"
#include "hakios-metal/a5xx.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

using namespace hakios::metal;
using Linked = link_a5xx::Program;
using Interface = link_a5xx::VaryingInterface;
namespace {
unsigned checks = 0, refusals = 0;
std::vector<uint32_t> stateCorpus;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
struct Pair { CompiledStage v, f; };
link_a5xx::Images images() { return {{0x100000, 4096}, {0x200000, 4096}}; }
Pair fixture() {
    Pair p;
    for (unsigned i = 0; i < 2; ++i) {
        auto& c = i ? p.f : p.v; auto& s = c.stage; auto& k = s.k;
        c.name = i ? "fixture_fs" : "fixture_vs";
        c.words.assign(32, 0); // deliberately NOT executable shader fixtures
        c.writesMemory = false;
        s.kind = i ? kStageFragment : kStageVertex;
        k.numWords = 32; k.instrlen = 1; k.maxReg = 3; k.maxHalfReg = -1;
        k.numwgConstRegid = kNoConstRegid; k.driverParamsOffsetVec4 = -1;
        k.compiledChipId = 0x05030004; k.a5xxBoundUboPushes = true;
        s.vertexIdRegid = s.instanceIdRegid = s.positionRegid = s.pointSizeRegid = kNoRegid;
        s.fragCoordRegid = s.faceRegid = s.sampleIdRegid = s.sampleMaskInRegid = kNoRegid;
        s.depthRegid = s.sampleMaskRegid = s.stencilRefRegid = kNoRegid;
        std::fill(std::begin(s.ijRegid), std::end(s.ijRegid), kNoRegid);
        std::fill(std::begin(s.colorRegid), std::end(s.colorRegid), kNoRegid);
        c.varyingNames = {"color"};
    }
    p.v.stage.positionRegid = 4;
    p.v.stage.numInputs = 1;
    p.v.stage.inputs[0] = {12, 0, 1, 0, 1, 0, 0, 0}; // actual ZERO_BASE enum
    p.v.stage.numOutputs = 2;
    p.v.stage.outputs[0] = {0, 4, 0, kElemFloat};
    p.v.stage.outputs[1] = {32, 8, 0, kElemFloat};
    p.v.immediates = {0x3f800000, 0, 0};
    p.v.stage.k.numImmediates = 3;
    p.v.stage.k.immediatesOffsetVec4 = 1;
    p.v.stage.k.constlen = 4;
    p.v.stage.k.driverParamsOffsetVec4 = 0;
    p.v.stage.k.driverParamsDwords = 4;
    p.f.stage.totalIn = 4; p.f.stage.numInputs = 2;
    p.f.stage.inputs[0] = {32, kNoRegid, 15, 0, 0, 1, 0, 0};
    p.f.stage.inputs[1] = {56, 0, 3, 4, 1, 0, 0, 0};
    p.f.stage.ijRegid[0] = 0;
    p.f.stage.numOutputs = 1; p.f.stage.outputs[0] = {4, 4, 0, kElemFloat};
    p.f.stage.numColorOutputs = 1; p.f.stage.colorRegid[0] = 4;
    return p;
}
std::unique_ptr<Linked> link(const Pair& p, std::string* why = nullptr,
                            Interface interface = Interface::ExplicitSlots) {
    return Linked::link(p.v, p.f, images(), link_a5xx::ColorFormat::RGBA8Unorm, interface, why);
}
void reject(const char* name, const std::function<void(Pair&)>& change,
            Interface interface = Interface::ExplicitSlots) {
    Pair p = fixture(); change(p); std::string why;
    check(!link(p, &why, interface), name); check(!why.empty(), "refusal must name missing fact"); ++refusals;
}
struct Decoded {
    std::map<uint32_t, uint32_t> regs;
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> commands;
};
Decoded decode(const pm4::Stream& s) {
    Decoded d;
    for (size_t i = 0; i < s.size();) {
        auto h = pm4::decode(s.words()[i]);
        check(h.kind != pm4::Header::kBad && h.count <= s.size() - i - 1, "valid packet framing/parity");
        if (h.kind == pm4::Header::kRegisters)
            for (unsigned j = 0; j < h.count; ++j) d.regs[h.reg + j] = s.words()[i + 1 + j];
        else d.commands.push_back({h.opcode, {s.words().begin() + i + 1, s.words().begin() + i + 1 + h.count}});
        i += size_t(h.count) + 1;
    }
    return d;
}
void happyAndOwnership() {
    Pair p = fixture(); std::string why = "stale";
    auto linked = link(p, &why);
    if (!linked) std::fprintf(stderr, "%s\n", why.c_str());
    check(bool(linked) && why.empty(), "source-backed link succeeds and clears reason");
    const auto& l = linked->linkage();
    check(l.cnt == 2 && l.maxLoc == 8 && l.positionLoc == 4 && l.psizeLoc == 255, "fd5 position appended after FS map");
    check(l.varmask[0] == 15 && l.varmask[1] == 0 && l.mrtComponents == 15, "position excluded from varying disable mask");
    check(l.var[0].slot == 32 && l.var[0].regid == 8 && l.var[0].compmask == 15 && l.var[0].loc == 0, "varying map from compiler");
    check(l.var[1].slot == 0 && l.var[1].regid == 4 && l.var[1].loc == 4, "position map from compiler");
    const auto words = linked->vertex().words;
    p.v.words.clear(); p.v.immediates.clear(); p.v.name = "mutated";
    p.v.stage.k.numWords = 0; p.v.stage.outputs[1].regid = 200;
    p.f.varyingNames[0] = "mutated";
    check(linked->vertex().words == words, "owns words independently");
    check(linked->vertex().stage.k.words == linked->vertex().words.data(), "rebased binary pointer");
    check(linked->vertex().stage.k.immediates == linked->vertex().immediates.data(), "rebased immediates pointer");
    check(linked->vertex().stage.k.name == linked->vertex().name.c_str(), "rebased name pointer");
    check(linked->vertex().stage.vertexIdRegid == kNoRegid, "does not invent convenience vertex-id metadata");
    pm4::Stream s; check(linked->encodeState(s, &why), "callable program state");
    const auto d = decode(s);
    using namespace a5xx;
    check(d.regs.at(REG_A5XX_SP_VS_OBJ_START_LO) == 0x100000 && d.regs.at(REG_A5XX_SP_FS_OBJ_START_LO) == 0x200000, "both exact IOVAs emitted");
    check(d.regs.at(REG_A5XX_VPC_VAR_DISABLE(0)) == ~15u, "fd5 varying disable");
    check(d.regs.at(REG_A5XX_SP_VS_OUT_REG(0)) ==
        (A5XX_SP_VS_OUT_REG_A_REGID(8) | A5XX_SP_VS_OUT_REG_A_COMPMASK(15) |
         A5XX_SP_VS_OUT_REG_B_REGID(4) | A5XX_SP_VS_OUT_REG_B_COMPMASK(15)), "actual SP VS output words");
    check(d.regs.at(REG_A5XX_SP_VS_VPC_DST_REG(0)) == 0x400, "actual VPC destination word");
    check((d.regs.at(REG_A5XX_VFD_CONTROL_1) & 255) == 0, "ZERO_BASE regid supplied from raw table");
    unsigned loads = 0, constants = 0, draws = 0;
    for (const auto& c : d.commands) {
        if (c.first == CP_LOAD_STATE4) {
            if (((c.second[0] >> 16) & 3) == SS4_INDIRECT) ++loads;
            else ++constants;
        }
        draws += c.first == CP_DRAW_INDX_OFFSET;
    }
    check(loads == 2 && constants == 2 && draws == 0, "two shader preloads, owned immediates and VS params, no draw");
    const auto before = s.words();
    check(!linked->encodeState(s, &why, s.size()) && s.words() == before, "word limit failure is atomic");
    check(!linked->encodeState(s, &why, 4097) && s.words() == before, "explicit upper stream bound");
}
void negativeTests() {
    reject("wrong stage", [](Pair& p){p.v.stage.kind = kStageFragment;});
    reject("missing chip provenance", [](Pair& p){p.v.stage.k.compiledChipId = 0;});
    reject("A6xx provenance", [](Pair& p){p.f.stage.k.compiledChipId = 0x06030500;});
    reject("missing bound push provenance", [](Pair& p){p.f.stage.k.a5xxBoundUboPushes = false;});
    reject("input count OOB", [](Pair& p){p.f.stage.numInputs = ~0u;});
    reject("output count OOB", [](Pair& p){p.v.stage.numOutputs = ~0u;});
    reject("missing position", [](Pair& p){p.v.stage.positionRegid = kNoRegid;});
    reject("missing raw position", [](Pair& p){p.v.stage.outputs[0].slot = 33;});
    reject("half position", [](Pair& p){p.v.stage.outputs[0].half = 1;});
    reject("duplicate VS slot", [](Pair& p){p.v.stage.outputs[1].slot = 0;});
    reject("missing producer", [](Pair& p){p.v.stage.outputs[1].slot = 33;});
    reject("missing name", [](Pair& p){p.v.varyingNames.clear();}, Interface::MatchingNames);
    reject("wrong name", [](Pair& p){p.f.varyingNames[0] = "uv";}, Interface::MatchingNames);
    reject("empty active name", [](Pair& p){p.f.varyingNames[0].clear();}, Interface::MatchingNames);
    reject("sparse mask", [](Pair& p){p.f.stage.inputs[0].compmask = 5;});
    reject("huge mask", [](Pair& p){p.f.stage.inputs[0].compmask = 255;});
    reject("input location OOB", [](Pair& p){p.f.stage.inputs[0].inloc = 255;});
    reject("varying limit", [](Pair& p){p.f.stage.totalIn = 125;});
    reject("sysval bary cannot bypass preflight", [](Pair& p){p.f.stage.inputs[1].bary = 1; p.f.stage.inputs[1].inloc = 255;});
    reject("flat", [](Pair& p){p.f.stage.inputs[0].flat = 1;});
    reject("half varying", [](Pair& p){p.f.stage.inputs[0].half = 1;});
    reject("vertex attributes", [](Pair& p){p.v.stage.numAttributes = 1;});
    reject("words count not repaired", [](Pair& p){p.v.stage.k.numWords = 31;});
    reject("immediates count not repaired", [](Pair& p){p.v.stage.k.numImmediates = 2;});
    reject("unbound resources", [](Pair& p){p.f.resources.emplace_back();});
    reject("memory effects", [](Pair& p){p.f.writesMemory = true;});
    reject("textures", [](Pair& p){p.f.stage.k.numSamp = 1;});
    reject("private memory", [](Pair& p){p.v.stage.k.pvtmemSize = 4;});
    reject("position footprint", [](Pair& p){p.v.stage.positionRegid = 200; p.v.stage.outputs[0].regid = 200;});
    reject("FS output footprint", [](Pair& p){p.f.stage.colorRegid[0] = p.f.stage.outputs[0].regid = 200;});
    reject("color class", [](Pair& p){p.f.stage.outputs[0].pad = kElemUint;});
    reject("second MRT", [](Pair& p){p.f.stage.numColorOutputs = 2;});
    reject("depth", [](Pair& p){p.f.stage.depthRegid = 4;});
    reject("centroid", [](Pair& p){p.f.stage.ijRegid[1] = 0;});
    reject("wrong bary reg", [](Pair& p){p.f.stage.ijRegid[0] = 2;});
    reject("non-zero-base id", [](Pair& p){p.v.stage.inputs[0].slot = 9;});
    reject("driver constants collision", [](Pair& p){p.v.stage.k.immediatesOffsetVec4 = 0;});
    auto p = fixture(); std::string why;
    for (auto bad : {link_a5xx::Images{{0,4096},{0x200000,4096}},
                     link_a5xx::Images{{0x100001,4096},{0x200000,4096}},
                     link_a5xx::Images{{0x100000,127},{0x200000,4096}},
                     link_a5xx::Images{{0x100000,4096},{0x100080,4096}},
                     link_a5xx::Images{{uint64_t(1)<<49,4096},{0x200000,4096}}}) {
        check(!Linked::link(p.v,p.f,bad,link_a5xx::ColorFormat::RGBA8Unorm,Interface::ExplicitSlots,&why), "bad image rejected"); ++refusals;
    }
    check(!Linked::link(p.v,p.f,images(),link_a5xx::ColorFormat(0),Interface::ExplicitSlots,&why), "unsupported color format");
    check(bool(link(p,&why,Interface::MatchingNames)), "matching names accepted when actually supplied");
    check(!link(p,&why,Interface(255)), "unknown interface contract rejected");
    p.v.varyingNames.clear(); p.f.varyingNames.clear();
    check(bool(link(p, &why)), "explicit slot interface without names");
}
void drawTests() {
    auto p = fixture(); auto linked = link(p); check(bool(linked), "draw fixture link");
    render_a5xx::ColorTarget t{0x300000, 16384, 64, 64, 256, 16384};
    render_a5xx::Scratch scratch{0x400000, 4};
    pm4::Stream s; render_a5xx::SysmemPass pass(s); std::string why;
    check(!linked->drawTriangles(s,pass,0,3,&why) && s.size() == 0, "inactive pass rejects without writes");
    check(pass.begin(t,scratch,&why), "real sysmem begin encoding");
    const auto before = s.words();
    check(!pass.drawTriangles(0,3,&why) && s.words() == before, "shared M0 refusal retained");
    check(!linked->drawTriangles(s,pass,1,3,&why) && s.words() == before, "first nonzero refusal atomic");
    check(!linked->drawTriangles(s,pass,0,2,&why) && s.words() == before, "nontriangle refusal atomic");
    check(!linked->drawTriangles(s,pass,0,0,&why) && s.words() == before, "empty draw refusal atomic");
    check(!linked->drawTriangles(s,pass,0,3,&why,s.size()) && s.words() == before, "draw word bound atomic");
    check(linked->drawTriangles(s,pass,0,3,&why), "linked draw callable inside existing pass");
    check(pass.end(&why), "real sysmem end encoding");
    auto d = decode(s); unsigned draws = 0;
    for (const auto& c : d.commands) if (c.first == a5xx::CP_DRAW_INDX_OFFSET) {
        ++draws; check(c.second.size() == 3 && c.second[0] == 0x84 && c.second[1] == 1 && c.second[2] == 3, "actual auto-index single-instance draw");
    }
    check(draws == 1 && d.regs.at(a5xx::REG_A5XX_RB_RENDER_COMPONENTS) == 15, "one draw one color");
    pm4::Stream other; render_a5xx::SysmemPass bgra(other); t.format = render_a5xx::ColorFormat::BGRA8Unorm;
    check(bgra.begin(t,scratch,&why), "BGRA pass"); const auto b = other.words();
    check(!linked->drawTriangles(other,bgra,0,3,&why) && b == other.words(), "format mismatch atomic");
    pm4::Stream alias; render_a5xx::SysmemPass aliasPass(alias); t.format = render_a5xx::ColorFormat::RGBA8Unorm; t.address = images().vs.address;
    check(aliasPass.begin(t,scratch,&why), "alias pass encoding"); const auto a = alias.words();
    check(!linked->drawTriangles(alias,aliasPass,0,3,&why) && a == alias.words(), "shader color alias atomic");
}
void locationsAndLimit() {
    // Every component width at every possible location, including bit31/63/95
    // and last legal location 123. Compare the ir3 native and portable builds
    // through identical packet digests saved by main().
    for (unsigned width = 1; width <= 4; ++width) for (unsigned loc = 0; loc + width <= 124; ++loc) {
        auto p = fixture(); p.f.stage.inputs[0].compmask = uint8_t((1u << width) - 1);
        p.f.stage.inputs[0].inloc = uint8_t(loc); p.f.stage.totalIn = loc + width;
        auto x = link(p); check(bool(x), "legal bounded width/location");
        const auto& l = x->linkage(); check(l.positionLoc == loc + width && l.maxLoc == loc + width + 4, "bounded position location");
        pm4::Stream encoded;
        check(x->encodeState(encoded), "emit bounded varying corpus");
        stateCorpus.push_back(uint32_t(encoded.size()));
        stateCorpus.insert(stateCorpus.end(), encoded.words().begin(), encoded.words().end());
        for (unsigned bit = 0; bit < 128; ++bit)
            check(bool(l.varmask[bit/32] & (1u << (bit%32))) == (bit >= loc && bit < loc+width), "varying mask excludes padding and position");
    }
    auto p = fixture(); p.v.varyingNames.clear(); p.f.varyingNames.clear();
    p.v.stage.numOutputs = 32; p.f.stage.numInputs = 32; p.f.stage.totalIn = 124;
    p.f.stage.inputs[31] = {56,0,3,124,1,0,0,0};
    for (unsigned i = 0; i < 31; ++i) {
        p.v.stage.outputs[i+1] = {uint8_t(32+i),8,0,kElemFloat};
        p.f.stage.inputs[i] = {uint8_t(32+i),kNoRegid,15,uint8_t(i*4),0,1,0,0};
    }
    auto x = link(p); check(bool(x) && x->linkage().cnt == 32 && x->linkage().maxLoc == 128, "full 32-entry linkage");
    pm4::Stream s; check(x->encodeState(s), "full linkage emitter ASan boundary"); decode(s);
    stateCorpus.push_back(uint32_t(s.size()));
    stateCorpus.insert(stateCorpus.end(), s.words().begin(), s.words().end());
    reject("overlapping locations", [](Pair& q){q.f.stage.inputs[2] = q.f.stage.inputs[0]; q.f.stage.inputs[2].slot = 33; q.f.stage.numInputs = 3; q.v.stage.outputs[2] = {33,8,0,kElemFloat}; q.v.stage.numOutputs = 3; q.v.varyingNames.clear(); q.f.varyingNames.clear();});
}
} // namespace

int main(int argc, char** argv) {
    happyAndOwnership(); negativeTests(); drawTests(); locationsAndLimit();
    auto p = fixture(); auto linked = link(p); pm4::Stream s; check(linked->encodeState(s), "deterministic stream");
    if (argc == 2) {
        std::ofstream f(argv[1], std::ios::binary);
        stateCorpus.push_back(uint32_t(s.size()));
        stateCorpus.insert(stateCorpus.end(), s.words().begin(), s.words().end());
        f.write(reinterpret_cast<const char*>(stateCorpus.data()), std::streamsize(stateCorpus.size()*4));
        check(bool(f), "save comparison stream");
    }
    std::printf("PASS a5xx-link checks=%u negative_cases=%u mesa_linker=%u state_words=%zu corpus_words=%zu hardware_executed=0\n",
                checks, refusals, link_a5xx::usesMesaLinker(), s.size(), stateCorpus.size());
}
