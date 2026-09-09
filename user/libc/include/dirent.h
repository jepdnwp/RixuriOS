#pragma once
#include <stdint.h>
#include <stddef.h>
struct dirent { uint64_t d_ino; uint8_t d_type; char d_name[256]; };
typedef struct { int fd; size_t count; size_t index; struct dirent entries[16]; } DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
