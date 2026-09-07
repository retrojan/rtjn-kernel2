#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"

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
