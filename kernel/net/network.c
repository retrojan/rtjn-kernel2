#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "time.h"
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "dns.h"
#include "http.h"
#include "pci.h"
#include "rtl8139.h"

/* External symbols coming from the RTL8139 NIC driver (dev/rtl8139.c). */
extern void			rtl8139_poll(void);
extern volatile uint32_t	timer_ticks;
extern volatile uint8_t		cancel_input;

/* ============================================================
**  NETWORK CORE
** ============================================================ */

net_iface_t	net_iface;

uint32_t	ip4_to_u32(net_ip4_t ip)
{
	return (((uint32_t)ip.b[0] << 24) | ((uint32_t)ip.b[1] << 16) |
		((uint32_t)ip.b[2] << 8) | (uint32_t)ip.b[3]);
}

net_ip4_t	u32_to_ip4(uint32_t v)
{
	net_ip4_t	ip;

	ip.b[0] = (uint8_t)((v >> 24) & 0xFF);
	ip.b[1] = (uint8_t)((v >> 16) & 0xFF);
	ip.b[2] = (uint8_t)((v >> 8) & 0xFF);
	ip.b[3] = (uint8_t)(v & 0xFF);
	return (ip);
}

int	ip4_to_string(net_ip4_t ip, char *out, size_t n)
{
	char	tmp[4];
	size_t	used = 0;

	for (int i = 0; i < 4; i++)
	{
		if (i > 0 && used + 1 < n)
			out[used++] = '.';
		itoa_buf(ip.b[i], tmp);
		size_t	tl = strlen(tmp);
		if (used + tl >= n)
			return (-1);
		memcpy(out + used, tmp, tl);
		used += tl;
	}
	if (used < n)
		out[used] = 0;
	return (0);
}

int	string_to_ip4(const char *s, net_ip4_t *out)
{
	int	octets[4] = {0, 0, 0, 0};
	int	idx = 0;
	int	cur = 0;

	if (!s)
		return (-1);
	while (*s)
	{
		if (*s >= '0' && *s <= '9')
		{
			cur = cur * 10 + (*s - '0');
			if (cur > 255)
				return (-1);
		}
		else if (*s == '.')
		{
			if (idx >= 3)
				return (-1);
			octets[idx++] = cur;
			cur = 0;
		}
		else
			return (-1);
		s++;
	}
	if (idx != 3)
		return (-1);
	octets[3] = cur;
	if (out)
	{
		for (int i = 0; i < 4; i++)
			out->b[i] = (uint8_t)octets[i];
	}
	return (0);
}

void	net_init(void)
{
	strcpy(net_iface.name, "eth0");
	bzero(&net_iface.mac, sizeof(net_iface.mac));
	string_to_ip4("10.0.2.15", &net_iface.ip);
	string_to_ip4("255.255.255.0", &net_iface.netmask);
	string_to_ip4("10.0.2.2", &net_iface.gateway);
	string_to_ip4("10.0.2.3", &net_iface.dns);
	net_iface.up = 0;
}

void	net_link_up(net_mac_t mac)
{
	net_iface.mac = mac;
	net_iface.up = 1;
	printk("net: link up on %s, MAC %x:%x:%x:%x:%x:%x\n",
		net_iface.name,
		mac.b[0], mac.b[1], mac.b[2], mac.b[3], mac.b[4], mac.b[5]);
}

void	net_receive(uint8_t *frame, size_t len)
{
	net_handle_frame(frame, len);
}

int	net_send_frame(uint8_t *frame, size_t len)
{
	extern int	rtl8139_tx(uint8_t *data, size_t len);

	if (!net_iface.up)
		return (-1);
	return (rtl8139_tx(frame, len));
}

void	net_driver_init(void)
{
	pci_device_t	dev;

	eth_init();
	arp_cache_boot();

	if (pci_find_device(PCI_VENDOR_REALTEK, PCI_DEVICE_RTL8139, &dev) >= 0)
	{
		printk("net: found RTL8139 (%x:%x) at bus %d slot %d IRQ %d\n",
			dev.vendor, dev.device, dev.bus, dev.slot, dev.irq_line);
		rtl8139_init(&dev);
		net_link_up(net_iface.mac);
	}
	else
	{
		printk("net: no supported NIC found\n");
	}
}

