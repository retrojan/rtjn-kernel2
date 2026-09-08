#ifndef UDP_H
# define UDP_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

typedef void (*udp_rx_cb_t)(uint8_t *data, size_t len);

/* register a callback for a given destination port (we are a server on this port) */
int	udp_bind(uint16_t port, udp_rx_cb_t cb);
/* send a UDP datagram */
int	udp_send(net_ip4_t dst, uint16_t src_port, uint16_t dst_port, uint8_t *payload, size_t len);
int	udp_handle(uint8_t *pkt, size_t len);

/* one-shot request/response helper used by DNS */
typedef struct udp_transaction
{
	net_ip4_t	remote;
	uint16_t	remote_port;
	uint16_t	local_port;
	uint16_t	id;
	uint8_t		reply[512];
	size_t		reply_len;
	int			done;
} udp_transaction_t;

/* start a transaction: send payload, wait for a reply on local_port */
int		udp_exchange(net_ip4_t dst, uint16_t dst_port, uint8_t *payload, size_t len,
			uint16_t local_port, udp_transaction_t *tx, uint32_t timeout_ticks);

#endif
