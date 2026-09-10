#pragma once
#include <stdint.h>
#define AT_FDCWD (-100)
#define O_RDONLY 0u
#define O_WRONLY 1u
#define O_RDWR 2u
#define O_CREAT 4u
#define O_TRUNC 8u
#define O_APPEND 16u
#define O_EXCL 32u
#define O_NONBLOCK 64u
#define O_CLOEXEC 128u
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
int fcntl(int fd, int command, ...);
