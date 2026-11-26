// SPDX-License-Identifier: MIT
// Official Mesa 25.0.7: fd5_emit.c:905-1135; fd5_emit.h:92-184;
// fd5_gmem.c:28-240,699-778; fd5_draw.c:220-328; fd5_format.c:162,172.
#include "hakios-metal/render_a5xx.hpp"
#include "hakios-metal/a5xx.hpp" // completed XML constants; no A6xx render code

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace hakios::metal::render_a5xx {
namespace {
using namespace a5xx;
bool fail(std::string* why, const char* message) { if (why) *why = message; return false; }
uint32_t bits(float value) { uint32_t word; std::memcpy(&word, &value, 4); return word; }
uint32_t xy(uint32_t x, uint32_t y) { return x | (y << 16); }
void reg(pm4::Stream& s, uint32_t index, uint32_t value) { s.registers(index, &value, 1); }
template<size_t N> void regs(pm4::Stream& s, uint32_t index, const uint32_t (&values)[N]) {
    s.registers(index, values, uint32_t(N));
}
template<size_t N> void cmd(pm4::Stream& s, uint32_t opcode, const uint32_t (&values)[N]) {
    s.packet(opcode, values, uint32_t(N));
}
void zeros(pm4::Stream& s, uint32_t index, uint32_t count) {
    const uint32_t zero[16]{};
    s.registers(index, zero, count);
}
void eventPacket(pm4::Stream& s, vgt_event_type type) {
    const uint32_t value = CP_EVENT_WRITE_0_EVENT(type);
    s.packet(CP_EVENT_WRITE, &value, 1);
}
void eventScratch(pm4::Stream& s, vgt_event_type type, uint64_t scratch) {
    const uint32_t payload[]{CP_EVENT_WRITE_0_EVENT(type), uint32_t(scratch),
                             uint32_t(scratch >> 32), 0};
    cmd(s, CP_EVENT_WRITE, payload);
}
void lrzFlush(pm4::Stream& s) {
    reg(s, REG_A5XX_GRAS_LRZ_CNTL, A5XX_GRAS_LRZ_CNTL_ENABLE);
    eventPacket(s, LRZ_FLUSH);
    reg(s, REG_A5XX_GRAS_LRZ_CNTL, 0);
}
void cacheFlush(pm4::Stream& s) {
    const uint32_t values[]{0, 0, 0, 0, 0x12};
    regs(s, REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_LO, values);
    s.waitForIdle();
}
void bypass(pm4::Stream& s) {
    const uint32_t values[]{CP_SET_RENDER_MODE_0_MODE(BYPASS), 0, 0, 0, 0};
    cmd(s, CP_SET_RENDER_MODE, values);
}

// fd5_emit_restore's complete fixed A530 (not A540) sequence, including its
// empirically named UNKNOWN registers. Mesa debug scratch markers are omitted
// (__EMIT_MARKER disabled). No query/prologue/indirect BO is emitted.
void restore(pm4::Stream& s) {
    bypass(s);
    cacheFlush(s);
    reg(s, REG_A5XX_HLSQ_UPDATE_CNTL, 0xfffff);
    reg(s, REG_A5XX_PC_RESTART_INDEX, 0xffffffff);
    reg(s, REG_A5XX_PC_RASTER_CNTL, 0x12);
    const uint32_t point[]{A5XX_GRAS_SU_POINT_MINMAX_MIN(1.0f) |
                           A5XX_GRAS_SU_POINT_MINMAX_MAX(4092.0f),
                           A5XX_GRAS_SU_POINT_SIZE(0.5f)};
    regs(s, REG_A5XX_GRAS_SU_POINT_MINMAX, point);
    reg(s, REG_A5XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_SCREEN_SCISSOR_CNTL, 0);
    reg(s, REG_A5XX_SP_VS_CONFIG_MAX_CONST, 0);
    reg(s, REG_A5XX_SP_FS_CONFIG_MAX_CONST, 0);
    zeros(s, REG_A5XX_UNKNOWN_E292, 2);
    reg(s, REG_A5XX_RB_MODE_CNTL, 0x44);
    reg(s, REG_A5XX_RB_DBG_ECO_CNTL, 0x00100000);
    reg(s, REG_A5XX_VFD_MODE_CNTL, 0);
    reg(s, REG_A5XX_PC_MODE_CNTL, 0x1f);
    reg(s, REG_A5XX_SP_MODE_CNTL, 0x1e);
    reg(s, REG_A5XX_SP_DBG_ECO_CNTL, 0x40000800);
    reg(s, REG_A5XX_TPL1_MODE_CNTL, 0x544);
    const uint32_t timeout[]{0x80, 0};
    regs(s, REG_A5XX_HLSQ_TIMEOUT_THRESHOLD_0, timeout);
    reg(s, REG_A5XX_VPC_DBG_ECO_CNTL, 0x400);
    reg(s, REG_A5XX_HLSQ_MODE_CNTL, 1);
    reg(s, REG_A5XX_VPC_MODE_CNTL, 0);
    const uint32_t drawState[]{CP_SET_DRAW_STATE_0_DISABLE_ALL_GROUPS, 0, 0};
    cmd(s, CP_SET_DRAW_STATE, drawState);
    reg(s, REG_A5XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_BIN_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SC_BIN_CNTL, 0);
    reg(s, REG_A5XX_VPC_FS_PRIMITIVEID_CNTL, 0xff);
    reg(s, REG_A5XX_VPC_SO_OVERRIDE, A5XX_VPC_SO_OVERRIDE_SO_DISABLE);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_BASE_LO(0), 3);
    zeros(s, REG_A5XX_VPC_SO_FLUSH_BASE_LO(0), 2);
    reg(s, REG_A5XX_PC_GS_PARAM, 0);
    reg(s, REG_A5XX_PC_HS_PARAM, 0);
    reg(s, REG_A5XX_TPL1_TP_FS_ROTATION_CNTL, 0);
    reg(s, REG_A5XX_UNKNOWN_E004, 0);
    reg(s, REG_A5XX_GRAS_SU_LAYERED, 0);
    reg(s, REG_A5XX_VPC_SO_BUF_CNTL, 0);
    reg(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(0), 0);
    reg(s, REG_A5XX_PC_GS_LAYERED, 0);
    reg(s, REG_A5XX_UNKNOWN_E5AB, 0);
    reg(s, REG_A5XX_UNKNOWN_E5C2, 0);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_BASE_LO(1), 3);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(1), 6);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(2), 6);
    zeros(s, REG_A5XX_VPC_SO_BUFFER_OFFSET(3), 3);
    reg(s, REG_A5XX_UNKNOWN_E5DB, 0);
    reg(s, REG_A5XX_SP_HS_CTRL_REG0, 0);
    reg(s, REG_A5XX_SP_GS_CTRL_REG0, 0);
    zeros(s, REG_A5XX_TPL1_VS_TEX_COUNT, 4);
    zeros(s, REG_A5XX_TPL1_FS_TEX_COUNT, 2);
    for (uint32_t index : {REG_A5XX_UNKNOWN_E7C0, REG_A5XX_UNKNOWN_E7C5,
                          REG_A5XX_UNKNOWN_E7CA, REG_A5XX_UNKNOWN_E7CF,
                          REG_A5XX_UNKNOWN_E7D4, REG_A5XX_UNKNOWN_E7D9})
        zeros(s, index, 3);
    reg(s, REG_A5XX_RB_CLEAR_CNTL, 0);
}

