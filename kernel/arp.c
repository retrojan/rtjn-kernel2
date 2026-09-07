#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "time.h"

extern void rtl8139_poll(void);
extern volatile uint8_t	cancel_input;

#define ARP_HTYPE_ETHERNET	1
#define ARP_PTYPE_IPV4		ETH_TYPE_IPV4
#define ARP_HLEN			6
#define ARP_PLEN			4
#define ARP_OP_REQUEST		1
#define ARP_OP_REPLY		2

#define ARP_CACHE_SIZE		8
#define ARP_TIMEOUT_TICKS	200		/* await reply for ~200 timer ticks (~11s) */

typedef struct arp_packet
{
	uint16_t htype;
	uint16_t ptype;
	uint8_t	hlen;
	uint8_t	plen;
	uint16_t opcode;
	uint8_t	sha[6];
	uint8_t	spa[4];
	uint8_t	tha[6];
	uint8_t	tpa[4];
} arp_packet_t;

static arp_entry_t	arp_cache[ARP_CACHE_SIZE];
static int			arp_pending = 0;
static net_ip4_t	arp_pending_ip;
static net_mac_t	arp_pending_mac;

void	arp_cache_boot(void)
{
	bzero(arp_cache, sizeof(arp_cache));
	arp_pending = 0;
}

void	arp_cache_add(net_ip4_t ip, net_mac_t mac)
{
	/* refresh if present */
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
	{
		if (arp_cache[i].valid &&
			memcmp(arp_cache[i].ip.b, ip.b, 4) == 0)
		{
			arp_cache[i].mac = mac;
			arp_cache[i].age = (uint32_t)timer_ticks;
			return;
		}
	}
	/* find an empty or oldest slot */
	int		oldest = 0;
	uint32_t	oldest_age = 0xFFFFFFFF;
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
	{
		if (!arp_cache[i].valid)
		{
			oldest = i;
			break;
		}
		if (arp_cache[i].age < oldest_age)
		{
			oldest_age = arp_cache[i].age;
			oldest = i;
		}
	}
	arp_cache[oldest].ip = ip;
	arp_cache[oldest].mac = mac;
	arp_cache[oldest].valid = 1;
	arp_cache[oldest].age = (uint32_t)timer_ticks;

	if (arp_pending && memcmp(ip.b, arp_pending_ip.b, 4) == 0)
	{
		arp_pending_mac = mac;
		arp_pending = 0;
	}
}

static int	arp_cache_lookup(net_ip4_t ip, net_mac_t *mac)
{
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
	{
		if (arp_cache[i].valid && memcmp(arp_cache[i].ip.b, ip.b, 4) == 0)
		{
			if (mac)
				*mac = arp_cache[i].mac;
			return (0);
		}
	}
	return (-1);
}

static int	arp_send_request(net_ip4_t target_ip)
{
	arp_packet_t	pkt;

	pkt.htype = (uint16_t)((ARP_HTYPE_ETHERNET >> 8) | (ARP_HTYPE_ETHERNET << 8));
	pkt.ptype = (uint16_t)((ARP_PTYPE_IPV4 >> 8) | (ARP_PTYPE_IPV4 << 8));
	pkt.hlen = ARP_HLEN;
	pkt.plen = ARP_PLEN;
	pkt.opcode = (uint16_t)((ARP_OP_REQUEST >> 8) | (ARP_OP_REQUEST << 8));
	memcpy(pkt.sha, net_iface.mac.b, 6);
	memcpy(pkt.spa, net_iface.ip.b, 4);
	bzero(pkt.tha, 6);
	memcpy(pkt.tpa, target_ip.b, 4);

	net_mac_t	bc;
	bzero(&bc, 6);
	return (eth_send(bc, ETH_TYPE_ARP, (uint8_t*)&pkt, sizeof(pkt)));
}

int	arp_resolve(net_ip4_t ip, net_mac_t *mac)
{
	if (arp_cache_lookup(ip, mac) == 0)
		return (0);

	/* broadcast request; wait for reply */
	arp_pending = 1;
	arp_pending_ip = ip;

	uint32_t	start = (uint32_t)timer_ticks;
	if (arp_send_request(ip) != 0)
	{
		arp_pending = 0;
		return (-1);
	}

	while (arp_pending)
	{
		rtl8139_poll();
		if (cancel_input)
			break;
		if ((uint32_t)timer_ticks - start >= ARP_TIMEOUT_TICKS)
			break;
	}
	if (arp_pending)
	{
		arp_pending = 0;
		return (-1);
	}
	if (mac)
		*mac = arp_pending_mac;
	return (0);
}

void	arp_cache_dump(void)
{
	printk("ARP cache:\n");
	int	found = 0;
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
	{
		if (!arp_cache[i].valid)
			continue;
		char	ia[16], sa[16];
		ip4_to_string(arp_cache[i].ip, ia, sizeof(ia));
		snprintf(sa, sizeof(sa), "%02x:%02x:%02x:%02x:%02x:%02x",
			arp_cache[i].mac.b[0], arp_cache[i].mac.b[1], arp_cache[i].mac.b[2],
			arp_cache[i].mac.b[3], arp_cache[i].mac.b[4], arp_cache[i].mac.b[5]);
		printk("  %-15s -> %s\n", ia, sa);
		found = 1;
	}
	if (!found)
		printk("  (empty)\n");
}

int	arp_handle(uint8_t *pkt, size_t len)
{
	if (len < sizeof(arp_packet_t))
		return (-1);
	arp_packet_t	*p = (arp_packet_t*)pkt;
	uint16_t	opcode = (uint16_t)((p->opcode >> 8) | (p->opcode << 8));
	uint16_t	htype = (uint16_t)((p->htype >> 8) | (p->htype << 8));

	if (htype != ARP_HTYPE_ETHERNET)
		return (-1);
	/* record sender */
	arp_cache_add(u32_to_ip4((uint32_t)((p->spa[0] << 24) | (p->spa[1] << 16) |
		(p->spa[2] << 8) | p->spa[3])), *((net_mac_t*)p->sha));

	if (opcode == ARP_OP_REQUEST)
	{
		/* is it for us? */
		net_ip4_t	target = u32_to_ip4((uint32_t)((p->tpa[0] << 24) | (p->tpa[1] << 16) |
			(p->tpa[2] << 8) | p->tpa[3]));
		if (memcmp(target.b, net_iface.ip.b, 4) != 0)
			return (0);
		arp_packet_t	reply;
		reply.htype = p->htype;
		reply.ptype = p->ptype;
		reply.hlen = ARP_HLEN;
		reply.plen = ARP_PLEN;
		reply.opcode = (uint16_t)((ARP_OP_REPLY >> 8) | (ARP_OP_REPLY << 8));
		memcpy(reply.sha, p->tha, 6);
		memcpy(reply.spa, p->tpa, 4);
		memcpy(reply.tha, p->sha, 6);
		memcpy(reply.tpa, p->spa, 4);
		net_mac_t	dst = *((net_mac_t*)p->sha);
		eth_send(dst, ETH_TYPE_ARP, (uint8_t*)&reply, sizeof(reply));
	}
	return (0);
}
