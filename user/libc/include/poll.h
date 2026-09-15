#pragma once
#include <stddef.h>

/* I/O readiness compatibility surface.
 *
 * The kernel implements bounded, immediate readiness checks for socket
 * descriptors with bounded timeout waits. */

typedef unsigned long nfds_t;
struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN 1
#define POLLPRI 2
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32

int poll(struct pollfd *fds, nfds_t count, int timeout_ms);
