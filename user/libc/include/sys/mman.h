#pragma once
#include <stddef.h>
#include <sys/types.h>

/* Memory-mapping compatibility surface.
 *
 * PROT_* and MAP_* spellings exist so portable programs compile; the
 * backing syscalls (RIX_SYS_MMAP/MPROTECT/MUNMAP) are NOT implemented by
 * the kernel and fail closed with ENOSYS. The supported dynamic-memory
 * path remains brk/sbrk/malloc (see <stdlib.h>). Anonymous private
 * mappings are the only form that could be honored in the future; file
 * mappings have no design yet. */

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 16
#define MAP_ANONYMOUS 32
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void *)-1)

void *mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
