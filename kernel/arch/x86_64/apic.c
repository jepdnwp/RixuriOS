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
#define APIC_BASE_X2APIC (1ULL << 10)
#define X2APIC_MSR_BASE 0x800u
#define X2APIC_MSR_ID (X2APIC_MSR_BASE + 2u)
#define X2APIC_MSR_EOI (X2APIC_MSR_BASE + 0xBu)
#define X2APIC_MSR_SVR (X2APIC_MSR_BASE + 0xFu)
#define X2APIC_MSR_LVT_LINT0 (X2APIC_MSR_BASE + 0x35u)
#define X2APIC_MSR_ICR (X2APIC_MSR_BASE + 0x30u)
#define PTE_FLAGS (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX)
#define LAPIC_VIRTUAL_BASE 0xFFFF8000FEE00000ULL

static volatile uint32_t *lapic_mmio;
static uint8_t x2apic_mode;

void lapic_write(uint32_t offset, uint32_t value) {
    if (x2apic_mode) {
        if (offset == APIC_REG_EOI) x86_wrmsr(X2APIC_MSR_EOI, 0);
        else if (offset == APIC_REG_SVR) x86_wrmsr(X2APIC_MSR_SVR, value);
        else if (offset == APIC_REG_LVT_LINT0) x86_wrmsr(X2APIC_MSR_LVT_LINT0, value);
        return;
    }
    if (!lapic_mmio || (offset & 0xF) || offset >= 0x400) return;
    lapic_mmio[offset / 4] = value;
}

uint32_t lapic_read(uint32_t offset) {
    if (x2apic_mode) {
        if (offset == APIC_REG_ID) return (uint32_t)x86_rdmsr(X2APIC_MSR_ID);
        return 0;
    }
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
    x2apic_mode = (base_msr & APIC_BASE_X2APIC) != 0;
    if (x2apic_mode) {
        x86_wrmsr(X2APIC_MSR_SVR, APIC_SVR_ENABLE | RIXURI_LAPIC_SPURIOUS_VECTOR);
        x86_wrmsr(X2APIC_MSR_EOI, 0);
        return 0;
    }
    if (vmm_map_page(LAPIC_VIRTUAL_BASE, base,
                     PTE_FLAGS | RIXURI_PTE_PWT | RIXURI_PTE_PCD) != 0) return -1;
    lapic_mmio = (volatile uint32_t *)(uintptr_t)LAPIC_VIRTUAL_BASE;
    lapic_write(APIC_REG_SVR, APIC_SVR_ENABLE | RIXURI_LAPIC_SPURIOUS_VECTOR);
    lapic_eoi();
    return 0;
}

uint32_t lapic_id(void) {
    /* x2APIC MSR 0x802 returns the complete 32-bit ID. The MMIO LAPIC ID
     * register stores the legacy 8-bit value in bits 31:24. */
    return x2apic_mode ? lapic_read(APIC_REG_ID) : (lapic_read(APIC_REG_ID) >> 24);
}
void lapic_eoi(void) { lapic_write(APIC_REG_EOI, 0); }
void lapic_enable_pic_extint(void) { lapic_write(APIC_REG_LVT_LINT0, 7u << 8); }

/* Delivery clears within a handful of reads on healthy hardware; the
 * bound only caps pathological MMIO stalls (slow virtualized reads must
 * not wedge the BSP: worst case fails closed to the caller timeout). */
#define APIC_IPI_WAIT_ITERS 50000ULL
static int ipi_wait_idle(void) {
    if (x2apic_mode) {
        for (uint64_t i = 0; i < APIC_IPI_WAIT_ITERS; ++i)
            if (!(x86_rdmsr(X2APIC_MSR_ICR) & APIC_ICR_DELIVERY_STATUS)) return 0;
        return -1;
    }
    for (uint64_t i = 0; i < APIC_IPI_WAIT_ITERS; ++i) {
        if (!(lapic_read(APIC_REG_ICR_LOW) & APIC_ICR_DELIVERY_STATUS)) return 0;
    }
    return -1;
}

static int ipi_send(uint32_t apic_id, uint32_t low) {
    if (x2apic_mode) {
        x86_wrmsr(X2APIC_MSR_ICR, ((uint64_t)apic_id << 32) | low);
        return ipi_wait_idle();
    }
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

int lapic_send_ipi(uint32_t apic_id, uint8_t vector) {
    return ipi_send(apic_id, (uint32_t)vector);
}

void lapic_ap_enable(void) {
    lapic_write(APIC_REG_SVR, APIC_SVR_ENABLE | RIXURI_LAPIC_SPURIOUS_VECTOR);
    lapic_eoi();
}
