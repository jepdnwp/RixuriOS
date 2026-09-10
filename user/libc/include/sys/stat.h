#pragma once
#include <stdint.h>
#include <sys/types.h>
#include "../unistd.h"
#define S_IFMT 0170000u
#define S_IFREG 0100000u
#define S_IFDIR 0040000u
#define S_IFLNK 0120000u
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISLNK(mode) (((mode) & S_IFMT) == S_IFLNK)
#define S_IRUSR 0400u
#define S_IWUSR 0200u
#define S_IXUSR 0100u
#define S_IRGRP 0040u
#define S_IWGRP 0020u
#define S_IXGRP 0010u
#define S_IROTH 0004u
#define S_IWOTH 0002u
#define S_IXOTH 0001u
#define S_IRWXU 0700u
#define S_IRWXG 0070u
#define S_IRWXO 0007u
/* No kernel fd-stat/readlink yet: always fail closed with ENOSYS. */
int fstat(int fd, rix_stat_t *out);
int lstat(const char *path, rix_stat_t *out);
