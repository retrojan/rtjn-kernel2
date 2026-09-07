#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "ipv4.h"
#include "icmp.h"
#include "time.h"

#define ICMP_TYPE_ECHO_REPLY	0
#define ICMP_TYPE_ECHO_REQUEST	8

extern void rtl8139_poll(void);

extern volatile uint32_t	timer_ticks;
extern volatile uint8_t	cancel_input;

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
