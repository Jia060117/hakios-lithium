/* Actual-header host tests. Independent address/opcode literals are sourced
 * from stock, pinned regulator/arbiter sources; no hardware I/O occurs here.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/gpu/g1-spmi-read.h"
#include "../../ports/hakios-lithium/diagnostics/cpu-spmi-read-r38.h"

struct event { char op; uint64_t pa; uint32_t value; };
struct mock {
    uint32_t version, map[128], status[100], status_count, status_index;
    uint32_t config, raw, reads, writes, times, delays, events;
    uint32_t fail_at, mutate_read, mutate_value, frozen, backwards_at;
    uint32_t probe_only;
    uint64_t observer, clock, jump;
    struct event log[1024];
};
static unsigned cases, checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
_Static_assert(sizeof(struct lgs_result) <= 256, "single bounded transaction result");

static void init(struct mock *m, uint32_t ppid, unsigned apid)
{
    memset(m, 0, sizeof(*m));
    m->version = 0x20010000;
    m->map[apid] = ppid << 8;
    m->observer = UINT64_C(0x04c00000) + UINT64_C(0x8000) * apid;
    m->clock = 1000;
    m->status[0] = 1;
    m->status_count = 1;
    m->raw = 0x67452301;
}
static int event(struct mock *m, char op, uint64_t pa, uint32_t v)
{
    CHECK(m->events < 1024);
    m->log[m->events++] = (struct event){op, pa, v};
    return m->events == m->fail_at ? -73 : 0;
}
static int read32(void *ctx, uint64_t pa, uint32_t *v)
{
    struct mock *m = ctx;
    ++m->reads;
    *v = 0xdeadbeef; /* a failed callback must not publish this value */
    if (event(m, 'R', pa, 0)) return -73;
    if (pa == UINT64_C(0x0400f000)) *v = m->version;
    else if (pa >= UINT64_C(0x0400f800) && pa <= UINT64_C(0x0400f9fc) && !(pa & 3u))
        *v = m->map[(pa - UINT64_C(0x0400f800)) / 4u];
    else {
        CHECK(!m->probe_only);
        CHECK(pa == m->observer + 4 || pa == m->observer + 8 ||
              pa == m->observer + 24 || pa == m->observer + 28);
        if (pa == m->observer + 4) *v = m->config;
        else if (pa == m->observer + 8) {
            CHECK(m->writes == 1);
            uint32_t i = m->status_index++;
            *v = m->status[i < m->status_count ? i : m->status_count - 1];
        } else {
            CHECK(m->writes == 1 && m->status_index > 0);
            *v = m->raw;
        }
    }
    if (m->mutate_read == m->reads) *v = m->mutate_value;
    m->log[m->events - 1].value = *v;
    return 0;
}
static int write32(void *ctx, uint64_t pa, uint32_t v)
{
    struct mock *m = ctx;
    ++m->writes;
    CHECK(!m->probe_only && m->writes == 1);
    CHECK(pa == m->observer); /* ONLY CMD, never WDATA/CONFIG/IRQ/reset */
    CHECK((v >> 27) == 1);  /* EXT_READL */
    CHECK((v & 0x07fff008u) == 0); /* no v1 SID/full address/non-data bits */
    m->status_index = 0;
    return event(m, 'W', pa, v);
}
static int time_us(void *ctx, uint64_t *v)
{
    struct mock *m = ctx;
    ++m->times;
    if (event(m, 'T', 0, 0)) return -73;
    *v = m->times == m->backwards_at ? 1 :
         m->clock + (m->times > 1 ? m->jump : 0);
    return 0;
}
static int delay_us(void *ctx, uint32_t us)
{
    struct mock *m = ctx;
    ++m->delays;
    CHECK(us == 1);
    if (event(m, 'D', 0, us)) return -73;
    if (!m->frozen) m->clock += us;
    return 0;
}
static struct lgs_io io_for(struct mock *m)
{
    return (struct lgs_io){m, read32, write32, time_us, delay_us};
}
static struct lgs_route probe(struct mock *m, uint32_t sid)
{
    struct lgs_io io = io_for(m);
    struct lgs_result r;
    m->probe_only = 1;
    io.write32 = NULL; io.time_us = NULL; io.delay_us = NULL;
    CHECK(lgs_probe(&io, sid, 0x1700, &r) == LGS_OK);
    CHECK(r.reads == 129 && r.mapping_reads == 128 && r.matches == 1);
    CHECK(!r.writes && !m->writes && !m->times && !m->delays);
    CHECK(r.route.sid == sid && r.route.base == 0x1700);
    CHECK(r.route.ppid == (sid == 3 ? 0x317u : 0x117u));
    CHECK(r.route.observer_pa == m->observer && r.route.observer_bytes == 4096);
    m->probe_only = 0; m->reads = 0; m->events = 0;
    return r.route;
}
static enum lgs_error run(struct mock *m, const struct lgs_route *route,
                          uint32_t sid, uint32_t addr, uint32_t len,
                          struct lgs_result *r)
{
    struct lgs_io io = io_for(m);
    return lgs_read(&io, route, sid, addr, len, r);
}