/* ============================================================
**  ETHERNET
** ============================================================ */

typedef struct eth_header
{
	uint8_t	dst[6];
	uint8_t	src[6];
	uint16_t type;
} eth_header_t;

int	eth_send(net_mac_t dst, uint16_t ethertype, uint8_t *payload, size_t len)
{
	net_buffer_t	frame;

	if (len > NET_PAYLOAD_MAX)
		return (-1);
	memcpy(frame.data, dst.b, 6);
	memcpy(frame.data + 6, net_iface.mac.b, 6);
	frame.data[12] = (uint8_t)((ethertype >> 8) & 0xFF);
	frame.data[13] = (uint8_t)(ethertype & 0xFF);
	memcpy(frame.data + 14, payload, len);
	frame.len = 14 + len;
	return (net_send_frame(frame.data, frame.len));
}

int	net_handle_frame(uint8_t *frame, size_t len)
{
	if (len < 14)
		return (-1);
	eth_header_t	*eth = (eth_header_t*)frame;
	uint16_t	type = (uint16_t)((eth->type >> 8) | (eth->type << 8));

	/* update our MAC from what the NIC reported */
	if (net_iface.mac.b[0] == 0 && net_iface.mac.b[1] == 0 &&
		net_iface.mac.b[2] == 0 && net_iface.mac.b[3] == 0 &&
		net_iface.mac.b[4] == 0 && net_iface.mac.b[5] == 0)
	{
		memcpy(net_iface.mac.b, eth->src, 6);
	}

	switch (type)
	{
		case ETH_TYPE_ARP:
			return (arp_handle(frame + 14, len - 14));
		case ETH_TYPE_IPV4:
			return (ipv4_handle(frame + 14, len - 14));
		default:
			return (-1);
	}
}

void	eth_init(void)
{
}

/* ============================================================
**  ARP
** ============================================================ */

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

/* ============================================================
**  IPV4
** ============================================================ */

typedef struct ipv4_header
{
	uint8_t		ver_ihl;
	uint8_t		tos;
	uint16_t	total_len;
	uint16_t	id;
	uint16_t	flags_frag;
	uint8_t		ttl;
	uint8_t		protocol;
	uint16_t	checksum;
	uint8_t		src[4];
	uint8_t		dst[4];
} ipv4_header_t;

static uint16_t	ip_checksum(uint8_t *data, size_t len)
{
	uint32_t	sum = 0;

	while (len > 1)
	{
		sum += (uint16_t)((data[0] << 8) | data[1]);
		data += 2;
		len -= 2;
	}
	if (len)
		sum += (uint16_t)(data[0] << 8);
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ((uint16_t)~sum);
}

static int	is_local_network(net_ip4_t ip)
{
	uint32_t	a = ip4_to_u32(ip);
	uint32_t	local = ip4_to_u32(net_iface.ip);
	uint32_t	mask = ip4_to_u32(net_iface.netmask);

	return ((a & mask) == (local & mask));
}

int	ipv4_send(net_ip4_t dst, uint8_t proto, uint16_t src_port,
	uint16_t dst_port, uint8_t *payload, size_t len)
{
	(void)src_port; (void)dst_port;
	if (!net_iface.up || len > NET_PAYLOAD_MAX - 20)
		return (-1);

	ipv4_header_t	hdr;
	bzero(&hdr, sizeof(hdr));
	hdr.ver_ihl = 0x45;
	hdr.tos = 0;
	uint16_t	total = (uint16_t)(20 + len);
	hdr.total_len = (uint16_t)((total >> 8) | (total << 8));
	hdr.id = (uint16_t)((0x4242 >> 8) | (0x4242 << 8));
	hdr.flags_frag = 0;
	hdr.ttl = 64;
	hdr.protocol = proto;
	memcpy(hdr.src, net_iface.ip.b, 4);
	memcpy(hdr.dst, dst.b, 4);
	hdr.checksum = 0;
	hdr.checksum = (uint16_t)((ip_checksum((uint8_t*)&hdr, 20) >> 8) |
		(ip_checksum((uint8_t*)&hdr, 20) << 8));

	net_buffer_t	pkt;
	memcpy(pkt.data, &hdr, 20);
	memcpy(pkt.data + 20, payload, len);
	pkt.len = 20 + len;

	net_ip4_t	next_hop = dst;
	if (!is_local_network(dst))
		next_hop = net_iface.gateway;

	net_mac_t	mac;
	if (arp_resolve(next_hop, &mac) != 0)
	{
		printk("ipv4: ARP resolve failed for next hop\n");
		return (-1);
	}
	return (eth_send(mac, ETH_TYPE_IPV4, pkt.data, pkt.len));
}

