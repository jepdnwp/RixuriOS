#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../unistd.h"
#define RIX_KLOG_SIZE 65536u
rix_ssize_t klog_read(void *buffer, size_t capacity, uint64_t cursor, uint64_t *next);
