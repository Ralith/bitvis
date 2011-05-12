#ifndef _NET_H_
#define _NET_H_

#include <netdb.h>

int resolve(const char *host, const char *port, struct addrinfo **servinfo);

#endif
