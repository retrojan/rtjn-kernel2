#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "dns.h"
#include "tcp.h"
#include "http.h"
#include "time.h"

extern volatile uint32_t	timer_ticks;

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
