#pragma once

#include <stdint.h>

struct x86_cpuid {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

void x86_cpuid(uint32_t leaf, uint32_t subleaf, struct x86_cpuid *out);
uint64_t x86_rdmsr(uint32_t msr);
void x86_wrmsr(uint32_t msr, uint64_t value);
uint64_t x86_cpu_features(void);
