// Opt-in so the unchanged Makefile wildcard remains compatible.
#if defined(HAKIOS_A5XX_RENDER_TEST_STANDALONE) || defined(HAKIOS_A5XX_RENDER_TEST_INTEGRATED)
#include "hakios-metal/render_a5xx.hpp"
#include "test.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>
#include "mesa_mocks.hpp"

namespace {
using namespace mesa; // only the independent official generated C constants
namespace r5 = hakios::metal::render_a5xx;
namespace pm = hakios::metal::pm4;
uint64_t checks = 0;
void verify(bool okay, const char* text, int line) {
    ++checks;
    if (!okay) {
        std::fprintf(stderr, "FAIL a5xx_render_test.cpp:%d: %s\n", line, text);
        ++test::failures();
    }
}
#define VERIFY(value) verify(bool(value), #value, __LINE__)
r5::ColorTarget target(uint32_t width = 320, uint32_t height = 240, bool bgra = false) {
    const uint32_t pitch = (width * 4 + 255) & ~255u;
    const uint64_t layer = (uint64_t(pitch) * ((height + 31) & ~31u) + 4095) & ~uint64_t(4095);
    return {0x123400001000ull, layer, width, height, pitch, layer,
            bgra ? r5::ColorFormat::BGRA8Unorm : r5::ColorFormat::RGBA8Unorm, true};
}
r5::Scratch scratch() { return {0x123500002000ull, 4, true}; }
unsigned parity(uint32_t value) {
    unsigned sum = 0;
    while (value) { sum += value & 1; value >>= 1; }
    return (sum & 1u) ^ 1u;
}
uint32_t bits(float value) { uint32_t out; std::memcpy(&out, &value, 4); return out; }
uint32_t t7(uint32_t op, uint32_t count) {
    return 0x70000000u | (op << 16) | (parity(op) << 23) | count | (parity(count) << 15);
}
void registerOracle(std::vector<uint32_t>& w, uint32_t reg, std::initializer_list<uint32_t> values) {
    w.push_back(mesa::pm4_pkt4_hdr(uint16_t(reg), uint16_t(values.size())));
    w.insert(w.end(), values);
}
void additionalBeginOracle(std::vector<uint32_t>& w, const r5::ColorTarget& t) {
    // Extra deterministic states beyond sysmem_prep, witnessed in fd5_emit/zsa.
    // Insert these separately at the depth-off point when comparing below.
    registerOracle(w, REG_A5XX_RB_RENDER_CNTL, {8});
    registerOracle(w, REG_A5XX_GRAS_SC_CNTL, {8});
    const uint32_t bottom = (t.width - 1) | ((t.height - 1) << 16);
    registerOracle(w, REG_A5XX_GRAS_SC_SCREEN_SCISSOR_TL_0, {0, bottom});
    registerOracle(w, REG_A5XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0, {0, bottom});
    w.push_back(mesa::pm4_pkt7_hdr(mesa::CP_WAIT_FOR_IDLE, 0));
    registerOracle(w, REG_A5XX_GRAS_CL_VPORT_XOFFSET_0,
        {bits(t.width * 0.5f), bits(t.width * 0.5f), bits(t.height * 0.5f),
         bits(-float(t.height) * 0.5f), bits(0), bits(1)});
}
std::vector<uint32_t> beginOracle(mesa::Harness& h, const r5::ColorTarget& t) {
    h.begin();
    auto expected = h.ring.words;
    // Insert explicit depth/stencil disable registers immediately after emit_zs.
    const uint32_t depthHeader = mesa::pm4_pkt4_hdr(REG_A5XX_RB_STENCIL_INFO, 1);
    auto found = std::find(expected.begin(), expected.end(), depthHeader);
    VERIFY(found != expected.end());
    std::vector<uint32_t> off;
    registerOracle(off, REG_A5XX_RB_DEPTH_CNTL, {0});
    registerOracle(off, REG_A5XX_RB_DEPTH_PLANE_CNTL, {0});
    registerOracle(off, REG_A5XX_GRAS_SU_DEPTH_PLANE_CNTL, {0});
    registerOracle(off, REG_A5XX_RB_STENCIL_CONTROL, {0});
    registerOracle(off, REG_A5XX_RB_ALPHA_CONTROL, {0});
    if (found != expected.end()) expected.insert(found + 2, off.begin(), off.end());
    additionalBeginOracle(expected, t);
    return expected;
}
void equalWords(const std::vector<uint32_t>& actual, const std::vector<uint32_t>& expected) {
    VERIFY(actual.size() == expected.size());
    for (size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
        if (actual[i] != expected[i])
            std::fprintf(stderr, "  DWORD %zu actual=%08x mesa=%08x\n", i, actual[i], expected[i]);
        VERIFY(actual[i] == expected[i]);
    }
}

// Test-only structural fake. It owns NO pixel bytes and has no shader evaluator.
// Header decoding is independent of Stream/decode. The register whitelist comes
// from the official-function oracle's writes, not from the encoder being tested.
struct StructuralFake {
    std::set<uint32_t> allowed;
    std::map<uint32_t, uint32_t> regs;
    size_t clearEvents = 0;
    bool lastWfi = false, ccuOrdered = false;
    bool colorFlushed = false, depthFlushed = false;
    explicit StructuralFake(const std::vector<uint32_t>& sourceOracle) {
        for (size_t i = 0; i < sourceOracle.size();) {
            const uint32_t header = sourceOracle[i], kind = header >> 28;
            const uint32_t count = header & (kind == 4 ? 0x7f : 0x3fff);
            if (kind == 4) for (uint32_t j = 0; j < count; ++j)
                allowed.insert(((header >> 8) & 0x3ffff) + j);
            i += count + 1;
        }
        allowed.insert(REG_A5XX_RB_BLIT_CNTL);
        allowed.insert(REG_A5XX_RB_CLEAR_CNTL);
        for (uint32_t i = 0; i < 4; ++i) allowed.insert(REG_A5XX_RB_CLEAR_COLOR_DW0 + i);
    }
    bool accept(const std::vector<uint32_t>& words, const r5::ColorTarget& t, r5::Scratch scratch) {
        regs.clear(); clearEvents = 0; ccuOrdered = false;
        colorFlushed = false; depthFlushed = false; lastWfi = false;
        for (size_t i = 0; i < words.size();) {
            const uint32_t h = words[i], kind = h >> 28;
            const uint32_t count = h & (kind == 4 ? 0x7f : 0x3fff);
            if (count > words.size() - i - 1) return false;
            const uint32_t* p = words.data() + i + 1;
            if (kind == 4) {
                const uint32_t reg = (h >> 8) & 0x3ffff;
                if ((h & 0x04000000u) || !reg || !count || count >= 127 ||
                    ((h >> 7) & 1) != parity(count) || ((h >> 27) & 1) != parity(reg))
                    return false;
                for (uint32_t j = 0; j < count; ++j) {
                    if (!allowed.count(reg + j)) return false;
                    regs[reg + j] = p[j];
                    if (reg + j == REG_A5XX_RB_CCU_CNTL) ccuOrdered = lastWfi && p[j] == 0x10000000;
                }
                lastWfi = false;
            } else if (kind == 7) {
                const uint32_t op = (h >> 16) & 0x7f;
                if ((h & 0x0f004000u) || ((h >> 15) & 1) != parity(count) ||
                    ((h >> 23) & 1) != parity(op)) return false;
                if (op == 0x26 || op == 0x12 || op == 0x13) { if (count) return false; }
                else if (op == mesa::CP_SET_RENDER_MODE) {
                    if (count != 5 || p[0] != 1 || p[1] || p[2] || p[3] || p[4]) return false;
                } else if (op == mesa::CP_SET_DRAW_STATE) {
                    if (count != 3 || p[0] != 0x40000 || p[1] || p[2]) return false;
                } else if (op == mesa::CP_SKIP_IB2_ENABLE_GLOBAL) {
                    if (count != 1 || p[0]) return false;
                } else if (op == mesa::CP_SET_VISIBILITY_OVERRIDE) {
                    if (count != 1 || p[0] != 1) return false;
                } else if (op == mesa::CP_EVENT_WRITE) {
                    if (!count) return false;
                    if (p[0] == mesa::PC_CCU_INVALIDATE_COLOR || p[0] == mesa::LRZ_FLUSH) {
                        if (count != 1) return false;
                    } else if (p[0] == mesa::BLIT || p[0] == mesa::PC_CCU_FLUSH_COLOR_TS ||
                               p[0] == mesa::PC_CCU_FLUSH_DEPTH_TS) {
                        if (count != 4 || p[3] ||
                            (uint64_t(p[1]) | uint64_t(p[2]) << 32) != scratch.address) return false;
                        if (p[0] == mesa::BLIT) {
                            const uint32_t format = t.format == r5::ColorFormat::BGRA8Unorm ? 0x2030 : 0x30;
                            if (!ccuOrdered || regs[REG_A5XX_RB_MRT_BUF_INFO(0)] != format ||
                                regs[REG_A5XX_RB_MRT_PITCH(0)] != t.pitchBytes / 64 ||
                                regs[REG_A5XX_RB_MRT_ARRAY_PITCH(0)] != t.layerBytes / 64 ||
                                regs[REG_A5XX_RB_MRT_BASE_LO(0)] != uint32_t(t.address) ||
                                regs[REG_A5XX_RB_MRT_BASE_HI(0)] != uint32_t(t.address >> 32) ||
                                regs[REG_A5XX_RB_DEPTH_BUFFER_INFO] || regs[REG_A5XX_RB_DEPTH_CNTL] ||
                                regs[REG_A5XX_RB_STENCIL_CONTROL] ||
                                regs[REG_A5XX_RB_CLEAR_CNTL] != 0xf2 ||
                                regs[REG_A5XX_RB_BLIT_CNTL] || regs[REG_A5XX_RB_RENDER_CNTL])
                                return false;
                            ++clearEvents; colorFlushed = depthFlushed = false;
                        } else if (p[0] == mesa::PC_CCU_FLUSH_COLOR_TS) colorFlushed = true;
                        else depthFlushed = true;
                    } else return false;
                } else return false; // in particular no shader loads or draw packets
                lastWfi = op == 0x26;
            } else return false;
            i += size_t(count) + 1;
        }
        return ccuOrdered && colorFlushed && depthFlushed && regs[REG_A5XX_RB_CLEAR_CNTL] == 0;
    }
};
} // namespace

