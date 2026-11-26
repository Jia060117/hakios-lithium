/* Read-only context for the pinned HakiOS ustacks walker; no send rights consumed. */
#include <sys/ulock.h>
#include <sys/syscall.h>
#include <ipc/ipc_port.h>
#include <kern/ipc_tt.h>

static char lithium_ustacks_related[128];
static uint32_t lithium_ustacks_top;
static unsigned lithium_ustacks_sequence;

static bool
lithium_ustacks_match(const char *name)
{
	const char *at = lithium_ustacks_related;
	size_t length = strlen(name);

	while (*at != 0) {
		const char *end = strchr(at, ',');
		size_t token = end != NULL ? (size_t)(end - at) : strlen(at);

		if (token != 0 && token == length && strncmp(at, name, token) == 0) {
			return true;
		}
		if (end == NULL) {
			break;
		}
		at = end + 1;
	}
	return false;
}

static bool
lithium_ustacks_bounds(uint32_t threads, uint32_t count, uint32_t period)
{
	return threads >= 1 && threads <= 96 && count >= 1 && count <= 16 &&
	       period >= 180000 && period <= 600000;
}

struct lithium_wait_context {
	bool recognized, same_task, word_matches;
	uint32_t opcode, owner_port;
	uint64_t word, owner_tid;
	int copy_error;
	kern_return_t lookup_error;
};

/* The referenced task owns its IPC space until task destruction. The walker
 * also holds its VM map while copyin runs. A sampled lock may have changed:
 * report the comparison rather than claiming the historical owner was proven.
 */
static struct lithium_wait_context
lithium_ustacks_wait(task_t task, uint64_t operation, uint64_t address,
    uint64_t value, uint64_t syscall, int state)
{
	struct lithium_wait_context result = { 0 };
	ipc_port_t port = IP_NULL;
	thread_read_t owner;
	size_t size;

	result.lookup_error = KERN_INVALID_NAME;
	if ((state & TH_WAIT) == 0 || (syscall != SYS_ulock_wait && syscall != SYS_ulock_wait2)) {
		return result;
	}
	result.opcode = operation & UL_OPCODE_MASK;
	switch (result.opcode) {
	case UL_COMPARE_AND_WAIT:
	case UL_UNFAIR_LOCK:
	case UL_COMPARE_AND_WAIT_SHARED:
		size = sizeof(uint32_t);
		break;
	case UL_UNFAIR_LOCK64_SHARED:
	case UL_COMPARE_AND_WAIT64:
	case UL_COMPARE_AND_WAIT64_SHARED:
		size = sizeof(uint64_t);
		break;
	default:
		return result;
	}
	if (!plausible_user_address(address) || address > MACH_VM_MAX_ADDRESS - size || (address & (size - 1)) != 0) {
		return result;
	}
	result.recognized = true;
	result.copy_error = copyin((user_addr_t)address, &result.word, size);
	if (result.copy_error != 0) {
		return result;
	}
	result.word_matches = result.word == (size == sizeof(uint32_t) ? (uint32_t)value : value);
	/* Compare-and-wait and shared 64-bit locks do not use this owner-port format. */
	if (result.opcode != UL_UNFAIR_LOCK || result.word == 0) {
		return result;
	}
	result.owner_port = ulock_owner_value_to_port_name((uint32_t)result.word);
	result.lookup_error = ipc_port_translate_send(task->itk_space, result.owner_port, &port);
	if (result.lookup_error != KERN_SUCCESS) {
		return result;
	}
	/* translate_send returns the port locked without a reference. Take one,
 * unlock before the normal thread-read conversion, then release both refs.
 */
	ip_reference(port);
	ip_mq_unlock(port);
	owner = convert_port_to_thread_read(port);
	ip_release(port);
	if (owner != THREAD_NULL) {
		result.owner_tid = thread_tid(owner);
		result.same_task = get_threadtask(owner) == task;
		thread_deallocate(owner);
	} else {
		result.lookup_error = KERN_INVALID_RIGHT;
	}
	return result;
}
