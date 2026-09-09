#pragma once
#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_console_enable(void);
void serial_write(const char *s);
void serial_write_n(const char *s, size_t length);
void serial_write_hex(uint64_t value);
void serial_write_dec(uint64_t value);
int serial_read_byte(uint8_t *byte);
void panic(const char *reason);
/* Spin until the UART transmit FIFO is fully empty (bounded). Call after
 * crash-forensic lines so a subsequent reset cannot strand bytes that were
 * accepted into the FIFO but never reached the wire. */
void serial_drain(void);
