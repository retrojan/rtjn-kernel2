#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "udp.h"
#include "ipv4.h"
#include "time.h"

#define UDP_MAX_BIND	4

extern void rtl8139_poll(void);
extern volatile uint32_t	timer_ticks;
extern volatile uint8_t	cancel_input;

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
