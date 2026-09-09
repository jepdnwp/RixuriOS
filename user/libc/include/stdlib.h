#pragma once
#include <stddef.h>
#include <stdint.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
int atoi(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
int abs(int value);
long labs(long value);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
void srand(unsigned seed);
int rand(void);
uint32_t arc4random(void);
