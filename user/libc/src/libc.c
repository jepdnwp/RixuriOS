#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include "stdio.h"
#include "dirent.h"
#include "fcntl.h"
#include <stdint.h>
#include <stdarg.h>

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


int isdigit(int value) { return value >= '0' && value <= '9'; }
int islower(int value) { return value >= 'a' && value <= 'z'; }
int isupper(int value) { return value >= 'A' && value <= 'Z'; }
int isalpha(int value) { return islower(value) || isupper(value); }
int isalnum(int value) { return isalpha(value) || isdigit(value); }
int isspace(int value) { return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f'; }
int tolower(int value) { return isupper(value) ? value + ('a' - 'A') : value; }
int toupper(int value) { return islower(value) ? value - ('a' - 'A') : value; }

static void format_char(char *buffer, size_t capacity, size_t *written, char value) {
    if (capacity && *written + 1u < capacity) buffer[*written] = value;
    ++*written;
}
static void format_text(char *buffer, size_t capacity, size_t *written, const char *text) {
    if (!text) text = "(null)";
    while (*text) format_char(buffer, capacity, written, *text++);
}
static void format_unsigned(char *buffer, size_t capacity, size_t *written, uint64_t value, unsigned base) {
    char digits[sizeof(uint64_t) * 2u + 1u]; size_t count = 0;
    do { unsigned digit = (unsigned)(value % base); digits[count++] = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u); value /= base; } while (value);
    while (count) format_char(buffer, capacity, written, digits[--count]);
}
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments) {
    size_t written = 0;
    if (!format || (!buffer && capacity)) return -1;
    while (*format) {
        if (*format != '%') { format_char(buffer, capacity, &written, *format++); continue; }
        ++format; if (!*format) break;
        if (*format == '%') { format_char(buffer, capacity, &written, '%'); ++format; continue; }
        int long_value = 0; if (*format == 'l') { long_value = 1; ++format; }
        if (*format == 'z') { long_value = 1; ++format; }
        switch (*format++) {
        case 'c': format_char(buffer, capacity, &written, (char)va_arg(arguments, int)); break;
        case 's': format_text(buffer, capacity, &written, va_arg(arguments, const char *)); break;
        case 'd': {
            int64_t value = long_value ? va_arg(arguments, long) : va_arg(arguments, int);
            if (value < 0) { format_char(buffer, capacity, &written, '-'); format_unsigned(buffer, capacity, &written, (uint64_t)(-(value + 1)) + 1u, 10); }
            else format_unsigned(buffer, capacity, &written, (uint64_t)value, 10);
            break;
        }
        case 'u': format_unsigned(buffer, capacity, &written, long_value ? va_arg(arguments, unsigned long) : va_arg(arguments, unsigned), 10); break;
        case 'x': format_unsigned(buffer, capacity, &written, long_value ? va_arg(arguments, unsigned long) : va_arg(arguments, unsigned), 16); break;
        case 'p': format_text(buffer, capacity, &written, "0x"); format_unsigned(buffer, capacity, &written, (uint64_t)(uintptr_t)va_arg(arguments, void *), 16); break;
        default: format_char(buffer, capacity, &written, '?'); break;
        }
    }
    if (capacity) buffer[written < capacity ? written : capacity - 1u] = 0;
    return (int)written;
}
int snprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list arguments; va_start(arguments, format); int result = vsnprintf(buffer, capacity, format, arguments); va_end(arguments); return result;
}
int puts(const char *text) {
    size_t length = strlen(text); if (write(1, text, length) < 0 || write(1, "\n", 1) < 0) return -1; return (int)(length + 1u);
}
int putchar(int value) { char character = (char)value; return write(1, &character, 1) < 0 ? -1 : (unsigned char)character; }

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode || !mode[0]) { errno = RIX_EINVAL; return 0; }
    uint32_t flags = mode[0] == 'r' ? O_RDONLY : (mode[0] == 'a' ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY | O_CREAT | O_TRUNC);
    if (mode[1] == '+') flags = (flags & ~(O_RDONLY | O_WRONLY)) | O_RDWR;
    int fd = open(path, flags, 0666u);
    if (fd < 0) return 0;
    FILE *stream = malloc(sizeof(*stream));
    if (!stream) { (void)close(fd); return 0; }
    stream->fd = fd; return stream;
}
int fclose(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1; } int rc = close(stream->fd); free(stream); return rc; }
size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream || (!buffer && size && count)) { errno = RIX_EINVAL; return 0; }
    if (!size || count > (size_t)-1 / size) return 0;
    rix_ssize_t result = read(stream->fd, buffer, size * count);
    return result < 0 ? 0 : (size_t)result / size;
}
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!stream || (!buffer && size && count)) { errno = RIX_EINVAL; return 0; }
    if (!size || count > (size_t)-1 / size) return 0;
    rix_ssize_t result = write(stream->fd, buffer, size * count);
    return result < 0 ? 0 : (size_t)result / size;
}
int fflush(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1; } return 0; }
int fseek(FILE *stream, long offset, int whence) { if (!stream) { errno = RIX_EINVAL; return -1; } return lseek(stream->fd, (off_t)offset, whence) < 0 ? -1 : 0; }
long ftell(FILE *stream) { if (!stream) { errno = RIX_EINVAL; return -1L; } return (long)lseek(stream->fd, 0, SEEK_CUR); }
void rewind(FILE *stream) { if (stream) (void)fseek(stream, 0, SEEK_SET); }
int fgetc(FILE *stream) { unsigned char value; return (!stream || read(stream->fd, &value, 1) != 1) ? EOF : (int)value; }
int fputc(int value, FILE *stream) { unsigned char character=(unsigned char)value; return (!stream || write(stream->fd, &character, 1) != 1) ? EOF : (int)character; }
char *fgets(char *buffer, int capacity, FILE *stream) {
    if (!buffer || capacity <= 0 || !stream) { errno=RIX_EINVAL; return 0; }
    int index=0; while (index+1 < capacity) { int value=fgetc(stream); if (value==EOF) break; buffer[index++]=(char)value; if (value=='\n') break; }
    if (!index) return 0;
    buffer[index]=0; return buffer;
}
int fputs(const char *text, FILE *stream) { if (!text || !stream) { errno=RIX_EINVAL; return EOF; } size_t length=strlen(text); return write(stream->fd,text,length)==(rix_ssize_t)length ? 0 : EOF; }
int setvbuf(FILE *stream, char *buffer, int mode, size_t size) { (void)buffer; (void)size; if (!stream || mode < _IONBF || mode > _IOLBF) { errno=RIX_EINVAL; return -1; } return 0; }
void setbuf(FILE *stream, char *buffer) { (void)setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, buffer ? BUFSIZ : 0); }

DIR *opendir(const char *path) {
    if (!path) { errno = RIX_EINVAL; return 0; }
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    DIR *directory = malloc(sizeof(*directory));
    if (!directory) { (void)close(fd); return 0; }
    directory->fd = fd; directory->index = 0; directory->count = 0;
    if (getdents(fd, (rix_dirent_t *)directory->entries, 16u, &directory->count) < 0) { (void)close(fd); free(directory); return 0; }
    return directory;
}
struct dirent *readdir(DIR *directory) {
    if (!directory) { errno = RIX_EINVAL; return 0; }
    if (directory->index >= directory->count) {
        directory->index = 0; directory->count = 0;
        size_t fetched = 0;
        if (getdents(directory->fd, (rix_dirent_t *)directory->entries, 16u, &fetched) < 0 || !fetched) return 0;
        directory->count = fetched;
    }
    return &directory->entries[directory->index++];
}
int closedir(DIR *directory) { if (!directory) { errno = RIX_EINVAL; return -1; } int rc = close(directory->fd); free(directory); return rc; }
