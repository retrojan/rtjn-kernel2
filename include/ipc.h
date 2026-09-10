#ifndef IPC_H
# define IPC_H

# include <stdint.h>

# define IPC_MSG_SIZE		128
# define IPC_SEND_ANY		0

typedef struct s_ipc_msg
{
	uint32_t	type;
	uint32_t	from;
	uint32_t	data[IPC_MSG_SIZE / sizeof(uint32_t) - 2];
}				ipc_msg_t;

enum e_ipc_state
{
	IPC_IDLE = 0,
	IPC_SENDING,
	IPC_RECEIVING,
	IPC_REPLYING
};

typedef struct s_ipc_slot
{
	int			state;
	uint32_t	partner;
	ipc_msg_t	msg;
}				ipc_slot_t;

int		ipc_init(void);
int		ipc_send(uint32_t dest, ipc_msg_t *msg);
int		ipc_recv(uint32_t src, ipc_msg_t *msg);
int		ipc_reply(uint32_t dest, ipc_msg_t *msg);

#endif
