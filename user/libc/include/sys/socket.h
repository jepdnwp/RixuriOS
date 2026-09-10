#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>

/* POSIX socket layer over the RixuriOS native socket syscalls.
 *
 * Supported: AF_INET + SOCK_DGRAM / SOCK_STREAM against 127.0.0.1
 * (loopback delivery inside the calling process table) and the
 * external IPv4 path where the kernel device stack already serves it
 * (UDP/TCP/DHCP-configured QEMU user-net). Everything else fails
 * closed: non-AF_INET domains, SOCK_RAW, listen()/accept() (no passive
 * TCP in the kernel), and non-loopback bind() (kernel only binds
 * 127.0.0.1).
 *
 * fd namespace: socket descriptors live in the per-process socket
 * table, separate from VFS fds. close() releases either (VFS first,
 * then sockets). read()/write() on socket fds are UNSUPPORTED; use
 * send()/recv() and friends.
 *
 * Empty-queue/ARP-pending conditions surface as EAGAIN, oversized
 * datagrams as EMSGSIZE, connect timeouts as ETIMEDOUT (see errno). */

typedef uint32_t socklen_t;

struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

#define AF_UNSPEC 0
#define AF_INET 2
#define PF_INET AF_INET

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3

#define SOL_SOCKET 1
#define SO_REUSEADDR 2

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define SOMAXCONN 8

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *destination, socklen_t dest_len);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *source, socklen_t *source_len);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
