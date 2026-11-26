// SPDX-License-Identifier: MIT
// Explicit hardware probe: never launched as a side effect of building.
#include "hakios-metal/gpu.hpp"
#include "hakios-metal/target_a5xx.hpp"
#include "hakios-metal/transfer_a5xx.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace hakios::metal;
namespace a5 = hakios::metal::a5xx;
namespace {
constexpr int64_t kWaitNs = 100000000; // each wait is bounded100ms
constexpr uint32_t kCpuPrepRead = 1; // MSM_PREP_READ, pinned drm_msm.h
constexpr auto kBudget = std::chrono::seconds(10);
}
int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "--hardware") != 0) {
        std::fprintf(stderr, "usage: probe-a5xx --hardware (requires completed A530 G4 backend)\n");
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    std::string why;
    auto transport = openKernelGpu(&why);
    if (!transport) { std::fprintf(stderr, "LITHIUM GPU probe status=no_service reason=%s\n", why.c_str()); return 3; }
    Gpu gpu(std::move(transport));
    const uint32_t chip = gpu.chipId();
    if (chip != target::kChipId) {
        std::fprintf(stderr, "LITHIUM GPU probe status=wrong_chip actual=%#x expected=%#x\n", chip, target::kChipId);
        return 4;
    }
    const uint64_t faultsBefore = gpu.faults();
    uint32_t queue = 0, previousFence = 0;
    int error = gpu.newQueue(0, &queue);
    if (error) { std::fprintf(stderr, "LITHIUM GPU probe queue_error=%d\n", error); return 5; }
    unsigned completed = 0;
    for (uint32_t bytes : {4u, 28u, 256u, 4092u, 4096u, 65536u}) {
        if (std::chrono::steady_clock::now() - start >= kBudget) { error = 110; break; }
        auto source = gpu.newBo(bytes + 128, kBoWriteCombined, &error);
        if (!source) break;
        auto destination = gpu.newBo(bytes + 128, kBoWriteCombined, &error);
        if (!destination) break;
        // Objects expose their rounded mapped extent. Maintain guard bytes on
        // both ends of the tested interval and compare every mapped byte.
        std::memset(source->cpu(), 0xa5, size_t(source->size()));
        std::memset(destination->cpu(), 0x3c, size_t(destination->size()));
        std::vector<uint8_t> expected(destination->size(), 0x3c);
        for (uint32_t i = 0; i < bytes; ++i)
            static_cast<uint8_t*>(source->cpu())[64+i] = uint8_t(i * 17 + bytes);
        // Separate fences/readbacks: a copy must not hide a failed fill.
        for (unsigned operation = 0; operation < 2 && !error; ++operation) {
            a5::pm4::Stream stream;
            a5::TransferEncoder transfer(stream);
            const auto encoded = operation == 0
                ? transfer.fill({destination->gpuAddress(), destination->size(), true, true}, 64, bytes, 0x5a)
                : transfer.copy({source->gpuAddress(), source->size(), true, false}, 64,
                                {destination->gpuAddress(), destination->size(), true, true}, 64, bytes);
            if (!encoded) { error = 22; break; }
            const size_t before = stream.size();
            if (transfer.fill({destination->gpuAddress(), destination->size(), true, true}, 65, 3, 0) ||
                stream.size() != before) { error = 22; break; }
            auto commands = gpu.newBo(stream.size() * 4, kBoWriteCombined, &error);
            if (!commands) break;
            std::memcpy(commands->cpu(), stream.words().data(), stream.size() * 4);
            uint32_t fence = 0;
            error = gpu.submit(queue, *commands, 0, uint32_t(stream.size()),
                               {{source.get(), false}, {destination.get(), true}}, &fence);
            if (!error && (fence == 0 || int32_t(fence - previousFence) <= 0)) error = 5;
            if (!error) error = gpu.wait(queue, fence, kWaitNs);
            if (!error) error = gpu.cpuPrep(*destination, kCpuPrepRead, kWaitNs);
            if (error) {
                // G4 must quarantine live DMA pages on timeout/client close.
                // No reset, resubmit or presumed CPU-only free in this probe.
                std::fprintf(stderr, "LITHIUM GPU probe status=submit_or_wait_failed error=%d fence=%u\n", error, fence);
                break;
            }
            if (operation == 0) std::memset(expected.data()+64, 0x5a, bytes);
            else std::memcpy(expected.data()+64, static_cast<const uint8_t*>(source->cpu())+64, bytes);
            if (std::memcmp(destination->cpu(), expected.data(), expected.size()) != 0) error = 84;
            gpu.cpuFini(*destination);
            if (!error) {
                previousFence = fence;
                std::printf("LITHIUM GPU probe operation=%s bytes=%u fence=%u pixel_bytes_equal=1\n",
                            operation == 0 ? "fill" : "copy", bytes, fence);
            }
        }
        if (error) break;
        ++completed;
    }
    gpu.closeQueue(queue);
    const uint64_t faultsAfter = gpu.faults();
    if (faultsBefore != faultsAfter) error = 5;
    std::printf("LITHIUM GPU probe status=%s cases=%u faults_before=%llu faults_after=%llu error=%d\n",
        !error && completed == 6 ? "passed" : "failed", completed,
        (unsigned long long)faultsBefore, (unsigned long long)faultsAfter, error);
    return !error && completed == 6 ? 0 : 1;
}