TEST(a5xx_render_begin_end_matches_unmodified_mesa_fd5_functions) {
    for (bool bgra : {false, true}) for (uint32_t width : {1u, 64u, 65u, 320u, 1080u}) {
        const auto t = target(width, 37, bgra);
        pm::Stream stream; r5::SysmemPass pass(stream);
        std::string why = "old";
        VERIFY(pass.begin(t, scratch(), &why)); VERIFY(why.empty() && pass.active());
        mesa::Harness h(t.address, scratch().address, t.width, t.height, t.pitchBytes, uint32_t(t.layerBytes), bgra);
        auto expected = beginOracle(h, t);
        equalWords(stream.words(), expected);
        const size_t original = h.ring.words.size();
        h.end();
        expected.insert(expected.end(), h.ring.words.begin() + original, h.ring.words.end());
        expected.insert(expected.end(), {t7(0x26, 0), t7(0x12, 0), t7(0x13, 0)});
        VERIFY(pass.end(&why)); VERIFY(!pass.active() && why.empty());
        equalWords(stream.words(), expected);
        StructuralFake fake(beginOracle(h, t));
        VERIFY(fake.accept(stream.words(), t, scratch()));
        VERIFY(fake.clearEvents == 0);
    }
}

TEST(a5xx_render_clear_matches_source_for_rgba_bgra_and_byte_colors) {
    std::mt19937 random(0xa530c1ea);
    for (unsigned i = 0; i < 512; ++i) {
        const auto t = target(1 + random() % 320, 1 + random() % 241, i & 1);
        pm::Stream stream; r5::SysmemPass pass(stream);
        VERIFY(pass.begin(t, scratch()));
        const size_t before = stream.size();
        std::array<uint8_t, 4> color{uint8_t(random()), uint8_t(random()), uint8_t(random()), uint8_t(random())};
        VERIFY(pass.clearColor(color));
        mesa::Harness h(t.address, scratch().address, t.width, t.height, t.pitchBytes,
                        uint32_t(t.layerBytes), i & 1);
        h.clear(color[0], color[1], color[2], color[3]);
        // The adapter restores the previous nonblit RB_RENDER_CNTL after Mesa's
        // clear fragment; it has not emitted or linked graphics shader state.
        registerOracle(h.ring.words, REG_A5XX_RB_RENDER_CNTL, {8});
        equalWords(std::vector<uint32_t>(stream.words().begin() + before, stream.words().end()),
                   h.ring.words);
        VERIFY(pass.end());
        StructuralFake fake(beginOracle(h, t));
        VERIFY(fake.accept(stream.words(), t, scratch()));
        VERIFY(fake.clearEvents == 1);
    }
}

