#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct { int fd; unsigned char *buffer; size_t buffer_size; size_t buffer_pos; size_t buffer_len; int mode; int writing; int owns_buffer; } FILE;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IONBF 0
#define _IOFBF 1
#define _IOLBF 2
#define BUFSIZ 1024u
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
int fgetc(FILE *stream);
int fputc(int value, FILE *stream);
char *fgets(char *buffer, int capacity, FILE *stream);
int fputs(const char *text, FILE *stream);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
