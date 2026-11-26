#ifndef LITHIUM_PERIPHERAL_BOOT_OPTIONS_H
#define LITHIUM_PERIPHERAL_BOOT_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

struct lithium_peripheral_options {
    bool touch;
    bool touch_log;
    bool touch_poll;
    bool touch_stage_requested;
    unsigned touch_stage;
    bool touch_clock_init;
    bool touch_id_probe;
    bool touch_snapshot;
    bool touch_qup_init;
    bool touch_qup_state;
    bool touch_qup_phase;
    bool touch_qup_transfer;
    bool gpu_probe;
    unsigned gpu_stage;
    bool gpu_wd;
};

struct lithium_peripheral_key {
    const char *name;
    size_t length;
    unsigned max_value;
};

enum lithium_peripheral_option_id {
    LITHIUM_TOUCH,
    LITHIUM_TOUCH_LOG,
    LITHIUM_TOUCH_POLL,
    LITHIUM_TOUCH_STAGE,
    LITHIUM_TOUCH_CLOCK_INIT,
    LITHIUM_TOUCH_ID_PROBE,
    LITHIUM_TOUCH_SNAPSHOT,
    LITHIUM_TOUCH_QUP_INIT,
    LITHIUM_TOUCH_QUP_STATE,
    LITHIUM_TOUCH_QUP_PHASE,
    LITHIUM_TOUCH_QUP_TRANSFER,
    LITHIUM_GPU_PROBE,
    LITHIUM_GPU_STAGE,
    LITHIUM_GPU_WD,
    LITHIUM_OPTION_COUNT
};

static inline bool
lithium_peripheral_space(char byte)
{
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

/* This first transaction is restricted to the RAM diagnostic package. This
 * checks the command line, not physical firmware ownership. The isolated
 * loader omits the storage handoff when its bridge is disabled. */
static inline bool
lithium_touch_id_ram_args(const char *args, size_t length)
{
    unsigned ufs = 0, diag = 0, timer = 0, roots = 0;
    size_t pos = 0;
    /* Match PID1's exact printable/space-separated snapshot contract. */
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)args[i] < 0x20 || (unsigned char)args[i] > 0x7e) return false;
    while (pos < length) {
        while (pos < length && lithium_peripheral_space(args[pos])) ++pos;
        const size_t start = pos;
        while (pos < length && !lithium_peripheral_space(args[pos])) ++pos;
        const size_t size = pos - start;
        size_t key = 0;
        while (key < size && args[start + key] != '=') ++key;
#define LTI_KEY(text) (key == sizeof(text) - 1 && \
    __builtin_memcmp(args + start, (text), sizeof(text) - 1) == 0)
#define LTI_WORD(text) (size == sizeof(text) - 1 && \
    __builtin_memcmp(args + start, (text), sizeof(text) - 1) == 0)
        if (LTI_KEY("ufs")) { if (!LTI_WORD("ufs=0") || ++ufs != 1) return false; }
        else if (LTI_KEY("touch_diag")) { if (!LTI_WORD("touch_diag=1") || ++diag != 1) return false; }
        else if (LTI_KEY("loader_return_after")) {
            if (!LTI_WORD("loader_return_after=180") || ++timer != 1) return false;
        } else if (LTI_KEY("rd")) {
            if (!LTI_WORD("rd=md0") || ++roots != 1) return false;
        } else if ((key >= 4 && __builtin_memcmp(args + start, "ufs_", 4) == 0) ||
                   (key >= 4 && __builtin_memcmp(args + start, "var_", 4) == 0) ||
                   LTI_KEY("root_apfs") || LTI_KEY("rootdev") || LTI_KEY("root_device") ||
                   LTI_KEY("apfs_volume") || LTI_KEY("hfs_bench") || LTI_KEY("fsdiag") ||
                   LTI_KEY("hfsdiag") || LTI_KEY("lsdbfinal")) return false;
#undef LTI_WORD
#undef LTI_KEY
    }
    return ufs == 1 && diag == 1 && timer == 1;
}

/* Pass the payload command line without its NUL terminator. On invalid input,
 * leave every feature disabled. Ignore unrelated options for the base system. */
