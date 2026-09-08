#ifndef ICMP_H
# define ICMP_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

int	icmp_handle(uint8_t *pkt, size_t len, net_ip4_t src);
/* send an echo request and wait for a reply; returns rtt in ticks or -1 */
int	icmp_ping(net_ip4_t dst, uint32_t *rtt_ticks);

#endif