int	ipv4_handle(uint8_t *pkt, size_t len)
{
	if (len < 20)
		return (-1);
	ipv4_header_t	*h = (ipv4_header_t*)pkt;
	uint16_t	total = (uint16_t)((h->total_len >> 8) | (h->total_len << 8));

	/* verify destination is us or broadcast */
	net_ip4_t	dst = u32_to_ip4((uint32_t)((h->dst[0] << 24) | (h->dst[1] << 16) |
		(h->dst[2] << 8) | h->dst[3]));
	if (memcmp(dst.b, net_iface.ip.b, 4) != 0)
		return (0);

	if (total > len)
		total = (uint16_t)len;
	if (total < 20)
		return (-1);
	uint8_t	ihl = (uint8_t)((h->ver_ihl & 0x0F) * 4);
	if (ihl < 20)
		return (-1);
	uint8_t	proto = h->protocol;

	switch (proto)
	{
		case IP_PROTO_ICMP:
			return (icmp_handle(pkt + ihl, total - ihl, *((net_ip4_t*)h->src)));
		case IP_PROTO_UDP:
			return (udp_handle(pkt + ihl, total - ihl));
		case IP_PROTO_TCP:
			return (tcp_handle(pkt + ihl, total - ihl));
		default:
			return (-1);
	}
}

/* ============================================================
**  ICMP
** ============================================================ */

#define ICMP_TYPE_ECHO_REPLY	0
#define ICMP_TYPE_ECHO_REQUEST	8

typedef struct icmp_header
{
	uint8_t	type;
	uint8_t	code;
	uint16_t checksum;
	uint16_t id;
	uint16_t seq;
} icmp_header_t;

static uint16_t	icmp_checksum(uint8_t *data, size_t len)
{
	uint32_t	sum = 0;

	while (len > 1)
	{
		sum += (uint16_t)((data[0] << 8) | data[1]);
		data += 2;
		len -= 2;
	}
	if (len)
		sum += (uint16_t)(data[0] << 8);
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ((uint16_t)~sum);
}

static int	icmp_waiting = 0;
static uint16_t	icmp_wait_id;
static uint16_t	icmp_wait_seq;
static uint32_t	icmp_wait_start;
static int	icmp_reply_recv = 0;

int	icmp_handle(uint8_t *pkt, size_t len, net_ip4_t src)
{
	(void)src;
	if (len < sizeof(icmp_header_t))
		return (-1);
	icmp_header_t	*h = (icmp_header_t*)pkt;

	if (h->type == ICMP_TYPE_ECHO_REQUEST)
	{
		icmp_header_t	reply;
		reply.type = ICMP_TYPE_ECHO_REPLY;
		reply.code = 0;
		reply.checksum = 0;
		reply.id = h->id;
		reply.seq = h->seq;
		uint8_t	*body = pkt + sizeof(icmp_header_t);
		size_t	blen = len - sizeof(icmp_header_t);
		reply.checksum = (uint16_t)((icmp_checksum((uint8_t*)&reply, sizeof(reply)) >> 8) |
			(icmp_checksum((uint8_t*)&reply, sizeof(reply)) << 8));
		/* combine header+body checksum */
		uint8_t	tmp[512];
		memcpy(tmp, &reply, sizeof(reply));
		memcpy(tmp + sizeof(reply), body, blen > 512 - sizeof(reply) ? 512 - sizeof(reply) : blen);
		uint16_t	csum = icmp_checksum(tmp, sizeof(reply) + (blen > 512 - sizeof(reply) ? 512 - sizeof(reply) : blen));
		reply.checksum = (uint16_t)((csum >> 8) | (csum << 8));
		uint8_t	out[512];
		memcpy(out, &reply, sizeof(reply));
		memcpy(out + sizeof(reply), body, blen > 512 - sizeof(reply) ? 512 - sizeof(reply) : blen);
		ipv4_send(src, IP_PROTO_ICMP, 0, 0, out, sizeof(reply) + (blen > 512 - sizeof(reply) ? 512 - sizeof(reply) : blen));
		return (0);
	}
	else if (h->type == ICMP_TYPE_ECHO_REPLY)
	{
		if (icmp_waiting && h->id == icmp_wait_id && h->seq == icmp_wait_seq)
		{
			icmp_reply_recv = 1;
		}
	}
	return (0);
}

