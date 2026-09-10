#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "sched.h"
#include "ipc.h"

extern task_t	g_tasks[];

int	ipc_init(void)
{
	return (0);
}

static task_t	*find_task(uint32_t pid)
{
	for (int i = 0; i < SCHED_MAX_TASKS; i++)
	{
		if (g_tasks[i].pid == pid &&
			g_tasks[i].state != S_EMPTY &&
			g_tasks[i].state != S_ZOMBIE)
			return (&g_tasks[i]);
	}
	return (0);
}

int	ipc_send(uint32_t dest, ipc_msg_t *msg)
{
	task_t	*self;
	task_t	*target;

	if (!msg)
		return (-1);
	self = find_task(sched_pid());
	if (!self)
		return (-1);
	target = find_task(dest);
	if (!target)
		return (-1);
	/* Block if target already has a pending message */
	if (target->ipc.state != IPC_IDLE)
	{
		self->state = S_BLOCKED_IPC;
		self->ipc.state = IPC_SENDING;
		self->ipc.partner = dest;
		memcpy(&self->ipc.msg, msg, sizeof(ipc_msg_t));
		sched_yield();
		/* Resumed: re-fetch our state after partner consumed it */
		return (0);
	}
	/* Deliver immediately */
	target->ipc.state = IPC_IDLE;
	target->ipc.partner = self->pid;
	memcpy(&target->ipc.msg, msg, sizeof(ipc_msg_t));
	target->ipc.msg.from = self->pid;
	/* Wake target if it was waiting */
	if (target->state == S_BLOCKED_IPC)
		target->state = S_RUNNABLE;
	return (0);
}

int	ipc_recv(uint32_t src, ipc_msg_t *msg)
{
	task_t	*self;

	if (!msg)
		return (-1);
	self = find_task(sched_pid());
	if (!self)
		return (-1);
	/* Check if someone already sent us a message */
	if (self->ipc.state == IPC_IDLE && self->ipc.msg.type != 0)
	{
		memcpy(msg, &self->ipc.msg, sizeof(ipc_msg_t));
		bzero(&self->ipc.msg, sizeof(ipc_msg_t));
		return (0);
	}
	/* No pending message: block and wait */
	self->state = S_BLOCKED_IPC;
	self->ipc.state = IPC_RECEIVING;
	self->ipc.partner = src;
	sched_yield();
	/* Resumed: copy the delivered message */
	memcpy(msg, &self->ipc.msg, sizeof(ipc_msg_t));
	bzero(&self->ipc.msg, sizeof(ipc_msg_t));
	self->ipc.state = IPC_IDLE;
	return (0);
}

int	ipc_reply(uint32_t dest, ipc_msg_t *msg)
{
	task_t	*target;

	if (!msg)
		return (-1);
	target = find_task(dest);
	if (!target)
		return (-1);
	/* Deliver reply directly */
	memcpy(&target->ipc.msg, msg, sizeof(ipc_msg_t));
	target->ipc.msg.from = sched_pid();
	/* Wake target if it was blocked */
	if (target->state == S_BLOCKED_IPC &&
		target->ipc.state == IPC_SENDING)
	{
		target->state = S_RUNNABLE;
		target->ipc.state = IPC_IDLE;
	}
	return (0);
}
