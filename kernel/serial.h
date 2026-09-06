#pragma once
#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_write(const char *s);
void serial_write_n(const char *s, size_t length);
void serial_write_hex(uint64_t value);
void serial_write_dec(uint64_t value);
void panic(const char *reason);
