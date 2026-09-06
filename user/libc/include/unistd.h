#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int64_t rix_ssize_t;
typedef uint64_t rix_pid_t;
rix_ssize_t write(int fd,const void *buf,size_t count);
int pipe(int fds[2]);
int dup(int old_fd);
rix_pid_t spawn(const char *name, const void *image, size_t image_size);
rix_pid_t getpid(void);
_Noreturn void _exit(int status);
