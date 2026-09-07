#ifndef IPV4_H
# define IPV4_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

# define IP_PROTO_ICMP	1
# define IP_PROTO_TCP	6
# define IP_PROTO_UDP	17

/* returns 0 on success */
int	ipv4_send(net_ip4_t dst, uint8_t proto, uint16_t src_port,
	uint16_t dst_port, uint8_t *payload, size_t len);
int	ipv4_handle(uint8_t *pkt, size_t len);

#endif
