#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"

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
