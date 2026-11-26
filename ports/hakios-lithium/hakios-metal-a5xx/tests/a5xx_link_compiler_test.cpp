// SPDX-License-Identifier: MIT
// Actual AIR -> NIR -> ir3 -> link -> fd5 encoding. No GPU execution.
#include "hakios-metal/link_a5xx.hpp"
#include "hakios-metal/a5xx.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace hakios::metal;
int main(int argc, char** argv) {
    if (argc != 4 || !hasCompiler()) return 2;
    std::shared_ptr<CompiledStage> stages[2]; std::string why;
    for (unsigned i = 0; i < 2; ++i) {
        std::ifstream f(argv[i+1],std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
        if (data.empty()) return 3;
        stages[i] = compileStage(data.data(),data.size(),&why);
        if (!stages[i]) { std::fprintf(stderr,"compileStage: %s\n",why.c_str()); return 1; }
        const auto& c = *stages[i];
        std::printf("COMPILED %s chip=%08x words=%zu instructions128=%u constVec4=%u inputs=%u outputs=%u names=%zu\n",
            c.name.c_str(),c.stage.k.compiledChipId,c.words.size(),c.stage.k.instrlen,c.stage.k.constlen,c.stage.numInputs,c.stage.numOutputs,c.varyingNames.size());
        for (unsigned n=0;n<c.varyingNames.size();++n) std::printf("NAME slot=%u value=%s\n",32+n,c.varyingNames[n].c_str());
    }
    link_a5xx::Images images{{0x100000,4096},{0x200000,4096}}; // host encoding fixtures only
    // Both stages retain their own original AIR varying identities.
    auto byName = link_a5xx::Program::link(*stages[0],*stages[1],images,link_a5xx::ColorFormat::RGBA8Unorm,
                                         link_a5xx::VaryingInterface::MatchingNames,&why);
    if (!byName) { std::fprintf(stderr,"BY-NAME LINK FAILED: %s\n",why.c_str()); return 7; }
    std::printf("PASS source-backed MatchingNames\n");
    auto linked = link_a5xx::Program::link(*stages[0],*stages[1],images,link_a5xx::ColorFormat::RGBA8Unorm,
                                         link_a5xx::VaryingInterface::MatchingNames,&why);
    if (!linked) { std::fprintf(stderr,"LINK FAILED: %s\n",why.c_str()); return 1; }
    stages[0].reset(); stages[1].reset();
    pm4::Stream s; render_a5xx::SysmemPass pass(s);
    render_a5xx::ColorTarget color{0x300000,16384,64,64,256,16384};
    if (!pass.begin(color,{0x400000,4},&why) || !linked->drawTriangles(s,pass,0,6,&why) || !pass.end(&why)) {
        std::fprintf(stderr,"ENCODE FAILED: %s\n",why.c_str()); return 1;
    }
    unsigned draws = 0, loads = 0;
    for (size_t i=0;i<s.size();) {
        const auto h = pm4::decode(s.words()[i]);
        if (h.kind == pm4::Header::kBad || h.count > s.size()-i-1) return 4;
        if (h.kind == pm4::Header::kCommand) {
            draws += h.opcode == a5xx::CP_DRAW_INDX_OFFSET;
            loads += h.opcode == a5xx::CP_LOAD_STATE4 && ((s.words()[i+1]>>16)&3) == a5xx::SS4_INDIRECT;
        }
        i += size_t(h.count)+1;
    }
    if (draws != 1 || loads != 2) return 5;
    std::ofstream binary(argv[3],std::ios::binary);
    binary.write(reinterpret_cast<const char*>(s.words().data()),std::streamsize(s.size()*4));
    if (!binary) return 6;
    std::printf("PASS actual A530 AIR VS+FS link+sysmem draw encoding mesa_linker=%u words=%zu draw_packets=%u shader_loads=%u linkage_entries=%u stride=%u hardware_executed=0\n",
        link_a5xx::usesMesaLinker(),s.size(),draws,loads,linked->linkage().cnt,linked->linkage().maxLoc);
}
