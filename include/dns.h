#ifndef DNS_H
# define DNS_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

/* Resolve a hostname to an IPv4 address.
 * If 'name' is already a dotted-quad, parse it directly.
 * Otherwise sends a DNS query to net_iface.dns. */
int	dns_resolve(const char *name, net_ip4_t *out);

#endif
