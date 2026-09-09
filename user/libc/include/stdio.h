#pragma once
#include <stddef.h>
#include <stdarg.h>
int vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments);
int snprintf(char *buffer, size_t capacity, const char *format, ...);
int puts(const char *text);
int putchar(int value);
