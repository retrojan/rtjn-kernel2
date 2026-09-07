#ifndef HTTP_H
# define HTTP_H

# include <stdint.h>
# include <stddef.h>
# include "net.h"

/* Simple HTTP/1.1 GET client.
 * url forms: "http://host[:port]/path" or "host/path".
 * Response body (after headers) is copied into body (bounded by body_size).
 * Returns HTTP status code on success, or -1 on error.
 */
int	http_get(const char *url, char *body, size_t body_size, int *status);

#endif
