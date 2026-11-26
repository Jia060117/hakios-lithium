// SPDX-License-Identifier: MIT
#include "hakios-metal/compiler.hpp"
#include "hakios-metal/target_a5xx.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv) {
    assert(argc == 3);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> air((std::istreambuf_iterator<char>(input)), {});
    assert(!air.empty() && hakios::metal::hasCompiler());
    std::string why;
    auto result = hakios::metal::compileAir(air.data(), air.size(), &why);
    if (!result) { std::fprintf(stderr, "A530 compileAir: %s\n", why.c_str()); return 1; }
    const auto& k = result->kernel;
    assert(k.numWords && k.words && !k.mergedRegs);
    assert(result->name == "fill_id");
    assert(k.numUboRanges == 1);
    assert(k.numSsbos == 1);
    std::ofstream binary(argv[2], std::ios::binary);
    binary.write(reinterpret_cast<const char*>(k.words), k.numWords * 4);
    binary.close();
    assert(binary);
    std::printf("PASS actual port compileAir chip=%#x name=%s words=%u instrlen=%u constlen=%u "
                "merged=%u ubos=%u ssbos=%u\n", hakios::metal::target::kChipId,
                result->name.c_str(), k.numWords, k.instrlen, k.constlen,
                unsigned(k.mergedRegs), k.numUboRanges, k.numSsbos);
}