static void discovery(void)
{
    struct mock m; struct lgs_result r;
    for (unsigned rail = 0; rail < 2; ++rail) {
        uint32_t sid = rail ? 1 : 3, ppid = rail ? 0x117 : 0x317;
        for (unsigned apid = 0; apid < 128; ++apid) {
            init(&m, ppid, apid);
            struct lgs_route route = probe(&m, sid);
            CHECK(route.apid == apid && route.mapping_raw == (ppid << 8));
            ++cases;
        }
    }
    const uint32_t bad_versions[] = {0, 0xffffffff, 0x20000000, 0x20010001, 0x30000000};
    for (unsigned i = 0; i < sizeof(bad_versions)/sizeof(*bad_versions); ++i) {
        init(&m, 0x317, 17); m.version = bad_versions[i];
        struct lgs_io io = io_for(&m);
        CHECK(lgs_probe(&io, 3, 0x1700, &r) == LGS_VERSION);
        CHECK(m.reads == 1 && !m.writes && r.route.version == bad_versions[i]);
        ++cases;
    }
    init(&m, 0x317, 17); m.map[17] = 0;
    struct lgs_io io = io_for(&m);
    CHECK(lgs_probe(&io, 3, 0x1700, &r) == LGS_NO_MAPPING);
    CHECK(r.mapping_reads == 128 && !r.route.observer_bytes && !m.writes);
    ++cases;
    init(&m, 0x317, 17); m.map[127] = 0x317ff;
    CHECK(lgs_probe(&io, 3, 0x1700, &r) == LGS_DUPLICATE_MAPPING);
    CHECK(r.matches == 2 && !r.route.observer_bytes && !m.writes);
    ++cases;
    init(&m, 0x317, 17); m.map[17] = 0x00011700;
    CHECK(lgs_probe(&io, 3, 0x1700, &r) == LGS_NO_MAPPING);
    ++cases;
    init(&m, 0x317, 127); m.map[127] = 0xf00317a5;
    CHECK(lgs_probe(&io, 3, 0x1700, &r) == LGS_OK);
    CHECK(r.route.mapping_raw == 0xf00317a5 && r.route.apid == 127);
    ++cases;
    puts("PASS discovery both_rails all_128_channels strict_version mapping_bits missing duplicate");
}