static inline bool
lithium_peripheral_options_parse(const char *args, size_t length,
    struct lithium_peripheral_options *options)
{
    static const struct lithium_peripheral_key keys[LITHIUM_OPTION_COUNT] = {
        [LITHIUM_TOUCH] = {"touch", sizeof("touch") - 1, 1},
        [LITHIUM_TOUCH_LOG] = {"touch_log", sizeof("touch_log") - 1, 1},
        [LITHIUM_TOUCH_POLL] = {"touch_poll", sizeof("touch_poll") - 1, 1},
        [LITHIUM_TOUCH_STAGE] = {"touch_stage", sizeof("touch_stage") - 1, 3},
        [LITHIUM_TOUCH_CLOCK_INIT] = {"touch_clock_init", sizeof("touch_clock_init") - 1, 1},
        [LITHIUM_TOUCH_ID_PROBE] = {"touch_id_probe", sizeof("touch_id_probe") - 1, 1},
        [LITHIUM_TOUCH_SNAPSHOT] = {"touch_snapshot", sizeof("touch_snapshot") - 1, 1},
        [LITHIUM_TOUCH_QUP_INIT] = {"touch_qup_init", sizeof("touch_qup_init") - 1, 1},
        [LITHIUM_TOUCH_QUP_STATE] = {"touch_qup_state", sizeof("touch_qup_state") - 1, 1},
        [LITHIUM_TOUCH_QUP_PHASE] = {"touch_qup_phase", sizeof("touch_qup_phase") - 1, 1},
        [LITHIUM_TOUCH_QUP_TRANSFER] = {"touch_qup_transfer", sizeof("touch_qup_transfer") - 1, 1},
        [LITHIUM_GPU_PROBE] = {"gpu_probe", sizeof("gpu_probe") - 1, 1},
        [LITHIUM_GPU_STAGE] = {"gpu_stage", sizeof("gpu_stage") - 1, 4},
        [LITHIUM_GPU_WD] = {"gpu_wd", sizeof("gpu_wd") - 1, 1},
    };
    unsigned values[LITHIUM_OPTION_COUNT] = {0};
    unsigned seen = 0;
    size_t pos = 0;