TEST(a5xx_render_viewport_scissor_source_fields_and_full_clear) {
    const auto t = target();
    pm::Stream stream; r5::SysmemPass pass(stream);
    VERIFY(pass.begin(t, scratch()));
    size_t before = stream.size();
    VERIFY(pass.setViewport({1, 2, 30, 40, 0.25f, 0.75f}));
    std::vector<uint32_t> expected{t7(0x26, 0)};
    registerOracle(expected, REG_A5XX_GRAS_CL_VPORT_XOFFSET_0,
        {bits(16), bits(15), bits(22), bits(-20), bits(.25f), bits(.5f)});
    equalWords(std::vector<uint32_t>(stream.words().begin() + before, stream.words().end()), expected);
    before = stream.size();
    VERIFY(pass.setScissor({2, 3, 1, 1}));
    expected.clear();
    registerOracle(expected, REG_A5XX_GRAS_SC_SCREEN_SCISSOR_TL_0, {0x30002, 0x30002});
    registerOracle(expected, REG_A5XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0, {0x30002, 0x30002});
    equalWords(std::vector<uint32_t>(stream.words().begin() + before, stream.words().end()), expected);
    VERIFY(pass.clearColor({0x12, 0x34, 0x56, 0x78})); VERIFY(pass.end());
    mesa::Harness h(t.address, scratch().address, t.width, t.height, t.pitchBytes, uint32_t(t.layerBytes), false);
    StructuralFake fake(beginOracle(h, t));
    VERIFY(fake.accept(stream.words(), t, scratch()));
    VERIFY(fake.regs[REG_A5XX_RB_RESOLVE_CNTL_1] == 0);
    VERIFY(fake.regs[REG_A5XX_RB_RESOLVE_CNTL_2] == (319u | (239u << 16)));
}