int	icmp_ping(net_ip4_t dst, uint32_t *rtt_ticks)
{
	icmp_header_t	req;
	req.type = ICMP_TYPE_ECHO_REQUEST;
	req.code = 0;
	req.checksum = 0;
	req.id = (uint16_t)((uint32_t)timer_ticks & 0xFFFF);
	req.seq = 1;
	uint16_t	csum = icmp_checksum((uint8_t*)&req, sizeof(req));
	req.checksum = (uint16_t)((csum >> 8) | (csum << 8));

	icmp_waiting = 1;
	icmp_wait_id = req.id;
	icmp_wait_seq = req.seq;
	icmp_reply_recv = 0;
	icmp_wait_start = (uint32_t)timer_ticks;

	if (ipv4_send(dst, IP_PROTO_ICMP, 0, 0, (uint8_t*)&req, sizeof(req)) != 0)
	{
		icmp_waiting = 0;
		return (-1);
	}

	while (!icmp_reply_recv)
	{
		rtl8139_poll();
		if (cancel_input)
			break;
		if ((uint32_t)timer_ticks - icmp_wait_start >= 60)
			break;
	}
	icmp_waiting = 0;
	if (!icmp_reply_recv)
		return (-1);
	if (rtt_ticks)
		*rtt_ticks = (uint32_t)timer_ticks - icmp_wait_start;
	return (0);
}

/* ============================================================
**  UDP
** ============================================================ */

#define UDP_MAX_BIND	4

typedef struct udp_header
{
	uint16_t src_port;
	uint16_t dst_port;
	uint16_t length;
	uint16_t checksum;
} udp_header_t;

typedef struct udp_bind_slot
{
	uint16_t		port;
	udp_rx_cb_t		cb;
	int				used;
} udp_bind_slot_t;

static udp_bind_slot_t	udp_binds[UDP_MAX_BIND];

/* pending transaction handling */
static udp_transaction_t	*g_active_tx = 0;

static udp_bind_slot_t	*udp_find_bind(uint16_t port)
{
	for (int i = 0; i < UDP_MAX_BIND; i++)
	{
		if (udp_binds[i].used && udp_binds[i].port == port)
			return (&udp_binds[i]);
	}
	return (0);
}

int	udp_bind(uint16_t port, udp_rx_cb_t cb)
{
	for (int i = 0; i < UDP_MAX_BIND; i++)
	{
		if (!udp_binds[i].used)
		{
			udp_binds[i].port = port;
			udp_binds[i].cb = cb;
			udp_binds[i].used = 1;
			return (0);
		}
	}
	return (-1);
}

int	udp_send(net_ip4_t dst, uint16_t src_port, uint16_t dst_port, uint8_t *payload, size_t len)
{
	if (len > NET_PAYLOAD_MAX - 20 - 8)
		return (-1);

	udp_header_t	hdr;
	hdr.src_port = (uint16_t)((src_port >> 8) | (src_port << 8));
	hdr.dst_port = (uint16_t)((dst_port >> 8) | (dst_port << 8));
	uint16_t	ulen = (uint16_t)(8 + len);
	hdr.length = (uint16_t)((ulen >> 8) | (ulen << 8));
	hdr.checksum = 0;	/* IPv4 allows 0 */

	net_buffer_t	dat;
	memcpy(dat.data, &hdr, 8);
	memcpy(dat.data + 8, payload, len);
	return (ipv4_send(dst, IP_PROTO_UDP, src_port, dst_port, dat.data, 8 + len));
}