    *options = (struct lithium_peripheral_options){0};
    if (args == NULL) {
        return length == 0;
    }
    while (pos < length) {
        if (lithium_peripheral_space(args[pos])) {
            ++pos;
            continue;
        }
        const size_t start = pos;
        while (pos < length && !lithium_peripheral_space(args[pos])) {
            if (args[pos] == '\0') {
                return false;
            }
            ++pos;
        }
        const size_t size = pos - start;
        for (unsigned key = 0; key < LITHIUM_OPTION_COUNT; ++key) {
            const size_t name_length = keys[key].length;
            if (size < name_length ||
                (size > name_length && args[start + name_length] != '=')) {
                continue;
            }
            size_t same = 0;
            while (same < name_length && args[start + same] == keys[key].name[same]) {
                ++same;
            }
            if (same != name_length) {
                continue;
            }
            if (size != name_length + 2 || (seen & (1u << key)) != 0) {
                return false;
            }
            const char digit = args[start + name_length + 1];
            if (digit < '0' || digit > '9') {
                return false;
            }
            const unsigned value = (unsigned)(digit - '0');
            if (value > keys[key].max_value) {
                return false;
            }
            values[key] = value;
            seen |= 1u << key;
            break;
        }
    }
    const unsigned legacy_touch = (1u << LITHIUM_TOUCH) |
        (1u << LITHIUM_TOUCH_LOG) | (1u << LITHIUM_TOUCH_POLL);
    if ((seen & (1u << LITHIUM_TOUCH_STAGE)) != 0 && (seen & legacy_touch) != 0) {
        return false;
    }
    /* This separate opt-in recognizes only the two vendor-equivalent XO /1
     * HID encodings before voting the existing branches on. It never changes
     * RCG source/rate/reset state or enables the unfinished higher stages. */
    if ((seen & (1u << LITHIUM_TOUCH_CLOCK_INIT)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1)) {
        return false;
    }
    /* A separate experimental one-shot, never an implicit extension of
     * Stage1. Even an explicit zero must be attached to the right stage;
     * malformed or duplicate requests cannot enable peripheral access. */
    if ((seen & (1u << LITHIUM_TOUCH_ID_PROBE)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) {
        return false;
    }
    if (values[LITHIUM_TOUCH_ID_PROBE] != 0 &&
        !lithium_touch_id_ram_args(args, length)) return false;
    /* R28 observes the non-consuming QUP entry registers; it cannot fall
     * through to an ID transaction. Reuse the exact RAM session boundary. */
    if ((seen & (1u << LITHIUM_TOUCH_SNAPSHOT)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) return false;
    if (values[LITHIUM_TOUCH_SNAPSHOT] != 0 &&
        (values[LITHIUM_TOUCH_ID_PROBE] != 0 ||
         !lithium_touch_id_ram_args(args, length))) return false;
    /* R29 is a separate, destructive controller-initialization experiment.
     * It must never be implied by either old observation/transaction option.
     * Explicit zero still requires the appropriate Stage1 prerequisite. */
    if ((seen & (1u << LITHIUM_TOUCH_QUP_INIT)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) return false;
    if (values[LITHIUM_TOUCH_QUP_INIT] != 0 &&
        (values[LITHIUM_TOUCH_ID_PROBE] != 0 ||
         values[LITHIUM_TOUCH_SNAPSHOT] != 0 ||
         !lithium_touch_id_ram_args(args, length))) return false;
    /* R30 stops before new FIFO tags/ID. It cannot imply any old probe, nor
     * promise that first RUN is electrically silent under unknown ownership. */
    if ((seen & (1u << LITHIUM_TOUCH_QUP_STATE)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) return false;
    if (values[LITHIUM_TOUCH_QUP_STATE] != 0 &&
        (values[LITHIUM_TOUCH_QUP_INIT] != 0 ||
         values[LITHIUM_TOUCH_ID_PROBE] != 0 ||
         values[LITHIUM_TOUCH_SNAPSHOT] != 0 ||
         !lithium_touch_id_ram_args(args, length))) return false;
    /* R31 separately preserves the exact transition-poll evidence. Its phase
     * policy is source-reviewed independently; old R30 remains selectable. */
    if ((seen & (1u << LITHIUM_TOUCH_QUP_PHASE)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) return false;
    if (values[LITHIUM_TOUCH_QUP_PHASE] != 0 &&
        (values[LITHIUM_TOUCH_QUP_STATE] != 0 ||
         values[LITHIUM_TOUCH_QUP_INIT] != 0 ||
         values[LITHIUM_TOUCH_ID_PROBE] != 0 ||
         values[LITHIUM_TOUCH_SNAPSHOT] != 0 ||
         !lithium_touch_id_ram_args(args, length))) return false;
    /* R32 owns one initialized transfer. It never composes two active probes.
     * Even an explicit zero requires Stage1; enabling requires isolated RAM. */
    if ((seen & (1u << LITHIUM_TOUCH_QUP_TRANSFER)) != 0 &&
        ((seen & (1u << LITHIUM_TOUCH_STAGE)) == 0 ||
         values[LITHIUM_TOUCH_STAGE] != 1 ||
         values[LITHIUM_TOUCH_CLOCK_INIT] != 1)) return false;
    if (values[LITHIUM_TOUCH_QUP_TRANSFER] != 0 &&
        (values[LITHIUM_TOUCH_QUP_PHASE] != 0 ||
         values[LITHIUM_TOUCH_QUP_STATE] != 0 ||
         values[LITHIUM_TOUCH_QUP_INIT] != 0 ||
         values[LITHIUM_TOUCH_ID_PROBE] != 0 ||
         values[LITHIUM_TOUCH_SNAPSHOT] != 0 ||
         !lithium_touch_id_ram_args(args, length))) return false;
    options->touch = values[LITHIUM_TOUCH] != 0;
    options->touch_log = options->touch && values[LITHIUM_TOUCH_LOG] != 0;
    options->touch_poll = options->touch && values[LITHIUM_TOUCH_POLL] != 0;
    options->touch_stage_requested = (seen & (1u << LITHIUM_TOUCH_STAGE)) != 0;
    options->touch_stage = values[LITHIUM_TOUCH_STAGE];
    options->touch_clock_init = values[LITHIUM_TOUCH_CLOCK_INIT] != 0;
    options->touch_id_probe = values[LITHIUM_TOUCH_ID_PROBE] != 0;
    options->touch_snapshot = values[LITHIUM_TOUCH_SNAPSHOT] != 0;
    options->touch_qup_init = values[LITHIUM_TOUCH_QUP_INIT] != 0;
    options->touch_qup_state = values[LITHIUM_TOUCH_QUP_STATE] != 0;
    options->touch_qup_phase = values[LITHIUM_TOUCH_QUP_PHASE] != 0;
    options->touch_qup_transfer = values[LITHIUM_TOUCH_QUP_TRANSFER] != 0;
    options->gpu_stage = values[LITHIUM_GPU_STAGE];
    options->gpu_probe = values[LITHIUM_GPU_PROBE] != 0 || options->gpu_stage != 0;
    options->gpu_wd = options->gpu_probe && values[LITHIUM_GPU_WD] != 0;
    return true;
}

#endif
