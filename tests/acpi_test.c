#include "kernel/arch/x86_64/acpi.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    char sig[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} fake_rsdp_t;
typedef struct {
    char sig[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oemtable[8];
    uint32_t oemrev;
    uint32_t creator;
    uint32_t creatorrev;
} fake_sdt_t;
#pragma pack(pop)

static uint8_t cksum(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *b++);
    return s;
}

static void fix_sdt(fake_sdt_t *t, size_t total) {
    t->length = (uint32_t)total;
    t->checksum = 0;
    t->checksum = (uint8_t)(0u - cksum(t, total));
}

/* 24-entry XSDT: the APIC table sits at index 20, past the old 16-entry
 * cutoff that used to reject large physical-firmware tables. */
static uint8_t xsdt_area[36 + 24 * 8];
static uint8_t madt_area[64];
static uint8_t facp_area[64];
static fake_rsdp_t rsdp;

static void build_valid(void) {
    fake_sdt_t *xsdt = (fake_sdt_t *)xsdt_area;
    fake_sdt_t *madt = (fake_sdt_t *)madt_area;
    uint64_t *entries;
    memcpy(xsdt->sig, "XSDT", 4);
    xsdt->revision = 1;
    memcpy(xsdt->oemid, "RIXURI", 6);
    entries = (uint64_t *)(xsdt_area + 36);
    for (size_t i = 0; i < 20; ++i) entries[i] = (uint64_t)(uintptr_t)facp_area;
    entries[20] = (uint64_t)(uintptr_t)madt_area;
    for (size_t i = 21; i < 24; ++i) entries[i] = (uint64_t)(uintptr_t)facp_area;
    fix_sdt(xsdt, sizeof(xsdt_area));
    memcpy(facp_area, "FACP", 4);
    fix_sdt((fake_sdt_t *)facp_area, 36);
    memcpy(madt->sig, "APIC", 4);
    madt->revision = 3;
    {
        uint8_t *p = madt_area + 44;
        /* Local APIC entry. */
        p[0] = 0; p[1] = 8; p[2] = 0; p[3] = 0;
        p[4] = 1; p[5] = 0; p[6] = 0; p[7] = 0;
        /* I/O APIC entry. */
        p[8] = 1; p[9] = 12; p[10] = 2; p[11] = 0;
        p[12] = 0xf0; p[13] = 0xfe; p[14] = 0x00; p[15] = 0x00;
        p[16] = 0; p[17] = 0; p[18] = 0; p[19] = 0;
        /* Local APIC address + flags. */
        madt_area[36] = 0x00; madt_area[37] = 0x00;
        madt_area[38] = 0xfe; madt_area[39] = 0xf0;
        madt_area[40] = 1; madt_area[41] = 0;
        madt_area[42] = 0; madt_area[43] = 0;
        fix_sdt(madt, 64);
    }
    memcpy(rsdp.sig, "RSD PTR ", 8);
    rsdp.revision = 2;
    rsdp.rsdt = 0;
    rsdp.length = 36;
    rsdp.xsdt = (uint64_t)(uintptr_t)xsdt_area;
    rsdp.checksum = 0;
    rsdp.ext_checksum = 0;
    rsdp.checksum = (uint8_t)(0u - cksum(&rsdp, 20));
    rsdp.ext_checksum = (uint8_t)(0u - cksum(&rsdp, 36));
}

int main(void) {
    build_valid();
    assert(acpi_init((uint64_t)(uintptr_t)&rsdp) == 0);
    assert(acpi_cpu_count() == 1);
    assert(acpi_ioapic_count() == 1);
    assert(acpi_cpu(0) != 0 && acpi_cpu(0)->apic_id == 0);
    assert(acpi_ioapic(0) != 0 && acpi_ioapic(0)->id == 2);
    assert(acpi_cpu(1) == 0 && acpi_ioapic(1) == 0);

    assert(acpi_init(0) == ACPI_ERR_NO_RSDP);

    {
        fake_rsdp_t bad = rsdp;
        bad.sig[0] = 'X';
        bad.checksum = (uint8_t)(0u - cksum(&bad, 20));
        assert(acpi_init((uint64_t)(uintptr_t)&bad) == ACPI_ERR_BAD_SIG);
    }
    {
        fake_rsdp_t bad = rsdp;
        bad.ext_checksum ^= 0xffu;
        assert(acpi_init((uint64_t)(uintptr_t)&bad) == ACPI_ERR_BAD_CHECKSUM);
    }
    {
        /* XSDT with no APIC entry. */
        uint8_t area[36 + 8];
        fake_sdt_t *xsdt = (fake_sdt_t *)area;
        fake_rsdp_t root = rsdp;
        memcpy(xsdt->sig, "XSDT", 4);
        *(uint64_t *)(area + 36) = (uint64_t)(uintptr_t)facp_area;
        fix_sdt(xsdt, sizeof(area));
        root.xsdt = (uint64_t)(uintptr_t)area;
        root.checksum = 0;
        root.ext_checksum = 0;
        root.checksum = (uint8_t)(0u - cksum(&root, 20));
        root.ext_checksum = (uint8_t)(0u - cksum(&root, 36));
        assert(acpi_init((uint64_t)(uintptr_t)&root) == ACPI_ERR_NO_MADT);
    }
    {
        /* MADT truncated inside an entry header. */
        uint8_t area[48];
        fake_sdt_t *madt = (fake_sdt_t *)area;
        uint8_t xarea[36 + 8];
        fake_sdt_t *xsdt = (fake_sdt_t *)xarea;
        fake_rsdp_t root = rsdp;
        memcpy(madt->sig, "APIC", 4);
        memset(area + 36, 0, 12);
        area[44] = 0;
        area[45] = 1;
        fix_sdt(madt, sizeof(area));
        memcpy(xsdt->sig, "XSDT", 4);
        *(uint64_t *)(xarea + 36) = (uint64_t)(uintptr_t)area;
        fix_sdt(xsdt, sizeof(xarea));
        root.xsdt = (uint64_t)(uintptr_t)xarea;
        root.checksum = 0;
        root.ext_checksum = 0;
        root.checksum = (uint8_t)(0u - cksum(&root, 20));
        root.ext_checksum = (uint8_t)(0u - cksum(&root, 36));
        assert(acpi_init((uint64_t)(uintptr_t)&root) == ACPI_ERR_MADT_ENTRY);
    }
    assert(acpi_error_string(0) != 0);
    assert(acpi_error_string(ACPI_ERR_NO_RSDP) != 0);
    assert(acpi_error_string(ACPI_ERR_BAD_SIG) != 0);
    assert(acpi_error_string(ACPI_ERR_BAD_LENGTH) != 0);
    assert(acpi_error_string(ACPI_ERR_BAD_CHECKSUM) != 0);
    assert(acpi_error_string(ACPI_ERR_NO_MADT) != 0);
    assert(acpi_error_string(ACPI_ERR_BAD_MADT) != 0);
    assert(acpi_error_string(ACPI_ERR_MADT_ENTRY) != 0);
    assert(acpi_error_string(999) != 0);
    return 0;
}
