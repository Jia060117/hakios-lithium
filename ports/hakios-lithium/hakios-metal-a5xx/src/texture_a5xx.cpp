// SPDX-License-Identifier: MIT
// Mesa 25.0.7: fdl/fd5_layout.c:34-43,60-63,92-94;
// a5xx/fd5_texture.c:133-135,170-185, fd5_format.c:131,162,172,405-421;
// registers/adreno/a5xx.xml:147-185,2906-2984.
#include "hakios-metal/texture_a5xx.hpp"
#include <cstring>
#include <limits>

namespace hakios::metal::a5xx {
namespace {
bool fail(std::string* why, const char* text) { if (why) *why = text; return false; }
bool validFormat(LinearFormat f) {
    return f == LinearFormat::RGBA8 || f == LinearFormat::BGRA8 || f == LinearFormat::R32Uint;
}
bool validLayout(const LinearTexture& t) {
    LinearTexture expected;
    return linearTexture(t.width, t.height, t.format, &expected, nullptr) &&
           expected.pitch == t.pitch && expected.layerBytes == t.layerBytes;
}
bool rowsFit(uint32_t height, size_t pitch, size_t rowBytes, size_t bytes) {
    if (pitch < rowBytes || bytes < rowBytes) return false;
    return height && size_t(height - 1) <= (bytes - rowBytes) / pitch;
}
}
bool linearTexture(uint32_t width, uint32_t height, LinearFormat format,
                   LinearTexture* result, std::string* why) {
    if (!result || !width || !height || width > 0x7fff || height > 0x7fff)
        return fail(why, "A5xx linear texture dimensions must fit the 15-bit descriptor fields");
    if (!validFormat(format)) return fail(why, "A5xx initial linear texture format is unsupported");
    // cpp=4 -> pitchalign=cpp_shift+6=8; final linear level height aligned32.
    uint64_t pitch = (uint64_t(width) * 4 + 255) & ~uint64_t(255);
    uint64_t rows = (uint64_t(height) + 31) & ~uint64_t(31);
    uint64_t layer = (pitch * rows + 4095) & ~uint64_t(4095);
    if ((layer >> 12) > 0x3fff)
        return fail(why, "A5xx ARRAY_PITCH exceeds its 14-bit page field");
    *result = {width, height, uint32_t(pitch), layer, format};
    return true;
}
bool textureDescriptor(const LinearTexture& t, uint64_t iova,
                       uint64_t allocationBytes, std::array<uint32_t, 12>* words,
                       std::string* why) {
    if (!words || !validLayout(t)) return fail(why, "invalid A5xx texture layout");
    if ((iova & 31) || iova >= (uint64_t(1) << 49) ||
        t.layerBytes > (uint64_t(1) << 49) - iova || allocationBytes < t.layerBytes)
        return fail(why, "A5xx texture address/alignment/allocation is invalid");
    // Descriptor format numbers are TFMT5, not RB5 formats. RGBA/BGRA have
    // the same TFMT and distinct util_format_description component swizzles.
    const uint32_t fmt = t.format == LinearFormat::R32Uint ? 0x4b : 0x30;
    uint32_t x = 0, y = 1, z = 2, w = 3;
    if (t.format == LinearFormat::BGRA8) { x = 2; z = 0; }
    if (t.format == LinearFormat::R32Uint) { y = 4; z = 4; w = 5; }
    std::array<uint32_t, 12> out{};
    out[0] = (fmt << 22) | (x << 4) | (y << 7) | (z << 10) | (w << 13);
    out[1] = t.width | (t.height << 15);
    out[2] = 2 | (t.pitch << 7) | (1u << 29); // pitchalign-6=2, TEX_2D=1
    out[3] = uint32_t(t.layerBytes >> 12);
    out[4] = uint32_t(iova);
    out[5] = uint32_t(iova >> 32) | (1u << 17); // depth1
    *words = out;
    return true;
}
bool uploadRows(const LinearTexture& t, void* mapping, size_t mappingBytes,
                const void* source, size_t sourceBytes, size_t sourcePitch,
                std::string* why) {
    if (!validLayout(t) || !mapping || !source || mappingBytes < t.layerBytes ||
        !rowsFit(t.height, sourcePitch, size_t(t.width) * 4, sourceBytes))
        return fail(why, "A5xx upload row extent is invalid");
    // Caller must provide independent CPU source and GPU mappings; overlap
    // would make row order observable. Refuse it, including padded extents.
    uintptr_t a = reinterpret_cast<uintptr_t>(mapping), b = reinterpret_cast<uintptr_t>(source);
    if (mappingBytes > UINTPTR_MAX - a || sourceBytes > UINTPTR_MAX - b ||
        (a < b + sourceBytes && b < a + mappingBytes))
        return fail(why, "A5xx texture upload mappings overlap");
    for (uint32_t row = 0; row < t.height; ++row)
        std::memcpy(static_cast<uint8_t*>(mapping) + size_t(row) * t.pitch,
                    static_cast<const uint8_t*>(source) + size_t(row) * sourcePitch,
                    size_t(t.width) * 4);
    return true;
}
bool downloadRows(const LinearTexture& t, void* destination, size_t destinationBytes,
                  size_t destinationPitch, const void* mapping, size_t mappingBytes,
                  std::string* why) {
    if (!validLayout(t) || !mapping || !destination || mappingBytes < t.layerBytes ||
        !rowsFit(t.height, destinationPitch, size_t(t.width) * 4, destinationBytes))
        return fail(why, "A5xx download row extent is invalid");
    uintptr_t a = reinterpret_cast<uintptr_t>(mapping), b = reinterpret_cast<uintptr_t>(destination);
    if (mappingBytes > UINTPTR_MAX - a || destinationBytes > UINTPTR_MAX - b ||
        (a < b + destinationBytes && b < a + mappingBytes))
        return fail(why, "A5xx texture download mappings overlap");
    for (uint32_t row = 0; row < t.height; ++row)
        std::memcpy(static_cast<uint8_t*>(destination) + size_t(row) * destinationPitch,
                    static_cast<const uint8_t*>(mapping) + size_t(row) * t.pitch,
                    size_t(t.width) * 4);
    return true;
}
} // namespace hakios::metal::a5xx
