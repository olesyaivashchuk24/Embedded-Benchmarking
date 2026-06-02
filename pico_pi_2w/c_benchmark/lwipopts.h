#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1

#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

#define LWIP_TCP 1

#define MEM_SIZE 4000

#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)

#define LWIP_IPV4 1
#define LWIP_ARP 1

#define LWIP_DNS 1

#define LWIP_DEBUG 0

#endif