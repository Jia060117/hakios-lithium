// SPDX-License-Identifier: MIT
// A530 facade for the C API adapter. No legacy CommandQueue/Texture encoder.
#pragma once
#include "hakios-metal/air_dispatch_a5xx.hpp"
#include "hakios-metal/link_a5xx.hpp"
#include "hakios-metal/texture_a5xx.hpp"
#include <optional>

namespace hakios::metal::backend_a5xx {

constexpr uint32_t kChipId = 0x05030004;
constexpr size_t kMaxBatchWords = 1024 * 1024; // software bound
using BufferRef = air_dispatch_a5xx::Argument;
using Resident = air_dispatch_a5xx::Resident;

// Memory keeps the backing Gpu context alive and implements CPU coherency.
// Tests can inject host storage. onGpu() supplies the real Gpu ABI adapter.
class Memory : public air_dispatch_a5xx::Memory {
public:
    virtual bool upload(const Buffer& buffer, const void* bytes, uint64_t size,
                        std::string* why) = 0;
    // Publish freshly staged shader/constants/command BOs before submit.
    virtual bool publish(const std::vector<Resident>& buffers, std::string* why) = 0;
    // Retain a submission with uncertain DMA lifetime. No reset/retry/free.
    // The real adapter holds it for process lifetime and refuses more submits.
    virtual void quarantine(std::shared_ptr<const void> submission) noexcept = 0;
    virtual bool canSubmit() const noexcept = 0;
    virtual Gpu* gpu() const noexcept = 0;
};

struct Texture {
    BufferRef storage; // full logical padded allocation; owns backing context
    a5xx::LinearTexture layout{};
    uint32_t usage = 0; // Metal usage bits; sampled shader work is not supported
};

struct Program {
    std::shared_ptr<const link_a5xx::Program> linked;
    BufferRef vertexImage, fragmentImage;
};

struct Draw {
    uint32_t primitive = 3; // MTLPrimitiveTypeTriangle
    uint32_t first = 0, count = 3, instances = 1;
    uint32_t loadAction = 1, storeAction = 1; // DontCare=0, Load=1, Clear=2; Store=1
    std::array<double, 4> clearColor{{0, 0, 0, 0}};
    std::optional<render_a5xx::Viewport> viewport;
    std::optional<render_a5xx::Rect> scissor;
};

class Batch;
class Backend final {
public:
    // No openKernelGpu side effect. Caller opens the admitted service and
    // supplies its shared Gpu. Exact CHIP_ID is queried; 0/A6xx are refused.
    static std::shared_ptr<Backend> onGpu(std::shared_ptr<Gpu> gpu, std::string* why = nullptr);
    // Explicit injection for an already identified context, including host
    // tests. Does not query/submit. The chip value must still be exact A530.
    static std::shared_ptr<Backend> onMemory(uint32_t chip, std::shared_ptr<Memory> memory,
                                           std::string* why = nullptr);
    BufferRef buffer(uint64_t logicalBytes, std::string* why = nullptr);
    std::shared_ptr<Texture> texture(uint32_t width, uint32_t height, uint32_t format,
                                     uint32_t usage, std::string* why = nullptr);
    // Full level only, one linear layer/sample. No legacy descriptor/layout.
    bool upload(const Texture& texture, const void* rows, size_t bytes, size_t pitch,
                std::string* why = nullptr);
    bool download(const Texture& texture, void* rows, size_t bytes, size_t pitch,
                  std::string* why = nullptr);
    std::shared_ptr<const CompiledKernel> compileCompute(const void* air, size_t bytes,
        const FunctionConstants& constants = {}, std::string* why = nullptr);
    std::shared_ptr<Program> compileProgram(const void* vs, size_t vsBytes,
        const void* fs, size_t fsBytes, uint32_t format,
        const FunctionConstants& vsConstants = {}, const FunctionConstants& fsConstants = {},
        std::string* why = nullptr);
    // Connect existing c_api CompiledStage owners without recompiling them.
    std::shared_ptr<Program> link(const CompiledStage& vs, const CompiledStage& fs,
                                  uint32_t format, std::string* why = nullptr);
    std::unique_ptr<Batch> batch(size_t maxWords = kMaxBatchWords);
private:
    explicit Backend(std::shared_ptr<Memory> memory) : memory_(std::move(memory)) {}
    std::shared_ptr<Memory> memory_;
};

class Batch final {
public:
    // FenceConfirmed means transport wait+unchanged FAULTS, not tested pixels
    // or a host shader interpreter. Ready is only successfully recorded PM4.
    enum class Status { Ready, Submitted, FenceConfirmed, Rejected, Uncertain };
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    bool copy(const BufferRef& source, uint64_t sourceOffset,
              const BufferRef& destination, uint64_t destinationOffset,
              uint64_t bytes, std::string* why = nullptr);
    bool fill(const BufferRef& destination, uint64_t offset, uint64_t bytes,
              uint8_t value, std::string* why = nullptr);
    // Existing compileAir result + explicit bound logical ranges. Texture,
    // sampler, threadgroup and argument-buffer calls must be refused by c_api
    // BEFORE this call; the production AIR bridge also validates resources.
    // Constants are CPU snapshots at record time. A constant BO written earlier
    // in this batch, or aliased by a writable SSBO here, is refused. Queue
    // adapters must record snapshots after prior dependent work has completed.
    bool dispatch(const CompiledKernel& compiled, const compute_a5xx::Grid& grid,
                  const std::vector<BufferRef>& arguments, std::string* why = nullptr);
    // One begin/draw/end per call. No textures, buffers, indices, depth or
    // indirect draw bindings; the c_api adapter rejects those explicitly.
    bool draw(const Program& program, const Texture& target, const Draw& draw,
              std::string* why = nullptr);
    bool clear(const Texture& target, const std::array<double,4>& color,
               std::string* why = nullptr);

    // Explicit transport operations; never called while preparing/compiling.
    // No automatic retries. Records are immutable after the first submit.
    bool submit(uint32_t queue, std::string* why = nullptr);
    bool wait(int64_t timeoutNs, std::string* why = nullptr);
    Status status() const noexcept;
    uint32_t fence() const noexcept;
    const std::vector<uint32_t>& words() const noexcept;
    std::vector<BoUse> boUses() const;
private:
    friend class Backend;
    struct State;
    Batch(std::shared_ptr<Memory> memory, size_t maxWords);
    std::shared_ptr<State> state_;
    bool append(const pm4::Stream& added, const std::vector<Resident>& residents,
                std::string* why);
};

} // namespace hakios::metal::backend_a5xx
