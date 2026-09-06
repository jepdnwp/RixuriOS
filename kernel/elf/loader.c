#include "loader.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define USER_VA_MIN 0x0000008000000000ULL
#define USER_VA_MAX (1ULL << 47)

static int range(uint64_t a, uint64_t n, uint64_t lim) {
    return a <= lim && n <= lim - a;
}

static void zero_page(uint8_t *p) {
    for (size_t i = 0; i < PAGE_SIZE; ++i) p[i] = 0;
}

static int power_of_two(uint64_t v) {
    return v && (v & (v - 1ULL)) == 0;
}

int elf_load_image(const void *file, uint64_t file_size,
                   rix_address_space_t *as, rix_elf_image_t *out) {
    if (!file || !as || !out) return -1;
    rix_elf64_ehdr_t header;
    if (elf64_validate(file, (size_t)file_size, &header) != 0) return -1;

    uint64_t lo = UINT64_MAX;
    uint64_t hi = 0;
    uint64_t exec_lo = UINT64_MAX;
    uint64_t exec_hi = 0;
    int loads = 0;

    /* Validate every PT_LOAD before changing the address space. */
    for (uint16_t i = 0; i < header.phnum; ++i) {
        rix_elf64_phdr_t ph;
        if (elf64_program_header(file, (size_t)file_size, i, &ph) != 0) return -1;
        if (ph.type != RIX_PT_LOAD) continue;
        ++loads;
        if (ph.memsz == 0 || ph.filesz > ph.memsz) return -1;
        if (ph.vaddr < USER_VA_MIN || !range(ph.vaddr, ph.memsz, USER_VA_MAX)) return -1;
        if (!range(ph.offset, ph.filesz, file_size)) return -1;
        if (ph.align > 1) {
            if (!power_of_two(ph.align)) return -1;
            if ((ph.vaddr % ph.align) != (ph.offset % ph.align)) return -1;
        }
        uint64_t end = ph.vaddr + ph.memsz;
        if (end < ph.vaddr) return -1;
        if (ph.vaddr < lo) lo = ph.vaddr;
        if (end > hi) hi = end;
        if (ph.flags & RIX_PF_X) {
            if (ph.vaddr < exec_lo) exec_lo = ph.vaddr;
            if (end > exec_hi) exec_hi = end;
        }
    }
    if (!loads || lo == UINT64_MAX || exec_lo == UINT64_MAX ||
        header.entry < exec_lo || header.entry >= exec_hi) return -1;

    for (uint16_t i = 0; i < header.phnum; ++i) {
        rix_elf64_phdr_t ph;
        if (elf64_program_header(file, (size_t)file_size, i, &ph) != 0) return -1;
        if (ph.type != RIX_PT_LOAD) continue;
        uint64_t end = ph.vaddr + ph.memsz;
        uint64_t first = ph.vaddr & ~(PAGE_SIZE - 1ULL);
        uint64_t last = (end + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);
        uint64_t wanted = RIXURI_PTE_PRESENT | RIXURI_PTE_USER | RIXURI_PTE_NX;
        if (ph.flags & RIX_PF_W) wanted |= RIXURI_PTE_WRITE;
        if (ph.flags & RIX_PF_X) wanted &= ~RIXURI_PTE_NX;

        for (uint64_t va = first; va < last; va += PAGE_SIZE) {
            uint64_t old = address_space_query_flags(as, va);
            int owned = (old & (RIXURI_PTE_PRESENT | RIXURI_PTE_USER | RIXURI_PTE_OWNED)) ==
                        (RIXURI_PTE_PRESENT | RIXURI_PTE_USER | RIXURI_PTE_OWNED);
            uint64_t pa = owned ? (address_space_translate(as, va) & ~(PAGE_SIZE - 1ULL)) : 0;
            if (old & RIXURI_PTE_PRESENT) {
                if (!owned || address_space_update_flags(as, va, wanted) != 0) return -1;
            } else {
                pa = pmm_alloc_page();
                if (!pa) return -1;
                zero_page((uint8_t *)(uintptr_t)pa);
                if (address_space_map(as, va, pa, wanted) != 0) {
                    pmm_free_page(pa);
                    return -1;
                }
            }

            uint64_t segment_start = ph.vaddr > va ? ph.vaddr : va;
            uint64_t segment_end = end < va + PAGE_SIZE ? end : va + PAGE_SIZE;
            if (segment_end > segment_start) {
                uint64_t segment_offset = segment_start - ph.vaddr;
                if (segment_offset < ph.filesz) {
                    uint64_t n = ph.filesz - segment_offset;
                    if (n > segment_end - segment_start) n = segment_end - segment_start;
                    uint8_t *dst = (uint8_t *)(uintptr_t)pa + (segment_start - va);
                    const uint8_t *src = (const uint8_t *)file + ph.offset + segment_offset;
                    for (uint64_t k = 0; k < n; ++k) dst[k] = src[k];
                }
            }
        }
    }
    out->entry = header.entry;
    out->image_lo = lo;
    out->image_hi = hi;
    return 0;
}
