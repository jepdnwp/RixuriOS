#include "apic.h"
#include "cpu.h"
#include "../../mm/vmm.h"

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_MASK 0x000FFFFFFFFFF000ULL
#define APIC_ENABLE (1ULL << 11)
#define APIC_REG_ID 0x020
#define APIC_REG_EOI 0x0B0
#define APIC_REG_SVR 0x0F0
#define APIC_REG_LVT_LINT0 0x350
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
void lapic_enable_pic_extint(void) { lapic_write(APIC_REG_LVT_LINT0, 7u << 8); }

/* Delivery clears within a handful of reads on healthy hardware; the
 * bound only caps pathological MMIO stalls (slow virtualized reads must
 * not wedge the BSP: worst case fails closed to the caller timeout). */
#define APIC_IPI_WAIT_ITERS 50000ULL
static int ipi_wait_idle(void) {
    for (uint64_t i = 0; i < APIC_IPI_WAIT_ITERS; ++i) {
        if (!(lapic_read(APIC_REG_ICR_LOW) & APIC_ICR_DELIVERY_STATUS)) return 0;
    }
    return -1;
}

static int ipi_send(uint32_t apic_id, uint32_t low) {
    lapic_write(APIC_REG_ICR_HIGH, apic_id << 24);
    lapic_write(APIC_REG_ICR_LOW, low);
    return ipi_wait_idle();
}

int lapic_send_init(uint32_t apic_id, int assert_level) {
    uint32_t low = APIC_DELIVERY_INIT | APIC_TRIGGER_LEVEL;
    if (assert_level) low |= APIC_LEVEL_ASSERT;
    return ipi_send(apic_id, low);
}

int lapic_send_sipi(uint32_t apic_id, uint8_t vector) {
    return ipi_send(apic_id, APIC_DELIVERY_SIPI | vector);
}
