#include "kernel.h"

#define COM1 0x3F8u

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0,%1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80); outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) { }
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    if (!s) return;
    while (*s) serial_putc(*s++);
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) serial_putc(digits[(value >> shift) & 0xFULL]);
}

void serial_write_dec(uint64_t value) {
    char buf[21]; size_t i = sizeof(buf);
    if (value == 0) { serial_putc('0'); return; }
    while (value) { buf[--i] = (char)('0' + value % 10ULL); value /= 10ULL; }
    serial_write(&buf[i]);
}

void panic(const char *reason) {
    serial_write("RixuriOS PANIC: ");
    serial_write(reason ? reason : "unknown");
    serial_write("\r\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
