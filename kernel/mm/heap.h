#pragma once

#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size, size_t alignment);
void kfree(void *ptr);