TEST(a5xx_render_invalid_targets_scratch_lifecycle_and_draw_refuse_atomically) {
    const auto good = target(); const auto goodScratch = scratch();
    pm::Stream stream; stream.nop(1);
    r5::SysmemPass pass(stream);
    const auto prefix = stream.words();
    std::string why;
    VERIFY(!pass.end(&why)); VERIFY(!why.empty());
    VERIFY(!pass.clearColor({0,0,0,0}, &why));
    VERIFY(!pass.setViewport({}, &why)); VERIFY(!pass.setScissor({}, &why));
    VERIFY(!pass.drawTriangles(0, 3, &why)); VERIFY(why.find("missing linked A5xx") != std::string::npos);
    std::vector<r5::ColorTarget> bad;
    auto put = [&](auto mutate) { auto t = good; mutate(t); bad.push_back(t); };
    put([](auto& t){ t.width = 0; }); put([](auto& t){ t.height = 0; });
    put([](auto& t){ t.width = 0x8000; }); put([](auto& t){ t.height = UINT32_MAX; });
    put([](auto& t){ t.pitchBytes = 0; }); put([](auto& t){ t.pitchBytes -= 1; });
    put([](auto& t){ t.pitchBytes = 256; }); put([](auto& t){ t.layerBytes = 0; });
    put([](auto& t){ t.layerBytes -= 1; }); put([](auto& t){ t.layerBytes = 4096; });
    put([](auto& t){ t.layerBytes = uint64_t(1) << 32; });
    put([](auto& t){ t.allocationBytes = t.layerBytes - 1; });
    put([](auto& t){ t.address |= 1; }); put([](auto& t){ t.address = UINT64_MAX; });
    put([](auto& t){ t.address = uint64_t(1) << 49; });
    put([](auto& t){ t.allocationBytes = UINT64_MAX; }); put([](auto& t){ t.writable = false; });
    put([](auto& t){ t.format = r5::ColorFormat(71); });
    for (const auto& t : bad) {
        VERIFY(!r5::validTarget(t, &why)); VERIFY(!why.empty());
        VERIFY(!pass.begin(t, goodScratch, &why)); VERIFY(!pass.active());
        VERIFY(stream.words() == prefix);
    }
    for (auto s : {r5::Scratch{goodScratch.address, 3}, r5::Scratch{goodScratch.address + 1, 4},
                   r5::Scratch{UINT64_MAX - 3, 4}, r5::Scratch{good.address, 4},
                   r5::Scratch{good.address - 4, 8}, r5::Scratch{goodScratch.address, UINT64_MAX},
                   r5::Scratch{goodScratch.address, 4, false}}) {
        VERIFY(!pass.begin(good, s, &why)); VERIFY(stream.words() == prefix);
    }
    VERIFY(pass.begin(good, goodScratch));
    const auto activePrefix = stream.words();
    VERIFY(!pass.begin(good, goodScratch, &why));
    for (const auto& rect : {r5::Rect{}, r5::Rect{320,0,1,1}, r5::Rect{0,240,1,1},
                             r5::Rect{0,0,UINT32_MAX,1}, r5::Rect{UINT32_MAX,0,1,1}})
        VERIFY(!pass.setScissor(rect, &why));
    for (const auto& vp : {r5::Viewport{}, r5::Viewport{-1,0,1,1,0,1},
        r5::Viewport{0,0,321,1,0,1}, r5::Viewport{0,0,1,241,0,1},
        r5::Viewport{0,0,1,1,0.8f,0.2f}, r5::Viewport{0,0,1,1,-.1f,1},
        r5::Viewport{0,0,1,1,0,1.1f}, r5::Viewport{NAN,0,1,1,0,1},
        r5::Viewport{0,0,INFINITY,1,0,1}})
        VERIFY(!pass.setViewport(vp, &why));
    VERIFY(!pass.drawTriangles(0, 3, &why)); VERIFY(!pass.drawTriangles(UINT32_MAX, UINT32_MAX, &why));
    VERIFY(stream.words() == activePrefix);
    VERIFY(pass.end()); VERIFY(!pass.active());
}

