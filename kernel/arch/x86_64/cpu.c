#include "cpu.h"

void x86_cpuid(uint32_t leaf, uint32_t subleaf, struct x86_cpuid *out) {
    if (!out) return;
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(subleaf));
    out->eax = a; out->ebx = b; out->ecx = c; out->edx = d;
}

uint64_t x86_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void x86_wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value, hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

uint64_t x86_cpu_features(void) {
    struct x86_cpuid r;
    x86_cpuid(0, 0, &r);
    uint32_t max_leaf = r.eax;
    x86_cpuid(1, 0, &r);
    uint64_t features = 0;
    if (r.ecx & (1u << 21)) features |= 1ULL << 0; /* x2APIC */
    if (r.ecx & (1u << 24)) features |= 1ULL << 1; /* TSC deadline */
    if (r.ecx & (1u << 17)) features |= 1ULL << 2; /* PCID */
    if (r.ecx & (1u << 28)) features |= 1ULL << 3; /* AVX */
    if (r.edx & (1u << 4))  features |= 1ULL << 4; /* TSC */
    if (r.edx & (1u << 9))  features |= 1ULL << 5; /* APIC */
    if (r.edx & (1u << 25)) features |= 1ULL << 6; /* SSE */
    if (r.edx & (1u << 26)) features |= 1ULL << 7; /* SSE2 */
    if (r.ecx & (1u << 31)) features |= 1ULL << 8; /* hypervisor */
    if (r.ecx & (1u << 30)) features |= 1ULL << 9; /* RDRAND */
    if (max_leaf >= 7) {
        x86_cpuid(7, 0, &r);
        if (r.ebx & (1u << 18)) features |= 1ULL << 10; /* RDSEED */
    }
    return features;
}

int x86_random_u64(uint64_t *out) {
    if (!out) return -1;
    uint64_t features = x86_cpu_features();
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        unsigned char ok;
        if (features & (1ULL << 10)) {
            __asm__ volatile ("rdseed %0; setc %1" : "=r"(*out), "=qm"(ok) : : "cc");
            if (ok) return 0;
        }
        if (features & (1ULL << 9)) {
            __asm__ volatile ("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok) : : "cc");
            if (ok) return 0;
        }
    }
    return -1;
}
