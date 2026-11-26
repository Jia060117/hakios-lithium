// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace hakios::metal::a5xx {

// Initial single-level, single-layer, single-sample linear texture subset.
// Metal format numbers. Other formats require separate generation-specific work.
enum class LinearFormat : uint32_t { RGBA8 = 70, BGRA8 = 80, R32Uint = 53 };
struct LinearTexture {
    uint32_t width = 0, height = 0, pitch = 0;
    uint64_t layerBytes = 0;
    LinearFormat format = LinearFormat::RGBA8;
};
bool linearTexture(uint32_t width, uint32_t height, LinearFormat format,
                   LinearTexture* result, std::string* why);
bool textureDescriptor(const LinearTexture& texture, uint64_t iova,
                       uint64_t allocationBytes, std::array<uint32_t, 12>* words,
                       std::string* why);
bool uploadRows(const LinearTexture& texture, void* gpuMapping, size_t mappingBytes,
                const void* source, size_t sourceBytes, size_t sourcePitch,
                std::string* why);
bool downloadRows(const LinearTexture& texture, void* destination, size_t destinationBytes,
                  size_t destinationPitch, const void* gpuMapping, size_t mappingBytes,
                  std::string* why);
} // namespace hakios::metal::a5xx