static int source_allowed(uint32_t sid, uint32_t addr, uint32_t len)
{
    /* Independent source literals, deliberately not implementation macros. */
    static const unsigned offsets[] = {4, 5, 0x40, 0x41, 0x45, 0x46, 0x61};
    if ((sid != 1 && sid != 3) || addr < 0x1700 || addr > 0x17ff ||
        !len || len > 8 || len > 0x1800 - addr) return 0;
    for (unsigned i = 0; i < len; ++i) {
        unsigned yes = 0;
        for (unsigned j = 0; j < sizeof(offsets)/sizeof(*offsets); ++j)
            if (addr + i == 0x1700 + offsets[j]) yes = 1;
        if (!yes) return 0;
    }
    return 1;
}
static void field_allowlist(void)
{
    static const unsigned offsets[] = {4, 5, 0x40, 0x41, 0x45, 0x46, 0x61};
    static const char *names[] = {"TYPE", "SUBTYPE", "RANGE", "VSET", "MODE", "ENABLE", "STEP"};
    CHECK(lgs_field_count == 7 && lgs_rail_count == 2);
    for (unsigned i = 0; i < 7; ++i) {
        CHECK(lgs_fields[i].offset == offsets[i]);
        CHECK(strcmp(lgs_fields[i].name, names[i]) == 0);
    }
    for (unsigned rail = 0; rail < 2; ++rail) {
        uint32_t sid = rail ? 1 : 3, ppid = rail ? 0x117 : 0x317;
        for (unsigned i = 0; i < 7; ++i) {
            struct mock m; init(&m, ppid, 17);
            struct lgs_route route = probe(&m, sid);
            struct lgs_result r;
            CHECK(run(&m, &route, sid, 0x1700 + offsets[i], 1, &r) == LGS_OK);
            CHECK(r.cmd == (0x08000000u | (offsets[i] << 4)));
            CHECK(m.writes == 1 && r.cmd_attempted && r.cmd_completed && r.data_valid);
            CHECK(r.data_words_valid == 1 && r.data[0] == 1 && r.data[1] == 0);
            CHECK(r.reads == 134 && r.polls == 1 && !r.callback_rc);
            ++cases;
        }
        for (unsigned i = 0; i < 3; ++i) {
            static const unsigned starts[] = {4, 0x40, 0x45};
            struct mock m; init(&m, ppid, 17);
            struct lgs_route route = probe(&m, sid);
            struct lgs_result r;
            CHECK(run(&m, &route, sid, 0x1700 + starts[i], 2, &r) == LGS_OK);
            CHECK(r.cmd == (0x08000001u | (starts[i] << 4)));
            CHECK(r.data[0] == 1 && r.data[1] == 0x23 && r.data[2] == 0);
            ++cases;
        }
    }
    /* Invalid fields/spans/SIDs: no scan, command, time or delay callback. */
    struct mock m; init(&m, 0x317, 17);
    struct lgs_route route = {0}; struct lgs_result r;
    for (unsigned sid = 0; sid < 16; ++sid)
        for (unsigned offset = 0; offset < 256; ++offset)
            for (unsigned len = 0; len <= 9; ++len) {
                unsigned addr = 0x1700 + offset;
                int allowed = source_allowed(sid, addr, len);
                CHECK(lgs_request_valid(sid, addr, len) == allowed);
                if (!allowed) {
                    CHECK(run(&m, &route, sid, addr, len, &r) == LGS_ARGUMENT);
                    CHECK(!m.events && !r.cmd_attempted && !r.data_valid);
                    ++cases;
                }
            }
    const unsigned bad_addr[] = {0x1404,0x1440,0x3204,0x1742,0x1744,0x1747,0x1708,0x1760,0x1762,0xffffffff};
    for (unsigned i = 0; i < sizeof(bad_addr)/sizeof(*bad_addr); ++i) {
        CHECK(run(&m, &route, 1, bad_addr[i], 1, &r) == LGS_ARGUMENT);
        CHECK(!m.events);
        ++cases;
    }
    struct lgs_io io = io_for(&m);
    CHECK(lgs_probe(&io, 1, 0x1400, &r) == LGS_ARGUMENT && !m.events);
    CHECK(lgs_probe(&io, 3, 0x1701, &r) == LGS_ARGUMENT && !m.events);
    cases += 2;
    puts("PASS exact_source_fields both_rails contiguous_pairs all_sid_byte_length_rejections zero_io");
}

