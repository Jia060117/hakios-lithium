/* Bounded last outbound send attempt per selected thread. */
#include <arm64/proc_reg.h>
#include <machine/atomic.h>

#define LITHIUM_IPC_SLOTS 128
struct lithium_ipc_record {
	uint64_t tid, ticks, sequence;
	int32_t sender, receiver, msgid;
	uint32_t bits;
};
struct lithium_ipc_slot {
	uint32_t busy;
	struct lithium_ipc_record record;
};
static struct lithium_ipc_slot lithium_ipc_slots[LITHIUM_IPC_SLOTS];
static uint64_t lithium_ipc_sequence;
static uint32_t lithium_ipc_dropped;
static uint64_t lithium_ipc_dumped[LITHIUM_IPC_SLOTS];

static bool
lithium_ipc_selected(const char *name)
{
	return name != NULL && (strcmp(name, "lsd") == 0 ||
	    strcmp(name, "cfprefsd") == 0 ||
	    strcmp(name, "containermanagerd") == 0 ||
	    strcmp(name, "containermanagerd_system") == 0 ||
	    strcmp(name, "SpringBoard") == 0 ||
	    strcmp(name, "backboardd") == 0 ||
	    strcmp(name, "runningboardd") == 0 ||
	    strcmp(name, "installcoordinationd") == 0);
}

void lithium_ipc_send_locked(ipc_port_t, mach_msg_id_t, mach_msg_bits_t);
void
lithium_ipc_send_locked(ipc_port_t port, mach_msg_id_t msgid, mach_msg_bits_t bits)
{
	struct proc *proc = get_bsdtask_info(current_task());
	const char *name = proc ? proc_best_name(proc) : NULL;
	if (!lithium_ipc_selected(name)) return;

	uint64_t tid = thread_tid(current_thread());
	struct lithium_ipc_slot *slot = &lithium_ipc_slots[tid & (LITHIUM_IPC_SLOTS - 1)];
	if (!os_atomic_cmpxchg(&slot->busy, 0, 1, acquire)) {
		os_atomic_inc(&lithium_ipc_dropped, relaxed);
		return;
	}
	struct lithium_ipc_record *record = &slot->record;
	record->tid = tid;
	record->ticks = mach_absolute_time();
	record->sequence = __atomic_add_fetch(&lithium_ipc_sequence, 1, __ATOMIC_RELAXED);
	record->sender = proc_pid(proc);
	record->receiver = ipc_port_get_receiver_task_locked(port, NULL);
	record->msgid = msgid;
	record->bits = bits;
	os_atomic_store(&slot->busy, 0, release);
}

static void
lithium_ipc_history_dump(void)
{
	unsigned emitted = 0;
	for (unsigned i = 0; i < LITHIUM_IPC_SLOTS; i++) {
		struct lithium_ipc_record record = {};
		struct lithium_ipc_slot *slot = &lithium_ipc_slots[i];
		if (!os_atomic_cmpxchg(&slot->busy, 0, 1, acquire)) continue;
		record = slot->record;
		os_atomic_store(&slot->busy, 0, release);
		if (record.sequence == 0 || record.sequence == lithium_ipc_dumped[i]) continue;
		lithium_ipc_dumped[i] = record.sequence;
		emitted++;
		printf("LITHIUM IPC attempt sender=%d tid=0x%llx receiver=%d msgid=%d bits=0x%x ticks=%llu seq=%llu attempt_only=1\n",
		    record.sender, record.tid, record.receiver, record.msgid, record.bits,
		    record.ticks, record.sequence);
	}
	printf("LITHIUM IPC attempts emitted=%u slots=%u dropped=%u scope=latest-per-hashed-slot\n",
	    emitted, LITHIUM_IPC_SLOTS, os_atomic_load(&lithium_ipc_dropped, relaxed));
}
