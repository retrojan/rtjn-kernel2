#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "udp.h"
#include "dns.h"
#include "time.h"

#define DNS_PORT		53
#define DNS_MAX_NAME	255

extern volatile uint32_t	timer_ticks;

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
