#ifndef TCP_H
# define TCP_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

/* A single TCP connection (client-oriented). */
typedef struct tcp_conn
{
	int			active;
	net_ip4_t	remote;
	uint16_t	src_port;
	uint16_t	dst_port;

	uint32_t	my_seq;
	uint32_t	their_seq;
	uint32_t	my_ack;

	uint8_t		inbuf[4096];
	size_t		inlen;
	int			closed;

	uint8_t		sendbuf[2048];
	size_t		sendlen;
	int			established;
	int			syn_sent;
	int			fin_sent;
} tcp_conn_t;

/* Open a connection to ip:dst and perform full HTTP over it is not here;
 * used generically. Returns 0 on connect success. */
int	tcp_connect(tcp_conn_t *c, net_ip4_t dst, uint16_t dst_port);
int	tcp_send(tcp_conn_t *c, uint8_t *data, size_t len);
/* Read up to len bytes into buf; returns bytes read (blocks until data or close). */
int	tcp_recv(tcp_conn_t *c, uint8_t *buf, size_t len, uint32_t timeout_ticks);
void	tcp_close(tcp_conn_t *c);
int	tcp_handle(uint8_t *pkt, size_t len);

#endif
