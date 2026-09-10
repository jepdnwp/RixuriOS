#pragma once
#include <stdint.h>

/* IPv4 address family. Addresses are network-byte-order values held in
 * host integers (0x7f000001 is 127.0.0.1), matching the kernel endpoint
 * representation after ntohl(). Ports in sockaddr_in are network order
 * per POSIX; the <sys/socket.h> layer converts with ntohs(). */

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    uint16_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

#define INADDR_ANY ((in_addr_t)0x00000000u)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001u)
#define INADDR_BROADCAST ((in_addr_t)0xffffffffu)

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

static inline uint16_t htons(uint16_t value) {
    return (uint16_t)(((value & 0xffu) << 8) | (value >> 8));
}
static inline uint16_t ntohs(uint16_t value) {
    return htons(value);
}
static inline uint32_t htonl(uint32_t value) {
    return ((value & 0x000000ffu) << 24) | ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) | ((value & 0xff000000u) >> 24);
}
static inline uint32_t ntohl(uint32_t value) {
    return htonl(value);
}