int	udp_handle(uint8_t *pkt, size_t len)
{
	if (len < 8)
		return (-1);
	udp_header_t	*h = (udp_header_t*)pkt;
	uint16_t	dst_port = (uint16_t)((h->dst_port >> 8) | (h->dst_port << 8));
	uint8_t		*payload = pkt + 8;
	size_t		plen = len - 8;

	if (g_active_tx && g_active_tx->local_port == dst_port)
	{
		if (plen <= sizeof(g_active_tx->reply))
		{
			memcpy(g_active_tx->reply, payload, plen);
			g_active_tx->reply_len = plen;
			g_active_tx->done = 1;
		}
		return (0);
	}

	udp_bind_slot_t	*b = udp_find_bind(dst_port);
	if (b && b->cb)
		b->cb(payload, plen);
	return (0);
}

int	udp_exchange(net_ip4_t dst, uint16_t dst_port, uint8_t *payload, size_t len,
	uint16_t local_port, udp_transaction_t *tx, uint32_t timeout_ticks)
{
	if (!tx)
		return (-1);
	tx->remote = dst;
	tx->remote_port = dst_port;
	tx->local_port = local_port;
	tx->done = 0;
	tx->reply_len = 0;

	g_active_tx = tx;
	uint32_t	start = (uint32_t)timer_ticks;
	if (udp_send(dst, local_port, dst_port, payload, len) != 0)
	{
		g_active_tx = 0;
		return (-1);
	}
	while (!tx->done)
	{
		rtl8139_poll();
		if (cancel_input)
			break;
		if ((uint32_t)timer_ticks - start >= timeout_ticks)
			break;
	}
	g_active_tx = 0;
	return (tx->done ? 0 : -1);
}

/* ============================================================
**  TCP
** ============================================================ */

typedef struct tcp_header
{
	uint16_t src_port;
	uint16_t dst_port;
	uint32_t seq;
	uint32_t ack;
	uint8_t	data_off;
	uint8_t	flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent;
} tcp_header_t;

#define TCP_FIN	0x01
#define TCP_SYN	0x02
#define TCP_RST	0x04
#define TCP_PSH	0x08
#define TCP_ACK	0x10

static tcp_conn_t	*current_conn = 0;

static uint16_t	htons16(uint16_t x)
{
	return ((uint16_t)((x >> 8) | (x << 8)));
}

