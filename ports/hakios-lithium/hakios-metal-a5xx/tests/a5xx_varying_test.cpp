// SPDX-License-Identifier: MIT
// Real local AIR -> NIR -> A530 ir3. Metadata/link/encoding checks only.
#include "hakios-metal/compiler.hpp"
#include "hakios-metal/link_a5xx.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace hakios::metal;
namespace {
unsigned checks = 0;
void require(bool result, const std::string& message) {
    ++checks;
    if (!result) { std::fprintf(stderr,"FAIL %s\n",message.c_str()); std::exit(1); }
}
std::string inputs;
std::shared_ptr<CompiledStage> compile(const char* file, const char* refusal = nullptr) {
    std::ifstream f(inputs + "/" + file + ".bc", std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
    require(!bytes.empty(), std::string("read ") + file);
    std::string why;
    std::shared_ptr<CompiledStage> c = compileStage(bytes.data(), bytes.size(), &why);
    if (refusal) {
        require(!c && why.find(refusal) != std::string::npos,
                std::string(file) + " must refuse " + refusal + "; got: " + why);
        std::printf("EXPECTED %s: %s\n",file,why.c_str());
    } else require(bool(c), std::string(file) + ": " + why);
    return c;
}
std::unique_ptr<link_a5xx::Program> matching(const CompiledStage& v, const CompiledStage& f,
                                           const char* refusal = nullptr) {
    std::string why;
    auto linked = link_a5xx::Program::link(v, f, {{0x100000,4096},{0x200000,4096}},
        link_a5xx::ColorFormat::RGBA8Unorm,link_a5xx::VaryingInterface::MatchingNames,&why);
    if (refusal) {
        require(!linked && why.find(refusal) != std::string::npos,
                std::string("MatchingNames must refuse ") + refusal + "; got: " + why);
        std::printf("EXPECTED link: %s\n",why.c_str());
    } else {
        if (!linked) for (const auto* c : {&v,&f})
            std::fprintf(stderr,"STAGE %s words=%zu/%u instr=%u imm=%zu/%u const=%u data=%u\n",
                c->name.c_str(),c->words.size(),c->stage.k.numWords,c->stage.k.instrlen,
                c->immediates.size(),c->stage.k.numImmediates,c->stage.k.constlen,c->stage.k.constantDataSize);
        require(bool(linked), "MatchingNames: " + why);
    }
    return linked;
}
// Canonical comparison excludes raw pointers, names and padding. Every word,
// immediate and raw IO entry is compared with the unchanged compiler build.
void snapshot(std::ostream& out, const CompiledStage& c) {
    const auto& s = c.stage; const auto& k = s.k;
    out << unsigned(s.kind) << ' ' << k.instrlen << ' ' << k.constlen << ' '
        << k.maxReg << ' ' << k.maxHalfReg << ' ' << k.driverParamsOffsetVec4 << ' '
        << s.totalIn << ' ' << s.positionRegid << ' ' << s.vertexIdRegid << '\n';
    for (auto w : c.words) out << std::hex << w << ' ';
    out << '\n'; for (auto w : c.immediates) out << w << ' ';
    out << std::dec << '\n' << s.numInputs << '\n';
    for (unsigned i=0;i<s.numInputs;++i) {
        const auto& x=s.inputs[i];
        out << unsigned(x.slot) << ' ' << unsigned(x.regid) << ' ' << unsigned(x.compmask) << ' '
            << unsigned(x.inloc) << ' ' << unsigned(x.sysval) << ' ' << unsigned(x.bary) << ' '
            << unsigned(x.flat) << ' ' << unsigned(x.half) << '\n';
    }
    out << s.numOutputs << '\n';
    for (unsigned i=0;i<s.numOutputs;++i) {
        const auto& x=s.outputs[i];
        out << unsigned(x.slot) << ' ' << unsigned(x.regid) << ' ' << unsigned(x.half) << ' ' << unsigned(x.pad) << '\n';
    }
    for (auto w : s.ijRegid) out << w << ' ';
    out << '\n'; for (auto w : s.colorRegid) out << w << ' '; out << '\n';
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5 || !hasCompiler()) return 2;
    inputs = argv[2];
    const bool baseline = std::string(argv[1]) == "baseline";
    auto v=compile("tri_color_vs"), f=compile("tri_color_fs");
    require(v->varyingNames == std::vector<std::string>{"generated(5colorDv4_f)"}, "actual VS source identity");
    std::ofstream binary(argv[3]); snapshot(binary,*v); snapshot(binary,*f);
    require(bool(binary),"save canonical old/new binary + IO snapshot");
    binary.close();
    if (baseline) {
        require(f->varyingNames.empty(),"old compiler reproduces missing FS identity");
        matching(*v,*f,"missing varyingNames");
        std::printf("PASS baseline checks=%u missing FS reproduced hardware_executed=0\n",checks);
        return 0;
    }
    require(f->varyingNames == v->varyingNames,"actual FS identity equals independently parsed VS identity");
    auto linked=matching(*v,*f);
    pm4::Stream stream; render_a5xx::SysmemPass pass(stream); std::string why;
    require(pass.begin({0x300000,16384,64,64,256,16384},{0x400000,4},&why),"sysmem begin: "+why);
    require(linked->drawTriangles(stream,pass,0,6,&why),"named real compiler draw encoding: "+why);
    require(pass.end(&why),"sysmem end: "+why);
    auto arg=compile("fs_argname");
    require(arg->varyingNames == f->varyingNames && arg->words == f->words,"air.arg_name is not the identity");
    matching(*v,*arg);
    auto decoy=compile("fs_decoy");
    require(decoy->varyingNames == f->varyingNames,"unrelated metadata cannot supply identity"); matching(*v,*decoy);
    auto wrong=compile("fs_wrong");
    require(wrong->varyingNames == std::vector<std::string>{"generated(5otherDv4_f)"},"wrong FS keeps its own source identity");
    require(wrong->words == f->words,"FS identity-only change does not rewrite binary");
    matching(*v,*wrong,"varying name mismatch");
    auto wrongV=compile("vs_wrong"); matching(*wrongV,*f,"varying name mismatch");
    auto missing=compile("fs_missing");
    require(missing->varyingNames.size()==1 && missing->varyingNames[0].empty(),"non-string identity remains missing");
    matching(*v,*missing,"missing varyingNames");
    auto empty=compile("fs_empty"); matching(*v,*empty,"missing varyingNames");
    auto escapedV=compile("vs_escaped"), escapedF=compile("fs_escaped");
    require(escapedV->varyingNames == std::vector<std::string>{"generated(5col\"orDv4_f)"} &&
            escapedV->varyingNames == escapedF->varyingNames,"exact decoded metadata string bytes");
    matching(*escapedV,*escapedF);
    compile("fs_nul","embedded NUL"); compile("vs_nul","embedded NUL");
    compile("fs_ambiguous","ambiguous interface keys");
    auto twoV=compile("two_vs"), twoF=compile("two_fs");
    require(twoV->varyingNames == std::vector<std::string>{"source.color", "source.uv"} &&
            twoF->varyingNames == twoV->varyingNames,"two source identities in frontend slot order");
    matching(*twoV,*twoF);
    auto reordered=compile("two_fs_reordered");
    require(reordered->varyingNames == std::vector<std::string>{"source.uv","source.color"},"FS preserves actual metadata order");
    matching(*twoV,*reordered,"varying name mismatch");
    compile("two_fs_duplicate","duplicate AIR varying identity");
    compile("two_vs_duplicate","duplicate AIR varying identity");
    // Writable cache round trip keeps both names. Old seed entries use the
    // SAME external build tag but must miss the new semantic cache namespace.
    require(setenv("HAKIOS_METAL_CACHE",argv[4],1)==0,"set test cache");
    auto cold=compile("tri_color_fs"), warm=compile("tri_color_fs");
    require(cold->varyingNames == f->varyingNames && warm->varyingNames == f->varyingNames &&
            warm->words == f->words,"cold/warm cached identities and binary");
    require(setenv("HAKIOS_METAL_CACHE","",1)==0,"clear cache");
    const std::string seed=std::string(argv[4])+"-old";
    require(setenv("HAKIOS_METAL_CACHE_SEED",seed.c_str(),1)==0,"set old seed");
    auto seeded=compile("tri_color_fs");
    require(seeded->varyingNames == f->varyingNames,"old FS-empty seed cannot bypass extraction");
    std::printf("PASS a5xx-varying checks=%u MatchingNames=real-source VSwords=%zu FSwords=%zu draw_words=%zu hardware_executed=0\n",
        checks,v->words.size(),f->words.size(),stream.size());
}