TEST(a5xx_render_budget_edges_and_source_structural_mutations) {
    const auto t = target();
    pm::Stream original; r5::SysmemPass base(original);
    VERIFY(base.begin(t, scratch()));
    const size_t beginWords = original.size();
    VERIFY(base.clearColor({1,2,3,4}));
    const size_t clearWords = original.size() - beginWords;
    VERIFY(base.end());
    const size_t endWords = original.size() - beginWords - clearWords;
    pm::Stream tooSmall; r5::SysmemPass refused(tooSmall, beginWords - 1);
    VERIFY(!refused.begin(t, scratch())); VERIFY(tooSmall.size() == 0 && !refused.active());
    pm::Stream exact; r5::SysmemPass pass(exact, beginWords + clearWords + endWords);
    VERIFY(pass.begin(t, scratch())); VERIFY(pass.clearColor({1,2,3,4})); VERIFY(pass.end());
    VERIFY(exact.size() == original.size());
    pm::Stream tailShort; r5::SysmemPass shortPass(tailShort, beginWords + endWords - 1);
    VERIFY(shortPass.begin(t, scratch()));
    const auto before = tailShort.words();
    VERIFY(!shortPass.end()); VERIFY(shortPass.active() && tailShort.words() == before);
    mesa::Harness h(t.address, scratch().address, t.width, t.height, t.pitchBytes, uint32_t(t.layerBytes), false);
    StructuralFake fake(beginOracle(h, t));
    VERIFY(fake.accept(original.words(), t, scratch()));
    // Every packet header's parity bit, and every possible truncation.
    for (size_t i = 0; i < original.size();) {
        const uint32_t word = original.words()[i], kind = word >> 28;
        auto corrupt = original.words(); corrupt[i] ^= kind == 4 ? 1u << 7 : 1u << 15;
        VERIFY(!fake.accept(corrupt, t, scratch()));
        corrupt = original.words();
        corrupt[i] = t7(0x75, 0); // forbidden A6xx memcpy, not a render opcode
        VERIFY(!fake.accept(corrupt, t, scratch()));
        i += (word & (kind == 4 ? 0x7f : 0x3fff)) + 1;
    }
    for (size_t count = 0; count < original.size() - 3; ++count)
        VERIFY(!fake.accept(std::vector<uint32_t>(original.words().begin(),
                           original.words().begin() + count), t, scratch()));
    auto corrupt = original.words();
    const auto info = std::find(corrupt.begin(), corrupt.end(), mesa::pm4_pkt4_hdr(REG_A5XX_RB_MRT_BUF_INFO(0), 5));
    VERIFY(info != corrupt.end());
    if (info != corrupt.end()) *(info + 1) = 0x31;
    VERIFY(!fake.accept(corrupt, t, scratch()));
    VERIFY(!fake.accept(original.words(), t, {scratch().address + 4, 4}));
}

#ifdef HAKIOS_A5XX_RENDER_TEST_STANDALONE
int main(int argc, char** argv) {
    for (const auto& item : test::cases()) {
        const int before = test::failures();
        item.run();
        std::printf("%s %s\n", before == test::failures() ? "ok" : "FAIL", item.name);
    }
    if (argc > 1) {
        const auto t = target(320, 240, true);
        pm::Stream stream; r5::SysmemPass pass(stream);
        if (!pass.begin(t, scratch()) || !pass.clearColor({0x12,0x34,0x56,0x78}) || !pass.end()) return 2;
        std::ofstream dump(argv[1]);
        for (size_t i = 0; i < stream.size(); ++i) {
            char line[64]; std::snprintf(line, sizeof(line), "%04zu %08x\n", i, stream.words()[i]);
            dump << line;
        }
    }
    std::printf("A5xx RENDER HOST STRUCTURE ONLY: %zu tests, %llu checks, %d failures; hardware_tested=false; pixels_executed=0\n",
        test::cases().size(), static_cast<unsigned long long>(checks), test::failures());
    return test::failures() ? EXIT_FAILURE : EXIT_SUCCESS;
}
#endif
#endif
