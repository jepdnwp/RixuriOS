#include "smp.h"
#include "apic.h"
#include <stddef.h>
#include <stdint.h>

static smp_map_t smp_map;

int smp_build_map(const acpi_cpu_info_t *entries, size_t n,
                  uint32_t bsp_apic_id, int bsp_fallback, smp_map_t *out) {
    if (!out) return -1;
    for (size_t i = 0; i < SMP_MAX_CPUS; ++i) {
        out->cpu[i].apic_id = 0;
        out->cpu[i].enabled = 0;
        out->cpu[i].is_bsp = 0;
        out->cpu[i].x2apic = 0;
        out->cpu[i].state = SMP_CPU_ABSENT;
        out->cpu[i].stack_phys = 0;
    }
    out->count = 0;
    out->online = 0;
    out->bsp_apic = bsp_apic_id;
    out->bsp_index = -1;
    if (n && !entries) return -1;
    size_t usable = n < SMP_MAX_CPUS ? n : SMP_MAX_CPUS;
    for (size_t i = 0; i < usable; ++i) {
        out->cpu[i].apic_id = entries[i].apic_id;
        out->cpu[i].enabled = entries[i].enabled ? 1u : 0u;
        out->cpu[i].x2apic = entries[i].x2apic ? 1u : 0u;
        out->cpu[i].state = SMP_CPU_PRESENT;
        if (entries[i].apic_id == bsp_apic_id && out->bsp_index < 0) {
            out->cpu[i].is_bsp = 1;
            out->cpu[i].state = SMP_CPU_ONLINE;
            out->bsp_index = (int)i;
            out->online = 1;
        }
    }
    out->count = usable;
    if (out->bsp_index < 0) {
        if (!bsp_fallback) return -1;
        if (usable >= SMP_MAX_CPUS) return -2;
        out->cpu[usable].apic_id = bsp_apic_id;
        out->cpu[usable].enabled = 1;
        out->cpu[usable].is_bsp = 1;
        out->cpu[usable].state = SMP_CPU_ONLINE;
        out->bsp_index = (int)usable;
        out->online = 1;
        out->count = usable + 1;
    }
    return 0;
}

int smp_discover(void) {
    acpi_cpu_info_t list[SMP_MAX_CPUS];
    size_t n = acpi_cpu_count();
    if (n > SMP_MAX_CPUS) n = SMP_MAX_CPUS;
    for (size_t i = 0; i < n; ++i) {
        const acpi_cpu_info_t *entry = acpi_cpu(i);
        if (!entry) return -1;
        list[i] = *entry;
    }
    return smp_build_map(list, n, lapic_id(), 1, &smp_map);
}

size_t smp_cpu_count(void) { return smp_map.count; }
size_t smp_online_count(void) { return smp_map.online; }
uint32_t smp_bsp_apic(void) { return smp_map.bsp_apic; }
int smp_bsp_index(void) { return smp_map.bsp_index; }
const smp_cpu_t *smp_cpu(size_t index) {
    if (index >= SMP_MAX_CPUS || index >= smp_map.count) return 0;
    return &smp_map.cpu[index];
}
