#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include <stdint.h>

int errno;

void *memcpy(void *destination, const void *source, size_t length) {
    if (!destination || !source) return destination;
    uint8_t *out = (uint8_t *)destination; const uint8_t *in = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) out[i] = in[i];
    return destination;
}
void *memmove(void *destination, const void *source, size_t length) {
    if (!destination || !source || destination == source) return destination;
    uint8_t *out = (uint8_t *)destination; const uint8_t *in = (const uint8_t *)source;
    if (out < in) for (size_t i = 0; i < length; ++i) out[i] = in[i];
    else for (size_t i = length; i; --i) out[i - 1] = in[i - 1];
    return destination;
}
void *memset(void *destination, int value, size_t length) {
    if (!destination) return destination;
    uint8_t *out = (uint8_t *)destination;
    for (size_t i = 0; i < length; ++i) out[i] = (uint8_t)value;
    return destination;
}
int memcmp(const void *left, const void *right, size_t length) {
    const uint8_t *a = (const uint8_t *)left, *b = (const uint8_t *)right;
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    for (size_t i = 0; i < length; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
size_t strlen(const char *text) { size_t n = 0; while (text && text[n]) ++n; return n; }
int strcmp(const char *left, const char *right) {
    size_t i = 0; if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (left[i] && left[i] == right[i]) ++i;
    return (unsigned char)left[i] == (unsigned char)right[i] ? 0 :
           ((unsigned char)left[i] < (unsigned char)right[i] ? -1 : 1);
}
int strncmp(const char *left, const char *right, size_t length) {
    if (!length) return 0;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) return (unsigned char)left[i] < (unsigned char)right[i] ? -1 : 1;
        if (!left[i]) return 0;
    }
    return 0;
}
char *strchr(const char *text, int value) {
    if (!text) return 0;
    for (;;) { if ((unsigned char)*text == (unsigned char)value) return (char *)text; if (!*text) return 0; ++text; }
}

typedef struct { uint32_t magic; uint32_t used; size_t size; } rix_alloc_header_t;
#define RIX_ALLOC_MAGIC 0x52495841u
static size_t align_up(size_t value) { return (value + 15u) & ~(size_t)15u; }
void *malloc(size_t size) {
    if (!size || size > (size_t)-1 - sizeof(rix_alloc_header_t)) { errno = RIX_ENOMEM; return 0; }
    size_t total = align_up(sizeof(rix_alloc_header_t) + size);
    void *memory = sbrk((ptrdiff_t)total);
    if (memory == (void *)-1) { errno = RIX_ENOMEM; return 0; }
    rix_alloc_header_t *header = (rix_alloc_header_t *)memory;
    header->magic = RIX_ALLOC_MAGIC; header->used = 1; header->size = size;
    return header + 1;
}
void *calloc(size_t count, size_t size) {
    if (count && size > (size_t)-1 / count) { errno = RIX_ENOMEM; return 0; }
    void *pointer = malloc(count * size); if (pointer) memset(pointer, 0, count * size); return pointer;
}
void free(void *pointer) {
    if (!pointer) return;
    rix_alloc_header_t *header = ((rix_alloc_header_t *)pointer) - 1;
    if (header->magic == RIX_ALLOC_MAGIC) header->used = 0;
}
void *realloc(void *pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return 0; }
    rix_alloc_header_t *header = ((rix_alloc_header_t *)pointer) - 1;
    if (header->magic == RIX_ALLOC_MAGIC && header->used && header->size >= size) { header->size = size; return pointer; }
    void *replacement = malloc(size);
    if (!replacement) return 0;
    if (header->magic == RIX_ALLOC_MAGIC && header->used) memcpy(replacement, pointer, header->size < size ? header->size : size);
    free(pointer); return replacement;
}