static void routes_and_config(void)
{
    for (unsigned slot = 0; slot < 8; ++slot) {
        struct mock m; init(&m, 0x317, 17);
        struct lgs_route route = probe(&m, 3); struct lgs_result r;
        switch (slot) {
        case 0: ++route.sid; break;
        case 1: route.base = 0x1400; break;
        case 2: route.ppid = 0x117; break;
        case 3: ++route.version; break;
        case 4: ++route.apid; break;
        case 5: ++route.mapping_raw; break;
        case 6: route.observer_pa += 4; break;
        case 7: route.observer_bytes = 16384; break;
        }
        CHECK(run(&m, &route, 3, 0x1740, 1, &r) == LGS_ROUTE_CHANGED);
        CHECK(m.reads == 129 && !m.writes && !r.config_valid);
        ++cases;
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        struct mock m; init(&m, 0x317, 17);
        struct lgs_route route = probe(&m, 3); struct lgs_result r;
        m.mutate_read = slot ? 131 : 130;
        m.mutate_value = slot ? 0x31701 : 0x20010001;
        CHECK(run(&m, &route, 3, 0x1740, 1, &r) == LGS_ROUTE_CHANGED);
        CHECK(!m.writes && !r.config_valid && r.phase == LGS_RECHECK);
        ++cases;
    }
    struct mock m; init(&m, 0x317, 17);
    struct lgs_route route = probe(&m, 3); struct lgs_result r;
    m.config = 1;
    CHECK(run(&m, &route, 3, 0x1740, 1, &r) == LGS_CONFIG_UNSUPPORTED);
    CHECK(r.config_valid && r.config_raw == 1 && m.reads == 132 && !m.writes && !m.times);
    ++cases;
    init(&m, 0x317, 17); route = probe(&m, 3);
    r.route = route;
    CHECK(run(&m, &r.route, 3, 0x1740, 1, &r) == LGS_OK);
    CHECK(r.route.ppid == 0x317 && r.data_valid);
    ++cases;
    init(&m, 0x317, 17); route = probe(&m, 3);
    m.map[17] = 0x00011700;
    CHECK(run(&m, &route, 3, 0x1740, 1, &r) == LGS_NO_MAPPING && !m.writes);
    ++cases;
    puts("PASS route_all_components recheck_full_mapping version config_zero alias changed_scan");
}

static void completion(void)
{
    static const uint32_t status[] = {1,5,3,9,0x11,0x0f,0x80000001};
    static const enum lgs_error errors[] = {LGS_OK,LGS_DENIED,LGS_FAILURE,LGS_DROPPED,
                                           LGS_UNKNOWN_STATUS,LGS_DENIED,LGS_UNKNOWN_STATUS};
    for (unsigned i = 0; i < sizeof(status)/sizeof(*status); ++i) {
        struct mock m; init(&m, 0x317, 17);
        struct lgs_route route = probe(&m, 3); struct lgs_result r;
        m.status[0] = status[i]; m.jump = 101;
        CHECK(run(&m, &route, 3, 0x1740, 1, &r) == errors[i]);
        CHECK(r.cmd_completed && r.polls == 1 && r.status_raw == status[i]);
        CHECK(r.last_us - r.start_us == 101 && m.writes == 1);
        CHECK(r.data_valid == (errors[i] == LGS_OK));
        ++cases;
    }
    /* Differential against the unchanged actual R38 implementation. */
    static const uint64_t jumps[] = {0,99,100,101};
    for (unsigned i = 0; i < sizeof(jumps)/sizeof(*jumps); ++i)
        for (unsigned st = 0; st < 2; ++st) {
            struct mock gpu, cpu;
            init(&gpu,0x317,17); init(&cpu,0x132,17);
            struct lgs_route gr = probe(&gpu,3);
            struct r38_spmi_route cr = {0x20010000,17,0x13200,4096,0x04c88000};
            struct lgs_result g;
            struct r38_spmi_result c;
            gpu.jump = cpu.jump = jumps[i];
            gpu.status[0] = cpu.status[0] = st ? 5 : 1;
            struct r38_spmi_io ci = {&cpu,read32,write32,time_us,delay_us};
            enum lgs_error ge = run(&gpu,&gr,3,0x1740,1,&g);
            enum r38_spmi_error ce = r38_spmi_read(&ci,&cr,1,0x3240,1,&c);
            CHECK((int)ge == (int)ce && g.polls == c.polls && g.cmd == c.cmd);
            CHECK(g.cmd_completed == c.cmd_completed && g.data_valid == c.data_valid);
            CHECK(g.status_raw == c.status_raw && g.last_us == c.last_us);
            CHECK(gpu.writes == cpu.writes && gpu.delays == cpu.delays);
            ++cases;
        }
    struct mock m; init(&m,0x317,17);
    struct lgs_route route = probe(&m,3); struct lgs_result r;
    m.status[0] = 0; m.jump = 100;
    CHECK(run(&m,&route,3,0x1740,1,&r) == LGS_TIMEOUT);
    CHECK(r.polls == 1 && !r.cmd_completed && !r.data_valid && !m.delays);
    ++cases;
    init(&m,0x317,17); route=probe(&m,3);
    m.status[0]=2; m.frozen=1;
    CHECK(run(&m,&route,3,0x1740,1,&r) == LGS_TIMEOUT);
    CHECK(r.polls == 100 && m.delays == 99 && r.status_or == 2 && !r.cmd_completed);
    ++cases;
    init(&m,0x317,17); route=probe(&m,3);
    memset(m.status,0,sizeof(m.status)); m.status[99]=1; m.status_count=100;
    CHECK(run(&m,&route,3,0x1740,1,&r) == LGS_OK);
    CHECK(r.polls == 100 && m.delays == 99 && r.last_us-r.start_us == 99);
    ++cases;
    init(&m,0x317,17); route=probe(&m,3);
    m.backwards_at=2;
    CHECK(run(&m,&route,3,0x1740,1,&r) == LGS_TIME_ERROR);
    CHECK(!r.cmd_completed && !r.data_valid && r.status_valid);
    ++cases;
    init(&m,0x317,17); route=probe(&m,3);
    m.clock=UINT64_MAX-5;
    CHECK(run(&m,&route,3,0x1740,1,&r) == LGS_OK);
    CHECK(r.start_us == UINT64_MAX-5 && r.last_us == r.start_us);
    ++cases;
    puts("PASS late_done_errors R38_actual_header_differential timeout_100us frozen_100poll rollback overflow");
}

