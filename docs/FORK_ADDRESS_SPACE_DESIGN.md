# Fork and Address-Space Design

## Problem statement

The current `fork -> address_space_clone -> address_space_create` path manipulates page-table pages through physical addresses cast as kernel virtual pointers. This relies on a broad identity mapping remaining valid while the current CR3 changes between parent, kernel and child address spaces. QEMU evidence shows that the child reaches the fork boundary, but the kernel can fault while reading the bootstrap PML4/PDPT during child address-space creation. The design below removes that implicit dependency.

## Design goals

The implementation must preserve the x86_64 ABI, keep parent and child user mappings independent, retain inherited file descriptors, return `0` in the child and the child PID in the parent, and provide complete rollback on allocation failure. Page-table ownership must be explicit: a process owns its PML4, its user-only lower-level tables, and its user leaf pages; kernel mappings are shared and never freed by process teardown.

## 1. Kernel virtual mapping window

Introduce a permanent kernel page-table window, for example `KMAP_BASE`, covering a bounded set of physical pages used for page-table manipulation. The window is mapped in every process address space through the shared upper-half kernel mappings. The helpers are:

```c
void *pt_kmap(uint64_t physical_page);
void pt_kunmap(void *kernel_address);
uint64_t pt_read(uint64_t physical_page, unsigned index);
void pt_write(uint64_t physical_page, unsigned index, uint64_t value);
```

`pt_kmap` must reject unaligned or out-of-range physical addresses. The initial implementation may use a reserved, permanently mapped window sized for the maximum simultaneous page-table pages; it must not call `vmm_switch_pml4` merely to inspect a table. All address-space and VMM code must use this interface instead of `(void *)(uintptr_t)physical_address`.

The window’s own page-table pages are reserved before PMM allocation begins and are marked borrowed, not process-owned. `pmm_free_page` must reject them in debug builds and return an error in the checked API.

## 2. Explicit address-space object

Extend the address-space object with ownership metadata:

```c
typedef struct {
    uint64_t pml4_phys;
    uint64_t user_root_phys;
    uint64_t generation;
    uint32_t flags;
} rix_address_space_t;
```

The PML4 entry range used for user mappings is owned by the process. The kernel-half entries point to shared tables and are copied as borrowed references. `address_space_destroy` must free only user-owned tables and leaves; it must never recursively free a borrowed kernel table.

## 3. `address_space_create`

The operation becomes a transaction:

1. Allocate and zero a PML4 page.
2. Install borrowed kernel-half entries by reading the kernel template through `pt_kmap`.
3. Allocate a user-root table and install it in the user PML4 slot.
4. Commit the object only after every required table is present.
5. On any failure, release only the pages recorded in the transaction journal.

The function must not read `kt[0]` through the current CR3. It reads the kernel template as physical memory through `pt_kmap`, and it must validate every entry’s present bit, physical alignment and allowed flags.

## 4. `address_space_clone`

Clone is a two-phase operation:

**Prepare.** Walk the parent’s user tree through `pt_kmap`, allocate child tables and leaf pages, and record every allocation in a journal. The parent remains active and unchanged. The child’s kernel mappings are installed from the shared template, not copied from the parent’s potentially active lower identity mapping.

**Commit.** Publish the child address-space pointer in the process table only after the complete user tree and all inherited descriptors have succeeded. If a leaf copy or descriptor clone fails, unwind the journal in reverse order.

The walk must reject huge-page user entries until explicit huge-page clone support exists. It must detect cycles, invalid physical addresses, non-user entries in the user range and arithmetic overflow in virtual-address calculation.

## 5. CR3 and syscall sequencing

The syscall handler must not switch to the kernel template CR3 as a workaround. Entry already executes on the process kernel stack and kernel mappings are shared. The sequence is:

```text
validate syscall frame
  -> process_fork(parent address-space, frame)
  -> create child task with copied 64-bit user context
  -> restore parent’s syscall result in the existing frame
  -> scheduler later activates child CR3
```

`process_activate` is the only routine allowed to change `current_pml4_phys` for a task switch. Address-space construction uses the kernel mapping window and therefore works regardless of which process CR3 is active.

The child context must copy all 64-bit general registers, `RIP`, `RFLAGS` and `RSP`; only `RAX` is changed to zero. The parent frame retains the child PID. The `iretq` frame is built only after the child task has been selected and its CR3 activated.

## 6. Kernel stack ownership

A process kernel stack must be a physically allocated, page-aligned multi-page object with a guard page or a checked high-water mark. The size must be chosen from measured worst-case syscall depth, not from the size of one page. `execve` argument storage must not be allocated as large automatic arrays on that stack. Use process-indexed kernel scratch storage or heap allocations with bounded lifetime, and release them before returning from the syscall.

The stack object must carry its physical base, mapped kernel address and page count. TSS `RSP0` receives the mapped top address. Teardown returns exactly the owned pages and never returns shared bootstrap/VMM pages to PMM.

## 7. Pipe and descriptor semantics

`process_fork` clones descriptors only after the child address space has been prepared, or it records descriptor retains in the same rollback journal. A failed fork must close every retained pipe endpoint exactly once. The child inherits read/write endpoint counts; `execve` preserves descriptors unless a future close-on-exec flag is added. This design keeps pipe EOF semantics independent from page-table construction.

## 8. Implementation phases

| Phase | Change | Required evidence |
|---|---|---|
| A | Add `pt_kmap` window and physical validation helpers. Convert `address_space_create`. | Strict kernel build; table read/write unit harness. |
| B | Add ownership metadata and allocation journal. Convert destroy/clone. | Allocation-failure injection; no leaked or double-freed pages. |
| C | Move exec argument vectors off the kernel stack and enlarge/guard process stacks. | Deep `execve` argument test and stack watermark. |
| D | Restore ordinary full-register child context without syscall-side CR3 workaround. | Minimal second-fork child `_exit(7)` QEMU test. |
| E | Run `fork -> execve -> wait`, pipe EOF, xargs pipeline and failure-status cases. | Real QEMU PASS log with no exception or timeout. |

## Acceptance criteria

The change is not complete when the machine merely boots or when xargs prints output. It is complete only when the following all pass in one clean QEMU image: direct second-fork child exit, child `execve` followed by `wait`, pipe writer/reader EOF, `/bin/echo one two | /usr/bin/xargs /bin/echo`, xargs child failure propagation, and the existing find/sed/test utility cases. The harness must fail on a missing prompt, exception line, timeout or non-zero expected status.
