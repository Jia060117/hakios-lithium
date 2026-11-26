// SPDX-License-Identifier: MIT
#include "hakios-metal/texture_a5xx.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <array>
using namespace hakios::metal::a5xx;
extern "C" void mesa_fdl5_oracle(unsigned, unsigned, unsigned*, uint64_t*);
int main() {
    unsigned cases = 0;
    for (unsigned width : {1u, 2u, 63u, 64u, 65u, 1080u, 4096u}) {
        for (unsigned height : {1u, 31u, 32u, 33u, 2040u}) {
            for (auto fmt : {LinearFormat::RGBA8, LinearFormat::BGRA8, LinearFormat::R32Uint}) {
                LinearTexture t;
                std::string why;
                assert(linearTexture(width, height, fmt, &t, &why));
                unsigned pitch; uint64_t layer;
                mesa_fdl5_oracle(width, height, &pitch, &layer);
                assert(t.pitch == pitch && t.layerBytes == layer);
                std::array<uint32_t, 12> desc;
                assert(textureDescriptor(t, 0x120000000ull, t.layerBytes, &desc, &why));
                assert(desc[1] == (width | (height << 15)));
                assert(desc[4] == 0x20000000u && desc[5] == 0x20001u);
                size_t stride = width * 4 + 13;
                std::vector<uint8_t> source(stride * height, 0x5a), mapping(t.layerBytes, 0xa5),
                                     dest(stride * height, 0xcc);
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width * 4; ++x)
                        source[y * stride + x] = uint8_t(x + y);
                assert(uploadRows(t, mapping.data(), mapping.size(), source.data(), source.size(), stride, &why));
                assert(downloadRows(t, dest.data(), dest.size(), stride, mapping.data(), mapping.size(), &why));
                for (unsigned y = 0; y < height; ++y) {
                    for (unsigned x = 0; x < width * 4; ++x) assert(dest[y * stride + x] == source[y * stride + x]);
                    for (unsigned x = width * 4; x < stride; ++x) assert(dest[y * stride + x] == 0xcc);
                    for (unsigned x = width * 4; x < t.pitch; ++x) assert(mapping[y * t.pitch + x] == 0xa5);
                }
                assert(!textureDescriptor(t, 3, t.layerBytes, &desc, &why));
                assert(!textureDescriptor(t, 0, t.layerBytes - 1, &desc, &why));
                assert(!uploadRows(t, mapping.data(), mapping.size(), source.data(), 0, stride, &why));
                assert(!uploadRows(t, mapping.data(), mapping.size(), mapping.data(), mapping.size(), t.pitch, &why));
                ++cases;
            }
        }
    }
    LinearTexture t;
    assert(!linearTexture(0, 1, LinearFormat::RGBA8, &t, nullptr));
    assert(!linearTexture(0xffffffffu, 1, LinearFormat::RGBA8, &t, nullptr));
    assert(!linearTexture(1, 1, LinearFormat(999), &t, nullptr));
    assert(!linearTexture(32767, 32767, LinearFormat::RGBA8, &t, nullptr));
    std::printf("PASS A5xx texture: %u Mesa fdl5 layout comparisons and row/descriptor groups\n", cases);
}