void depthOff(pm4::Stream& s) {
    zeros(s, REG_A5XX_RB_DEPTH_BUFFER_INFO, 5); // DEPTH5_NONE=0
    reg(s, REG_A5XX_GRAS_SU_DEPTH_BUFFER_INFO, 0);
    zeros(s, REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE_LO, 3);
    reg(s, REG_A5XX_RB_STENCIL_INFO, 0);
    // Additional deterministic depth/stencil off state from fd5_emit.c/zsa.c.
    reg(s, REG_A5XX_RB_DEPTH_CNTL, 0);
    reg(s, REG_A5XX_RB_DEPTH_PLANE_CNTL, 0);
    reg(s, REG_A5XX_GRAS_SU_DEPTH_PLANE_CNTL, 0);
    reg(s, REG_A5XX_RB_STENCIL_CONTROL, 0);
    reg(s, REG_A5XX_RB_ALPHA_CONTROL, 0);
}
void targetState(pm4::Stream& s, const ColorTarget& target) {
    const a3xx_color_swap swap = target.format == ColorFormat::BGRA8Unorm ? WXYZ : WZYX;
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t format = i ? 0 : uint32_t(RB5_R8G8B8A8_UNORM);
        const uint32_t info = format | A5XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(TILE5_LINEAR) |
            A5XX_RB_MRT_BUF_INFO_COLOR_SWAP(i ? WZYX : swap);
        const uint32_t mrt[]{info, i ? 0 : A5XX_RB_MRT_PITCH(target.pitchBytes),
            i ? 0 : A5XX_RB_MRT_ARRAY_PITCH(uint32_t(target.layerBytes)),
            i ? 0 : uint32_t(target.address), i ? 0 : uint32_t(target.address >> 32)};
        regs(s, REG_A5XX_RB_MRT_BUF_INFO(i), mrt);
        reg(s, REG_A5XX_SP_FS_MRT_REG(i), format);
        zeros(s, REG_A5XX_RB_MRT_FLAG_BUFFER(i), 4);
    }
    const uint32_t msaa[]{0, 4}; // SAMPLES=MSAA_ONE, DEST.MSAA_DISABLE
    regs(s, REG_A5XX_TPL1_TP_RAS_MSAA_CNTL, msaa);
    regs(s, REG_A5XX_RB_RAS_MSAA_CNTL, msaa);
    regs(s, REG_A5XX_GRAS_SC_RAS_MSAA_CNTL, msaa);
}
void scissorState(pm4::Stream& s, const Rect& rect) {
    const uint32_t values[]{xy(rect.x, rect.y), xy(rect.x + rect.width - 1, rect.y + rect.height - 1)};
    regs(s, REG_A5XX_GRAS_SC_SCREEN_SCISSOR_TL_0, values);
    regs(s, REG_A5XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0, values);
}
void viewportState(pm4::Stream& s, const Viewport& vp) {
    s.waitForIdle(); // fd5_emit.c:619
    const uint32_t values[]{bits(vp.x + vp.width * 0.5f), bits(vp.width * 0.5f),
        bits(vp.y + vp.height * 0.5f), bits(-vp.height * 0.5f),
        bits(vp.minDepth), bits(vp.maxDepth - vp.minDepth)};
    regs(s, REG_A5XX_GRAS_CL_VPORT_XOFFSET_0, values);
}
bool scratchValid(const Scratch& scratch, const ColorTarget& target, std::string* why) {
    constexpr uint64_t limit = uint64_t(1) << 49;
    if (!scratch.writable || scratch.bytes < 4 || (scratch.address & 3) ||
        scratch.address >= limit || scratch.bytes > limit - scratch.address)
        return fail(why, "invalid A5xx four-byte writable event scratch range");
    if (scratch.address < target.address + target.allocationBytes &&
        target.address < scratch.address + scratch.bytes)
        return fail(why, "event scratch overlaps color allocation");
    return true;
}
} // namespace

