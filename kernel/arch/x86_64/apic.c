#include "apic.h"
#include "cpu.h"
#include "../../mm/vmm.h"

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_MASK 0x000FFFFFFFFFF000ULL
#define APIC_ENABLE (1ULL << 11)
#define APIC_REG_ID 0x020
#define APIC_REG_EOI 0x0B0
#define APIC_REG_SVR 0x0F0
#define APIC_SVR_ENABLE (1u << 8)
#define PTE_FLAGS (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX)
#define LAPIC_VIRTUAL_BASE 0xFFFF8000FEE00000ULL

static volatile uint32_t *lapic_mmio;

void lapic_write(uint32_t offset, uint32_t value) {
    if (!lapic_mmio || (offset & 0xF) || offset >= 0x400) return;
    lapic_mmio[offset / 4] = value;
}

uint32_t lapic_read(uint32_t offset) {
    if (!lapic_mmio || (offset & 0xF) || offset >= 0x400) return 0;
    return lapic_mmio[offset / 4];
}

int lapic_init(void) {
    uint64_t base_msr = x86_rdmsr(IA32_APIC_BASE_MSR);
    uint64_t base = base_msr & APIC_BASE_MASK;
    if (!base) return -1;
    if (!(base_msr & APIC_ENABLE)) {
        x86_wrmsr(IA32_APIC_BASE_MSR, base_msr | APIC_ENABLE);
    }
    if (vmm_map_page(LAPIC_VIRTUAL_BASE, base, PTE_FLAGS) != 0) return -1;
    lapic_mmio = (volatile uint32_t *)(uintptr_t)LAPIC_VIRTUAL_BASE;
    lapic_write(APIC_REG_SVR, APIC_SVR_ENABLE | RIXURI_LAPIC_SPURIOUS_VECTOR);
    lapic_eoi();
    return 0;
}

uint32_t lapic_id(void) { return lapic_read(APIC_REG_ID) >> 24; }
void lapic_eoi(void) { lapic_write(APIC_REG_EOI, 0); }
