#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "ipv4.h"
#include "tcp.h"
#include "time.h"

extern volatile uint32_t	timer_ticks;
extern volatile uint8_t	cancel_input;
extern void rtl8139_poll(void);

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
