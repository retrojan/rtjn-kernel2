#ifndef NET_H
# define NET_H

# include <stdint.h>
# include <stddef.h>

/* ---- MAC address ---- */
typedef struct net_mac
{
	uint8_t b[6];
} net_mac_t;

/* ---- IPv4 address ---- */
typedef struct net_ip4
{
	uint8_t b[4];
} net_ip4_t;

#define NET_IP4_STR_LEN 16

/* ---- Global host configuration ---- */
# define NET_IFACE_NAME_LEN	32

typedef struct net_iface
{
	char		name[NET_IFACE_NAME_LEN];
	net_mac_t	mac;
	net_ip4_t	ip;
	net_ip4_t	netmask;
	net_ip4_t	gateway;
	net_ip4_t	dns;
	int			up;
} net_iface_t;

extern net_iface_t	net_iface;

uint32_t	ip4_to_u32(net_ip4_t ip);
net_ip4_t	u32_to_ip4(uint32_t v);
int			ip4_to_string(net_ip4_t ip, char *out, size_t n);
int			string_to_ip4(const char *s, net_ip4_t *out);

/* Network buffer (chainless, fixed max size) for transmit */
# define NET_BUF_SIZE	1518
# define NET_PAYLOAD_MAX 1500

typedef struct net_buffer
{
	uint8_t	data[NET_BUF_SIZE];
	size_t	len;
} net_buffer_t;

/* ---- Packet protocol handlers ---
 * A protocol handler is called back from the receive path.
 * Returns 0 if it consumed/understands the packet, nonzero otherwise.
 */
typedef int (*net_rx_handler_t)(uint8_t *pkt, size_t len);

void	net_init(void);
void	net_link_up(net_mac_t mac);
void	net_driver_init(void);

/* Called by the NIC when a frame arrives */
void	net_receive(uint8_t *frame, size_t len);

/* Send a raw frame (adds nothing) on the default interface */
int		net_send_frame(uint8_t *frame, size_t len);

#endif
