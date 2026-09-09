#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct { int fd; } FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int snprintf(char *buffer, size_t capacity, const char *format, ...);
int puts(const char *text);
int putchar(int value);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