bool validTarget(const ColorTarget& t, std::string* why) {
    if (why) why->clear();
    if (!t.width || !t.height || t.width > 0x7fff || t.height > 0x7fff)
        return fail(why, "A5xx target dimensions must fit the 15-bit coordinates");
    if (t.format != ColorFormat::RGBA8Unorm && t.format != ColorFormat::BGRA8Unorm)
        return fail(why, "M0 color target supports only RGBA8/BGRA8 UNORM linear");
    if (!t.writable) return fail(why, "color target is not GPU-writable");
    const uint64_t paddedRows = (uint64_t(t.height) + 31) & ~uint64_t(31);
    const uint64_t extent = uint64_t(t.pitchBytes) * paddedRows;
    if (!t.pitchBytes || t.pitchBytes % 256 || t.pitchBytes < uint64_t(t.width) * 4 ||
        !t.layerBytes || t.layerBytes % 4096 || t.layerBytes < extent ||
        t.layerBytes > UINT32_MAX || t.allocationBytes < t.layerBytes)
        return fail(why, "invalid A5xx padded pitch/layer/allocation extent");
    constexpr uint64_t limit = uint64_t(1) << 49;
    if ((t.address & 63) || t.address >= limit || t.allocationBytes > limit - t.address)
        return fail(why, "invalid A5xx color address alignment/range");
    return true;
}

