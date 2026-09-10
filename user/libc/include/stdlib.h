#pragma once
#include <stddef.h>
#include <stdint.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
_Noreturn void exit(int status);
_Noreturn void _Exit(int status);
_Noreturn void abort(void);
/* Up to 16 handlers, LIFO. No locking; register before forking. */
int atexit(void (*function)(void));
/* No command interpreter exists; always fails with ENOSYS. */
int system(const char *command);
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
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
int clearenv(void);
#ifndef RIX_HOST_TEST
extern char **environ;
#endif
void srand(unsigned seed);
int rand(void);
uint32_t arc4random(void);