static uint32_t	htons32(uint32_t x)
{
	return (((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
		((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000));
}

static uint32_t	ntohs32(uint32_t x)
{
	return (htons32(x));
}

static uint16_t	ones_complement(uint32_t sum)
{
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ((uint16_t)~sum);
}

static uint16_t	tcp_checksum(uint8_t *src_ip, uint8_t *dst_ip, uint8_t proto,
	uint8_t *tcp, size_t tcp_len)
{
	uint32_t	sum = 0;

	for (int i = 0; i < 4; i += 2)
		sum += (uint16_t)((src_ip[i] << 8) | src_ip[i + 1]);
	for (int i = 0; i < 4; i += 2)
		sum += (uint16_t)((dst_ip[i] << 8) | dst_ip[i + 1]);
	sum += (uint32_t)proto;
	sum += (uint32_t)tcp_len;

	size_t	i = 0;
	for (; i + 1 < tcp_len; i += 2)
		sum += (uint16_t)((tcp[i] << 8) | tcp[i + 1]);
	if (i < tcp_len)
		sum += (uint16_t)(tcp[i] << 8);
	return (ones_complement(sum));
}

static int	tcp_send_seg(tcp_conn_t *c, uint8_t flags, uint8_t *data, size_t len)
{
	net_buffer_t	buf;
	tcp_header_t	*h = (tcp_header_t*)buf.data;

	bzero(buf.data, sizeof(buf.data));
	h->src_port = htons16(c->src_port);
	h->dst_port = htons16(c->dst_port);
	h->seq = htons32(c->my_seq);
	h->ack = htons32(c->my_ack);
	h->data_off = (uint8_t)((5 << 4) | 0);
	h->flags = flags;
	h->window = htons16(65535);
	h->checksum = 0;
	h->urgent = 0;

	size_t	total = sizeof(tcp_header_t) + len;
	if (len)
		memcpy(buf.data + sizeof(tcp_header_t), data, len);

	h->checksum = 0;
	uint16_t	csum = tcp_checksum(net_iface.ip.b, c->remote.b, IP_PROTO_TCP, buf.data, total);
	h->checksum = htons16(csum);

	int	ret = ipv4_send(c->remote, IP_PROTO_TCP, c->src_port, c->dst_port, buf.data, total);
	if (ret == 0 && (flags & TCP_SYN))
		c->my_seq++;
	return (ret);
}

int	tcp_connect(tcp_conn_t *c, net_ip4_t dst, uint16_t dst_port)
{
	bzero(c, sizeof(*c));
	c->remote = dst;
	c->dst_port = dst_port;
	c->src_port = (uint16_t)(49152 + ((uint32_t)timer_ticks % 5000));
	c->my_seq = (uint32_t)timer_ticks + 1;
	c->my_ack = 0;
	c->active = 1;
	current_conn = c;

	uint32_t	start = (uint32_t)timer_ticks;
	if (tcp_send_seg(c, TCP_SYN, 0, 0) != 0)
	{
		c->active = 0;
		current_conn = 0;
		return (-1);
	}
	c->syn_sent = 1;

	while (!c->established)
	{
		rtl8139_poll();
		if (c->closed)
			break;
		if (cancel_input)
			break;
		if ((uint32_t)timer_ticks - start >= 500)
			break;
	}
	if (!c->established)
	{
		c->active = 0;
		current_conn = 0;
		return (-1);
	}
	return (0);
}

int	tcp_send(tcp_conn_t *c, uint8_t *data, size_t len)
{
	if (!c->established)
		return (-1);
	int	ret = tcp_send_seg(c, TCP_ACK | TCP_PSH, data, len);
	if (ret == 0)
		c->my_seq += len;
	return (ret);
}

int	tcp_recv(tcp_conn_t *c, uint8_t *buf, size_t len, uint32_t timeout_ticks)
{
	uint32_t	start = (uint32_t)timer_ticks;
	while (c->inlen == 0 && !c->closed)
	{
		rtl8139_poll();
		if (cancel_input)
			break;
		if ((uint32_t)timer_ticks - start >= timeout_ticks)
			break;
	}
	if (c->inlen == 0)
		return (c->closed ? 0 : -1);
	size_t	n = c->inlen < len ? c->inlen : len;
	memcpy(buf, c->inbuf, n);
	memmove(c->inbuf, c->inbuf + n, c->inlen - n);
	c->inlen -= n;
	return ((int)n);
}

void	tcp_close(tcp_conn_t *c)
{
	if (c->established)
		tcp_send_seg(c, TCP_ACK | TCP_FIN, 0, 0);
	c->closed = 1;
	c->active = 0;
	if (current_conn == c)
		current_conn = 0;
}

int	tcp_handle(uint8_t *pkt, size_t len)
{
	if (len < sizeof(tcp_header_t))
		return (-1);
	tcp_conn_t	*c = current_conn;
	if (!c || !c->active)
		return (-1);

	tcp_header_t	*h = (tcp_header_t*)pkt;
	uint16_t	dst_port = htons16(h->dst_port);
	if (dst_port != c->src_port)
		return (-1);

	uint8_t	flags = h->flags;
	uint8_t	data_off = (uint8_t)((h->data_off >> 4) * 4);
	if (data_off < 20)
		data_off = 20;
	if (data_off > len)
		return (-1);
	uint32_t	seq = ntohs32(h->seq);
	uint8_t	*payload = pkt + data_off;
	size_t	plen = len - data_off;

	if (flags & TCP_SYN)
	{
		c->their_seq = seq;
		c->my_ack = seq + 1;
		tcp_send_seg(c, TCP_ACK, 0, 0);
		c->established = 1;
		c->syn_sent = 0;
		return (0);
	}

	if (plen > 0)
	{
		if (c->inlen + plen <= sizeof(c->inbuf))
		{
			memcpy(c->inbuf + c->inlen, payload, plen);
			c->inlen += plen;
			c->my_ack += plen;
			tcp_send_seg(c, TCP_ACK, 0, 0);
		}
	}

	if (flags & TCP_FIN)
		c->closed = 1;

	return (0);
}

/* ============================================================
**  DNS
** ============================================================ */

#define DNS_PORT		53
#define DNS_MAX_NAME	255

/* Build a DNS name in the wire (labels) format */
static void	dns_build_name(const char *name, uint8_t *out, size_t *len)
{
	size_t	o = 0;
	size_t	lab_start = 0;

	while (name[lab_start])
	{
		size_t	lab_end = lab_start;
		while (name[lab_end] && name[lab_end] != '.')
			lab_end++;
		out[o++] = (uint8_t)(lab_end - lab_start);
		for (size_t i = lab_start; i < lab_end; i++)
			out[o++] = (uint8_t)name[i];
		if (name[lab_end] == 0)
			break;
		lab_start = lab_end + 1;
	}
	out[o++] = 0;
	if (len)
		*len = o;
}

static int	dns_skip_name(const uint8_t *pkt, uint32_t pktlen, uint32_t offset, uint32_t *next)
{
	while (1)
	{
		if (offset >= pktlen)
			return (-1);
		uint8_t	len = pkt[offset];
		if ((len & 0xC0) == 0xC0)	/* pointer */
		{
			if (next)
				*next = offset + 2;
			return (0);
		}
		if (len == 0)
		{
			if (next)
				*next = offset + 1;
			return (0);
		}
		if ((len & 0xC0) != 0)
			return (-1);
		offset += 1 + len;
		if (offset >= pktlen)
			return (-1);
	}
}

int	dns_resolve(const char *name, net_ip4_t *out)
{
	/* if it is already an IP address, just parse it */
	if (string_to_ip4(name, out) == 0)
		return (0);

	if (!net_iface.dns.b[0])
	{
		printk("dns: no DNS server configured\n");
		return (-1);
	}
	if (strlen(name) > 0 && strlen(name) < DNS_MAX_NAME)
	{
		uint8_t	query[512];
		bzero(query, sizeof(query));
		uint16_t	id = (uint16_t)((uint32_t)timer_ticks & 0xFFFF);
		query[0] = (uint8_t)(id >> 8);
		query[1] = (uint8_t)(id & 0xFF);
		query[2] = 0x01;	/* RD */
		query[3] = 0x00;
		query[4] = 0x00; query[5] = 0x01;	/* QDCOUNT = 1 */
		query[6] = 0x00; query[7] = 0x00;
		query[8] = 0x00; query[9] = 0x00;
		query[10] = 0x00; query[11] = 0x00;
		size_t	nlen = 0;
		dns_build_name(name, query + 12, &nlen);
		size_t	hlen = 12 + nlen;
		query[hlen++] = 0x00; query[hlen++] = 0x01;	/* TYPE A */
		query[hlen++] = 0x00; query[hlen++] = 0x01;	/* CLASS IN */

		udp_transaction_t	tx;
		if (udp_exchange(net_iface.dns, DNS_PORT, query, hlen, (uint16_t)(5353 + (id % 1000)),
			&tx, 300) != 0)
		{
			return (-1);
		}

		/* parse reply */
		uint8_t	*resp = tx.reply;
		size_t	rlen = tx.reply_len;
		if (rlen < 12)
			return (-1);
		uint32_t	offset = 12;
		uint16_t	qd = (uint16_t)((resp[4] << 8) | resp[5]);
		uint16_t	ans = (uint16_t)((resp[6] << 8) | resp[7]);
		(void)qd;
		for (uint16_t i = 0; i < qd && offset < rlen; i++)
		{
			uint32_t	next;
			if (dns_skip_name(resp, (uint32_t)rlen, offset, &next) != 0)
				return (-1);
			offset = next;
			offset += 4;	/* skip type + class */
		}
		for (uint16_t i = 0; i < ans && offset < rlen; i++)
		{
			uint32_t	next;
			if (dns_skip_name(resp, (uint32_t)rlen, offset, &next) != 0)
				return (-1);
			offset = next;
			if (offset + 10 > rlen)
				return (-1);
			uint16_t	type = (uint16_t)((resp[offset] << 8) | resp[offset + 1]);
			uint16_t	rdlen = (uint16_t)((resp[offset + 8] << 8) | resp[offset + 9]);
			offset += 10;
			if (type == 1 && rdlen == 4 && offset + 4 <= rlen)	/* A record */
			{
				out->b[0] = resp[offset];
				out->b[1] = resp[offset + 1];
				out->b[2] = resp[offset + 2];
				out->b[3] = resp[offset + 3];
				return (0);
			}
			offset += rdlen;
		}
	}
	return (-1);
}

/* ============================================================
**  HTTP
** ============================================================ */

#define HTTP_PORT	80
#define HTTP_BUF	2048

/* locate first occurrence of needle in hay */
static char	*strstr_c(const char *hay, const char *needle)
{
	if (!*needle)
		return ((char*)hay);
	for (; *hay; hay++)
	{
		const char	*h = hay;
		const char	*n = needle;
		while (*h && *n && *h == *n)
		{
			h++;
			n++;
		}
		if (!*n)
			return ((char*)hay);
	}
	return (0);
}

static int	parse_url(const char *url, char *host, size_t host_sz, uint16_t *port, const char **path)
{
	const char	*p = url;
	const char	*path_start = "/";

	if (strncmp(p, "http://", 7) == 0)
		p += 7;
	*port = HTTP_PORT;

	char	hostbuf[128];
	size_t	hi = 0;
	while (*p && *p != '/' && *p != ':' && hi < host_sz - 1)
		hostbuf[hi++] = *p++;
	hostbuf[hi] = 0;

	if (*p == ':')
	{
		p++;
		int	prt = 0;
		while (*p >= '0' && *p <= '9')
		{
			prt = prt * 10 + (*p - '0');
			p++;
		}
		if (prt > 0 && prt < 65536)
			*port = (uint16_t)prt;
	}
	if (*p == '/')
		path_start = p;

	if (hi == 0)
		return (-1);
	memcpy(host, hostbuf, hi + 1);
	*path = path_start;
	return (0);
}

int	http_get(const char *url, char *body, size_t body_size, int *status)
{
	char		host[128];
	uint16_t	port = HTTP_PORT;
	const char	*path;

	if (parse_url(url, host, sizeof(host), &port, &path) != 0)
	{
		printk("http: bad URL '%s'\n", url);
		return (-1);
	}

	net_ip4_t	ip;
	if (dns_resolve(host, &ip) != 0)
	{
		printk("http: cannot resolve '%s'\n", host);
		return (-1);
	}
	char	ipstr[16];
	ip4_to_string(ip, ipstr, sizeof(ipstr));
	printk("http: connecting to %s (%s) port %u\n", host, ipstr, port);

	tcp_conn_t	conn;
	if (tcp_connect(&conn, ip, port) != 0)
	{
		printk("http: connection failed\n");
		return (-1);
	}

	char	req[512];
	size_t	rlen = 0;
	rlen += (size_t)snprintf(req + rlen, sizeof(req) - rlen, "GET %s HTTP/1.1\r\n", path);
	rlen += (size_t)snprintf(req + rlen, sizeof(req) - rlen, "Host: %s\r\n", host);
	rlen += (size_t)snprintf(req + rlen, sizeof(req) - rlen, "Connection: close\r\n");
	rlen += (size_t)snprintf(req + rlen, sizeof(req) - rlen, "\r\n");

	if (tcp_send(&conn, (uint8_t*)req, rlen) != 0)
	{
		tcp_close(&conn);
		return (-1);
	}

	/* read response until connection close or buffer full */
	char	resp[HTTP_BUF];
	size_t	rpos = 0;
	int		st = -1;

	while (rpos < sizeof(resp))
	{
		int		n = tcp_recv(&conn, (uint8_t*)resp + rpos, sizeof(resp) - rpos, 300);
		if (n <= 0)
			break;
		rpos += (size_t)n;
	}

	/* parse status line */
	resp[rpos < sizeof(resp) ? rpos : sizeof(resp) - 1] = 0;
	if (strncmp(resp, "HTTP/", 5) == 0)
	{
		char	*sp = resp;
		while (*sp && *sp != ' ') sp++;
		if (*sp) sp++;
		if (*sp >= '0' && *sp <= '9')
			st = atoi(sp);
	}

	/* find body after \r\n\r\n */
	char	*bodystart = strstr_c(resp, "\r\n\r\n");
	if (bodystart)
		bodystart += 4;
	else
		bodystart = resp;

	size_t	bodylen = (size_t)(rpos - (size_t)(bodystart - resp));
	if (bodylen > body_size)
		bodylen = body_size;
	memcpy(body, bodystart, bodylen);
	if (body_size)
	{
		size_t	limit = bodylen < body_size ? bodylen : body_size - 1;
		body[limit] = 0;
	}

	tcp_close(&conn);
	if (status)
		*status = st;
	return (st);
}