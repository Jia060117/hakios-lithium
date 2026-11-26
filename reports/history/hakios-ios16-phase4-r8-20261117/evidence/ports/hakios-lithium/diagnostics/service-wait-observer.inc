/* Read-only current port receiver correlation for saved mach_msg2 arguments.
 * A send may already have completed, its right may have moved or its name may
 * have been reused. This is a sample, not proof of the historical RPC peer.
 * Old mach_msg buffers can contain the reply; do not guess their send header.
 */
#include <mach/message.h>

static void
lithium_ustacks_ipc(task_t task, uint64_t tid, int state, uint64_t options,
    uint64_t bits_size, uint64_t remote_local, uint64_t voucher_id,
    uint64_t descriptors_receive, uint64_t syscall)
{
	ipc_port_t port = IP_NULL;
	kern_return_t kr = KERN_INVALID_NAME;
	pid_t receiver = -1;
	uint32_t remote = (uint32_t)remote_local;

	if ((state & TH_WAIT) == 0 || (int64_t)syscall != -47) return;
	if ((options & MACH_SEND_MSG) != 0 && MACH_PORT_VALID(remote)) {
		kr = ipc_port_translate_send(task->itk_space, remote, &port);
		if (kr == KERN_SUCCESS) {
			receiver = ipc_port_get_receiver_task_locked(port, NULL);
			ip_mq_unlock(port); /* no new right/reference was acquired */
		}
	}
	printf("LITHIUM IPC tid=0x%llx trap=-47 options=0x%llx remote=0x%x receiver_pid=%d lookup=%d sampled_current=1\n",
	    tid, options, remote, receiver, kr);
	printf("LITHIUM IPC args tid=0x%llx bits=0x%x send_size=%u local=0x%x msgid=%d receive=0x%x\n",
	    tid, (uint32_t)bits_size, (uint32_t)(bits_size >> 32), (uint32_t)(remote_local >> 32),
	    (int32_t)(voucher_id >> 32), (uint32_t)(descriptors_receive >> 32));
}
