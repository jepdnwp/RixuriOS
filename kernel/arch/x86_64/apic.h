#pragma once

#include <stdint.h>

#define RIXURI_LAPIC_SPURIOUS_VECTOR 0xFFu

int lapic_init(void);
uint32_t lapic_id(void);
void lapic_eoi(void);
void lapic_write(uint32_t offset, uint32_t value);
uint32_t lapic_read(uint32_t offset);
