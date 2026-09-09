#pragma once
#include <stdint.h>
struct dirent { uint64_t d_ino; uint8_t d_type; char d_name[256]; };
typedef struct dirent DIR;
