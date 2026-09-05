#pragma once
#include <stdint.h>
typedef uint64_t pid_t;
int process_init(void);
pid_t process_current(void);
