#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86_64/smp.h"
#include "kernel/arch/x86_64/acpi.h"

/* HW stubs: this TU exercises only the pure smp_build_map core. */
static size_t stub_cpu_count;
static acpi_cpu_info_t stub_cpus[4];
static uint32_t stub_lapic = 7;
size_t acpi_cpu_count(void) { return stub_cpu_count; }
const acpi_cpu_info_t *acpi_cpu(size_t index) {
    return index < stub_cpu_count ? &stub_cpus[index] : 0;
}
uint32_t lapic_id(void) { return stub_lapic; }

static acpi_cpu_info_t entry(uint32_t apic, uint8_t enabled, uint8_t x2apic) {
    acpi_cpu_info_t e = {0, enabled, x2apic, apic};
    return e;
}

int main(void) {
    smp_map_t map;
    acpi_cpu_info_t four[4] = {entry(0, 1, 0), entry(1, 1, 0),
                               entry(2, 1, 0), entry(3, 1, 0)};
    assert(smp_build_map(four, 4, 0, 1, &map) == 0);
    assert(map.count == 4 && map.online == 1 && map.bsp_index == 0);
    assert(map.cpu[0].is_bsp && map.cpu[0].state == SMP_CPU_ONLINE);
    assert(map.cpu[3].state == SMP_CPU_PRESENT && !map.cpu[3].is_bsp);

    assert(smp_build_map(four, 4, 2, 1, &map) == 0 && map.bsp_index == 2);
    assert(map.online == 1 && map.cpu[2].state == SMP_CPU_ONLINE);

    acpi_cpu_info_t mixed[3] = {entry(0, 0, 0), entry(1, 1, 0), entry(0x100, 1, 1)};
    assert(smp_build_map(mixed, 3, 0x100, 1, &map) == 0);
    assert(map.count == 3 && map.bsp_index == 2 && map.online == 1);
    assert(map.cpu[0].state == SMP_CPU_PRESENT && !map.cpu[0].enabled);

    assert(smp_build_map(0, 0, 9, 1, &map) == 0);
    assert(map.count == 1 && map.online == 1 && map.bsp_index == 0);
    assert(map.cpu[0].apic_id == 9 && map.cpu[0].is_bsp);

    assert(smp_build_map(0, 0, 9, 0, &map) != 0);
    assert(smp_build_map(four, 4, 0, 1, 0) != 0);
    assert(smp_build_map(0, 3, 0, 1, &map) != 0);

    acpi_cpu_info_t many[70];
    for (size_t i = 0; i < 70; ++i) many[i] = entry((uint32_t)i, 1, 0);
    assert(smp_build_map(many, 70, 0, 1, &map) == 0 && map.count == 64);
    assert(smp_build_map(many, 70, 9999, 1, &map) != 0);

    stub_cpu_count = 0;
    assert(smp_discover() == 0);
    assert(smp_cpu_count() == 1 && smp_online_count() == 1);
    assert(smp_bsp_apic() == 7 && smp_bsp_index() == 0);
    assert(smp_cpu(0) && smp_cpu(0)->is_bsp && smp_cpu(1) == 0);
    return 0;
}
