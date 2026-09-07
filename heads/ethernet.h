#ifndef ETHERNET_H
# define ETHERNET_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

# define ETH_TYPE_IPV4	0x0800
# define ETH_TYPE_ARP	0x0806
# define ETH_TYPE_IPV6	0x86DD

int	eth_send(net_mac_t dst, uint16_t ethertype, uint8_t *payload, size_t len);
int	net_handle_frame(uint8_t *frame, size_t len);
void	eth_init(void);

#endif