bool SysmemPass::append(const pm4::Stream& added, std::string* why) {
    if (stream_.size() > maxWords_ || added.size() > maxWords_ - stream_.size())
        return fail(why, "render software stream budget exceeded");
    // Copy existing prefix before mutation: stream append is strongly atomic,
    // including host allocation exceptions, without editing the shared Stream.
    try {
        pm4::Stream merged = stream_;
        for (size_t i = 0; i < added.size();) {
            const auto h = pm4::decode(added.words()[i]);
            const uint32_t* payload = added.words().data() + i + 1;
            if (h.kind == pm4::Header::kRegisters) merged.registers(h.reg, payload, h.count);
            else if (!h.count) {
                if (h.opcode == pm4::kWaitForIdle) merged.waitForIdle();
                else if (h.opcode == pm4::kWaitMemWrites) merged.waitMemWrites();
                else if (h.opcode == pm4::kWaitForMe) merged.waitForMe();
                else return fail(why, "internal unsupported empty render packet");
            } else merged.packet(h.opcode, payload, h.count);
            i += size_t(h.count) + 1;
        }
        stream_ = std::move(merged);
    } catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
      catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
    return true;
}

bool SysmemPass::begin(const ColorTarget& target, const Scratch& scratch, std::string* why) {
    if (why) why->clear();
    if (active_) return fail(why, "sysmem pass already active");
    if (!validTarget(target, why) || !scratchValid(scratch, target, why)) return false;
    try {
        pm4::Stream added;
        restore(added);
        lrzFlush(added);
        const uint32_t zero[]{0};
        cmd(added, CP_SKIP_IB2_ENABLE_GLOBAL, zero);
        eventPacket(added, PC_CCU_INVALIDATE_COLOR);
        reg(added, REG_A5XX_PC_POWER_CNTL, 3); // Mesa A530 num_sp_cores=4
        reg(added, REG_A5XX_VFD_POWER_CNTL, 3);
        added.waitForIdle();
        reg(added, REG_A5XX_RB_CCU_CNTL, 0x10000000);
        reg(added, REG_A5XX_RB_CNTL, A5XX_RB_CNTL_BYPASS);
        const uint32_t bounds[]{0, xy(target.width - 1, target.height - 1)};
        regs(added, REG_A5XX_GRAS_SC_WINDOW_SCISSOR_TL, bounds);
        regs(added, REG_A5XX_RB_RESOLVE_CNTL_1, bounds);
        reg(added, REG_A5XX_RB_WINDOW_OFFSET, 0);
        reg(added, REG_A5XX_VPC_SO_OVERRIDE, 0);
        const uint32_t visibility[]{1};
        cmd(added, CP_SET_VISIBILITY_OVERRIDE, visibility);
        depthOff(added);
        targetState(added, target);
        reg(added, REG_A5XX_RB_RENDER_CNTL, 8); // fd5_emit_render_cntl(!blit)
        reg(added, REG_A5XX_GRAS_SC_CNTL, 8);
        scissorState(added, {0, 0, target.width, target.height});
        viewportState(added, {0, 0, float(target.width), float(target.height), 0, 1});
        if (!append(added, why)) return false;
        target_ = target;
        scratch_ = scratch;
        active_ = true;
        return true;
    } catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
      catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
}

