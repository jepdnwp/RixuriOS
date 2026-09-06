#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int64_t rix_ssize_t;
typedef uint64_t rix_pid_t;
typedef struct { uint64_t sec; uint64_t nsec; } rix_timespec_t;
typedef struct { uint64_t inode; uint8_t type; char name[256]; } rix_dirent_t;
typedef struct { uint64_t inode; uint8_t type; uint32_t mode; uint32_t uid; uint32_t gid; uint64_t size; } rix_stat_t;
rix_ssize_t read(int fd, void *buf, size_t count);
rix_ssize_t write(int fd,const void *buf,size_t count);
int openat(int dirfd, const char *path, uint32_t flags, uint32_t mode);
int mkdir(const char *path, uint32_t mode);
int rmdir(const char *path);
int unlink(const char *path);
int link(const char *old_path, const char *new_path);
int getdents(int fd, rix_dirent_t *entries, size_t capacity, size_t *count);
int stat(const char *path, rix_stat_t *out);
int close(int fd);
int pipe(int fds[2]);
int dup(int old_fd);
int dup2(int old_fd, int new_fd);
int close_pipes_except(int keep_fd0, int keep_fd1);
rix_pid_t spawn(const char *name, const void *image, size_t image_size);
rix_pid_t fork(void);
rix_pid_t wait(rix_pid_t child, uint64_t *status);
rix_pid_t waitpid(rix_pid_t child, uint64_t *status, uint32_t options);
int nanosleep(const rix_timespec_t *request, rix_timespec_t *remaining);
int execve(const char *path, char *const argv[], char *const envp[]);
rix_pid_t getpid(void);
int kill(rix_pid_t pid, uint32_t signal);
_Noreturn void _exit(int status);
