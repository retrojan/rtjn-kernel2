#ifndef ARP_H
# define ARP_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

/* ARP cache entry */
typedef struct arp_entry
{
	net_ip4_t	ip;
	net_mac_t	mac;
	int			valid;
	uint32_t	age;
} arp_entry_t;

int		arp_handle(uint8_t *pkt, size_t len);
/* Resolve an IP to a MAC; returns 0 on success and fills *mac.
 * May block briefly waiting for a reply. */
int		arp_resolve(net_ip4_t ip, net_mac_t *mac);
void	arp_cache_add(net_ip4_t ip, net_mac_t mac);
void	arp_cache_boot(void);
void	arp_cache_dump(void);

/* called periodically; returns current tick */
extern volatile uint32_t	timer_ticks;

#endif
