#pragma once
#include <stdint.h>

#define RIXURI_PTE_PRESENT  (1ULL << 0)
#define RIXURI_PTE_WRITE    (1ULL << 1)
#define RIXURI_PTE_USER     (1ULL << 2)
#define RIXURI_PTE_PWT      (1ULL << 3)
#define RIXURI_PTE_PCD      (1ULL << 4)
/* Software-owned leaf mapping. Cleared on borrowed identity mappings. */
#define RIXURI_PTE_OWNED   (1ULL << 9)
#define RIXURI_PTE_NX       (1ULL << 63)

void vmm_early_init(void);
uint64_t vmm_kernel_pml4(void);
uint64_t vmm_current_pml4(void);
void *vmm_phys_ptr(uint64_t physical_address);
void vmm_switch_pml4(uint64_t pml4_phys);
/* Software view of the loaded CR3. Written by every CR3 writer
 * (vmm_switch_pml4, process_activate's load_cr3, and the x86_enter_user
 * family in user_entry.S); read by translators and fault diagnostics. */
extern uint64_t current_pml4_phys;
/* Sync the software tracker after a raw CR3 load performed outside
 * vmm_switch_pml4 (e.g. the isolated switch in process_activate).
 * uaccess/vmm queries walk vmm_current_pml4(), so HW and SW must agree. */
void vmm_track_pml4(uint64_t pml4_phys);
int vmm_map_page_in_pml4(uint64_t pml4_phys,uint64_t virtual_address,uint64_t physical_address,uint64_t flags);
int vmm_unmap_page_in_pml4(uint64_t pml4_phys,uint64_t virtual_address);
int vmm_map_page(uint64_t virtual_address,uint64_t physical_address,uint64_t flags);
void vmm_unmap_page(uint64_t virtual_address);
/* Supervisor-uncached MMIO mapping for device drivers. vmm_map_page targets
 * the CURRENT address space, which is a user root inside syscalls/IRQs, so
 * it must never be used for device MMIO reached from those contexts (the
 * entries would pollute user tables and trip the validator). This maps into
 * the KERNEL PML4 and borrows the slot into the current tree when it
 * differs, so the mapping is usable immediately under any CR3. Identity
 * VA==phys is kept when the range sits in a kernel-shared PML4 slot;
 * otherwise a window VA in the shared MMIO slot is assigned (deduped per
 * phys range). Returns the usable VA (phys intra-page offset preserved),
 * or 0 on failure. Single-CPU only (no lock); teardown never unmaps. */
uint64_t vmm_map_mmio(uint64_t physical_address,uint64_t size);
uint64_t vmm_translate(uint64_t virtual_address);
uint64_t vmm_query_flags(uint64_t virtual_address);
/* CR3-switch diagnostics (CR3-independent: use phys window, never current CR3).
 * validate: 0=CR3-safe (non-zero, 4K aligned, in range, reserved bits clear,
 * PML4[0] kernel identity present). walk: 0=mapped, fills entries/phys/flags,
 * handles 1 GiB/2 MiB PS leaves. Log helpers are serial-only by design: on
 * physical UC-VRAM consoles each screen line costs a full repaint, so detail
 * stays on COM1 while the caller prints short verdict lines via kernel_log
 * (screen+serial). */
int vmm_validate_pml4(uint64_t pml4_phys);
int vmm_walk_in_pml4(uint64_t pml4_phys,uint64_t va,uint64_t *out_pml4e,uint64_t *out_pdpte,uint64_t *out_pde,uint64_t *out_pte,uint64_t *out_phys,uint64_t *out_flags);
void vmm_log_walk(uint64_t pml4_phys,uint64_t va,const char *label);
void vmm_log_pml4_compare(uint64_t a_phys,uint64_t b_phys);
/* Unconditional dump of PML4 slots [first,last) for kernel-vs-target
 * comparison (even when both are zero). Serial-only. */
void vmm_log_pml4_range(uint64_t a_phys,uint64_t b_phys,unsigned first,unsigned last);
