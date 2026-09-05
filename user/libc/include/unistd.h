#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int64_t rix_ssize_t;
typedef uint64_t rix_pid_t;
rix_ssize_t write(int fd,const void *buf,size_t count);
rix_pid_t getpid(void);
_Noreturn void _exit(int status);
