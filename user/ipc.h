#ifndef USER_IPC_H
# define USER_IPC_H

# include <stdint.h>
# include "../include/syscall.h"

# define IPC_MSG_SIZE	128

typedef struct s_ipc_msg
{
	uint32_t	type;
	uint32_t	from;
	uint32_t	data[IPC_MSG_SIZE / sizeof(uint32_t) - 2];
}				ipc_msg_t;

static inline int	syscall3(int n, int a, int b, int c)
{
	int	r;

	__asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c));
	return (r);
}

static inline int	ipc_send(uint32_t dest, ipc_msg_t *msg)
{
	return (syscall3(SYS_IPC_SEND, (int)dest, (int)msg, 0));
}

static inline int	ipc_recv(uint32_t src, ipc_msg_t *msg)
{
	return (syscall3(SYS_IPC_RECV, (int)src, (int)msg, 0));
}

static inline int	ipc_reply(uint32_t dest, ipc_msg_t *msg)
{
	return (syscall3(SYS_IPC_REPLY, (int)dest, (int)msg, 0));
}

#endif