static void callback_failures(void)
{
    struct mock baseline; init(&baseline,0x317,17);
    struct lgs_route route=probe(&baseline,3); struct lgs_result r;
    baseline.status[0]=0; baseline.status[1]=1; baseline.status_count=2;
    CHECK(run(&baseline,&route,3,0x1740,1,&r) == LGS_OK);
    unsigned count=baseline.events;
    CHECK(count < 1024);
    for (unsigned at=1; at<=count; ++at) {
        struct mock m; init(&m,0x317,17);
        m.status[0]=0; m.status[1]=1; m.status_count=2; m.fail_at=at;
        char op=baseline.log[at-1].op;
        enum lgs_error want=op=='R'?LGS_READ_ERROR:op=='W'?LGS_WRITE_ERROR:
                            op=='T'?LGS_TIME_ERROR:LGS_DELAY_ERROR;
        CHECK(run(&m,&route,3,0x1740,1,&r) == want);
        CHECK(r.callback_rc == -73 && m.events == at && m.writes <= 1);
        CHECK(!r.data_valid && !r.data_words_valid && !r.data_raw[0]);
        if (op=='W') CHECK(r.cmd_attempted && !r.cmd_completed && r.writes == 1);
        ++cases;
    }
    struct mock m; init(&m,0x317,17); struct lgs_io io=io_for(&m);
    CHECK(lgs_probe(NULL,3,0x1700,&r) == LGS_ARGUMENT);
    CHECK(lgs_probe(&io,3,0x1700,NULL) == LGS_ARGUMENT);
    CHECK(lgs_read(NULL,&route,3,0x1740,1,&r) == LGS_ARGUMENT);
    CHECK(lgs_read(&io,NULL,3,0x1740,1,&r) == LGS_ARGUMENT);
    CHECK(lgs_read(&io,&route,3,0x1740,1,NULL) == LGS_ARGUMENT);
    for (unsigned i=0; i<4; ++i) {
        io=io_for(&m);
        if (i==0) io.read32=NULL;
        if (i==1) io.write32=NULL;
        if (i==2) io.time_us=NULL;
        if (i==3) io.delay_us=NULL;
        CHECK(lgs_read(&io,&route,3,0x1740,1,&r) == LGS_ARGUMENT);
    }
    CHECK(!m.events);
    cases += 9;
    puts("PASS callback_failure_every_actual_operation preserved_rc no_retry no_failed_raw null_callbacks");
}

int main(void)
{
    discovery(); field_allowlist(); routes_and_config(); completion(); callback_failures();
    printf("PASS total cases=%u checks=%u result_bytes=%zu hardware_verified=0\n",
           cases,checks,sizeof(struct lgs_result));
    return 0;
}
