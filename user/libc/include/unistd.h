#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int64_t rix_ssize_t;
typedef uint64_t rix_pid_t;
rix_ssize_t write(int fd,const void *buf,size_t count);
int openat(int dirfd, const char *path, uint32_t flags, uint32_t mode);
int close(int fd);
int pipe(int fds[2]);
int dup(int old_fd);
rix_pid_t spawn(const char *name, const void *image, size_t image_size);
rix_pid_t fork(void);
rix_pid_t wait(rix_pid_t child, uint64_t *status);
int execve(const char *path, char *const argv[], char *const envp[]);
rix_pid_t getpid(void);
_Noreturn void _exit(int status);
