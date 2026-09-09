#pragma once
#include <stdint.h>
#include "../unistd.h"
#define S_IFMT 0170000u
#define S_IFREG 0100000u
#define S_IFDIR 0040000u
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_IRUSR 0400u
#define S_IWUSR 0200u
#define S_IXUSR 0100u