bool SysmemPass::setViewport(const Viewport& vp, std::string* why) {
    if (why) why->clear();
    if (!active_) return fail(why, "viewport outside an active sysmem pass");
    for (float value : {vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth})
        if (!std::isfinite(value)) return fail(why, "non-finite viewport");
    if (vp.x < 0 || vp.y < 0 || vp.width <= 0 || vp.height <= 0 ||
        double(vp.x) + vp.width > target_.width || double(vp.y) + vp.height > target_.height ||
        vp.minDepth < 0 || vp.maxDepth > 1 || vp.minDepth > vp.maxDepth)
        return fail(why, "viewport outside color target/depth range");
    try { pm4::Stream added; viewportState(added, vp); return append(added, why); }
    catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
    catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
}

bool SysmemPass::setScissor(const Rect& rect, std::string* why) {
    if (why) why->clear();
    if (!active_) return fail(why, "scissor outside an active sysmem pass");
    if (!rect.width || !rect.height || rect.x > target_.width || rect.y > target_.height ||
        rect.width > target_.width - rect.x || rect.height > target_.height - rect.y)
        return fail(why, "scissor outside color target or empty");
    try { pm4::Stream added; scissorState(added, rect); return append(added, why); }
    catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
    catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
}

bool SysmemPass::clearColor(const std::array<uint8_t, 4>& rgba, std::string* why) {
    if (why) why->clear();
    if (!active_) return fail(why, "clear outside an active sysmem pass");
    try {
        pm4::Stream added;
        reg(added, REG_A5XX_RB_RENDER_CNTL, 0); // blit=true; no binning/query
        reg(added, REG_A5XX_GRAS_SC_CNTL, 8);
        reg(added, REG_A5XX_RB_BLIT_CNTL, A5XX_RB_BLIT_CNTL_BUF(BLIT_MRT0));
        reg(added, REG_A5XX_RB_CLEAR_CNTL, A5XX_RB_CLEAR_CNTL_FAST_CLEAR | A5XX_RB_CLEAR_CNTL_MASK(0xf));
        // fd5_clear swaps logical R/B for BGRA, then util_pack_color_union
        // packs BGRA. The two swizzles cancel for the clear register word.
        const uint32_t color[]{uint32_t(rgba[0]) | (uint32_t(rgba[1]) << 8) |
                               (uint32_t(rgba[2]) << 16) | (uint32_t(rgba[3]) << 24), 0, 0, 0};
        regs(added, REG_A5XX_RB_CLEAR_COLOR_DW0, color);
        eventScratch(added, BLIT, scratch_.address);
        reg(added, REG_A5XX_RB_CLEAR_CNTL, 0); // do not leak fast clear into M3
        reg(added, REG_A5XX_RB_RENDER_CNTL, 8); // restore the nonblit state
        return append(added, why);
    } catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
      catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
}

bool SysmemPass::end(std::string* why) {
    if (why) why->clear();
    if (!active_) return fail(why, "no active sysmem pass to end");
    try {
        pm4::Stream added;
        const uint32_t zero[]{0};
        cmd(added, CP_SKIP_IB2_ENABLE_GLOBAL, zero);
        lrzFlush(added);
        eventScratch(added, PC_CCU_FLUSH_COLOR_TS, scratch_.address);
        eventScratch(added, PC_CCU_FLUSH_DEPTH_TS, scratch_.address);
        // fd5 sysmem fini above; explicit completion-order syntax for fragment.
        added.waitForIdle();
        added.waitMemWrites();
        added.waitForMe();
        if (!append(added, why)) return false;
        active_ = false;
        return true;
    } catch (const std::bad_alloc&) { return fail(why, "host render allocation failed"); }
      catch (const std::length_error&) { return fail(why, "host render allocation size failed"); }
}

bool SysmemPass::drawTriangles(uint32_t, uint32_t, std::string* why) {
    return fail(why, "draw refused: missing linked A5xx VS/FS shader state, VPC linkage and VFD metadata (M3)");
}
} // namespace hakios::metal::render_a5xx
