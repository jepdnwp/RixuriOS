#pragma once
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
void *memchr(const void *source, int value, size_t length);
int memcmp(const void *left, const void *right, size_t length);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t length);
char *strchr(const char *text, int value);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t length);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t length);
char *strdup(const char *text);
char *strndup(const char *text, size_t length);
char *strerror(int error);
int strerror_r(int error, char *buffer, size_t capacity);
