#include "nvme.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include <stddef.h>

#define NVME_MAX_CONTROLLERS 8
#define NVME_CLASS 0x01
#define NVME_SUBCLASS 0x08
#define NVME_CAP 0x00
#define NVME_VS 0x08
#define NVME_INTMS 0x0C
#define NVME_CC 0x14
#define NVME_CSTS 0x1C
#define NVME_NSSR 0x20
#define NVME_AQA 0x24
#define NVME_ASQ 0x28
#define NVME_ACQ 0x30
#define NVME_CC_EN (1u<<0)
#define NVME_CC_CSS_SHIFT 4
#define NVME_CC_MPS_SHIFT 7
#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_IOCQES_SHIFT 20
#define NVME_CSTS_RDY (1u<<0)
#define NVME_CSTS_CFS (1u<<1)
#define NVME_BAR_MEM_MASK 0xFFFFFFF0u
#define NVME_ADMIN_DEPTH 16u
#define NVME_ADMIN_OP_IDENTIFY 0x06u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u
#define NVME_IDENTIFY_SIZE 4096u
#define NVME_POLL_LIMIT 1000000u

#define NVME_SQ_ENTRY_BYTES 64u
#define NVME_CQ_ENTRY_BYTES 16u

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint32_t rsvd2;
    uint32_t rsvd3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

typedef struct {
    uint32_t result;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} nvme_completion_t;

static rix_nvme_controller_t controllers[NVME_MAX_CONTROLLERS];
static size_t count;

static volatile uint32_t *map_regs(uint64_t bar) {
    uint64_t page = bar & ~0xFFFULL;
    if (vmm_map_page(page, page, RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return NULL;
    return (volatile uint32_t *)(uintptr_t)page;
}

static uint64_t bar_address(const rix_pci_device_t *d) {
    uint32_t lo = d->bars[0];
    if (lo & 1u) return 0;
    uint64_t base = (uint64_t)(lo & NVME_BAR_MEM_MASK);
    if (((lo >> 1) & 3u) == 2u) base |= (uint64_t)d->bars[1] << 32;
    return base;
}

static void zero_page(uint64_t phys) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)phys;
    for (size_t i = 0; i < 4096; ++i) p[i] = 0;
}

static int wait_ready(volatile uint32_t *r, int ready) {
    for (uint32_t i = 0; i < NVME_POLL_LIMIT; ++i) {
        uint32_t csts = r[NVME_CSTS / 4];
        if ((csts & NVME_CSTS_RDY) != 0u == ready) return (csts & NVME_CSTS_CFS) ? -2 : 0;
    }
    return -1;
}

static int controller_reset(volatile uint32_t *r) {
    uint32_t cc = r[NVME_CC / 4];
    if (cc & NVME_CC_EN) {
        r[NVME_CC / 4] = cc & ~NVME_CC_EN;
        if (wait_ready(r, 0) != 0) return -1;
    }
    if (r[NVME_CSTS / 4] & NVME_CSTS_CFS) return -2;
    return 0;
}

static int admin_setup(rix_nvme_controller_t *c, volatile uint32_t *r) {
    uint32_t min_mps = (uint32_t)((c->cap >> 48) & 0xFu);
    if (min_mps != 0u) return -1;

    uint64_t sq = pmm_alloc_page();
    uint64_t cq = pmm_alloc_page();
    if (!sq || !cq || (sq & 0xFFFULL) || (cq & 0xFFFULL)) {
        if (sq) pmm_free_page(sq);
        if (cq) pmm_free_page(cq);
        return -2;
    }
    zero_page(sq);
    zero_page(cq);

    r[NVME_AQA / 4] = ((NVME_ADMIN_DEPTH - 1u) << 16) | (NVME_ADMIN_DEPTH - 1u);
    *(volatile uint64_t *)((uint8_t *)r + NVME_ASQ) = sq;
    *(volatile uint64_t *)((uint8_t *)r + NVME_ACQ) = cq;

    uint32_t cc = r[NVME_CC / 4];
    cc &= ~((0xFu << NVME_CC_MPS_SHIFT) | (0x7u << NVME_CC_CSS_SHIFT) |
            (0xFu << NVME_CC_IOSQES_SHIFT) | (0xFu << NVME_CC_IOCQES_SHIFT) | NVME_CC_EN);
    cc |= (0u << NVME_CC_MPS_SHIFT) | (0u << NVME_CC_CSS_SHIFT) |
          (6u << NVME_CC_IOSQES_SHIFT) | (4u << NVME_CC_IOCQES_SHIFT) | NVME_CC_EN;
    r[NVME_CC / 4] = cc;
    if (wait_ready(r, 1) != 0) {
        pmm_free_page(sq);
        pmm_free_page(cq);
        return -3;
    }

    c->admin_sq_phys = sq;
    c->admin_cq_phys = cq;
    c->admin_ready = 1;
    c->cc = r[NVME_CC / 4];
    c->csts = r[NVME_CSTS / 4];
    return 0;
}

static int admin_identify(rix_nvme_controller_t *c, volatile uint32_t *r) {
    if (!c->admin_ready) return -1;
    uint64_t data = pmm_alloc_page();
    if (!data) return -2;
    zero_page(data);

    volatile nvme_command_t *sq = (volatile nvme_command_t *)(uintptr_t)c->admin_sq_phys;
    volatile nvme_completion_t *cq = (volatile nvme_completion_t *)(uintptr_t)c->admin_cq_phys;
    sq[0].cdw0 = NVME_ADMIN_OP_IDENTIFY | (1u << 16);
    sq[0].nsid = 0;
    sq[0].mptr = 0;
    sq[0].prp1 = data;
    sq[0].prp2 = 0;
    sq[0].cdw10 = NVME_IDENTIFY_CNS_CONTROLLER;
    for (uint32_t i = 0; i < 4; ++i) sq[0].cdw13 = 0;

    /* SQ0/CQ0 are the only outstanding admin command. Doorbell stride is 4*(2^DSTRD). */
    uint64_t stride = 4ULL << c->dstrd;
    volatile uint32_t *sq_tail_db = (volatile uint32_t *)((uint8_t *)r + 0x1000 + stride * 0);
    *sq_tail_db = 1;

    for (uint32_t i = 0; i < NVME_POLL_LIMIT; ++i) {
        uint16_t status = cq[0].status;
        if (status & 1u) {
            if ((status >> 1) & 0x7FFu) {
                pmm_free_page(data);
                return -3;
            }
            for (size_t j = 0; j < 5; ++j) c->serial[j] = ((volatile uint32_t *)(uintptr_t)(data + 4 + j * 4))[0];
            for (size_t j = 0; j < 10; ++j) c->model[j] = ((volatile uint32_t *)(uintptr_t)(data + 24 + j * 4))[0];
            for (size_t j = 0; j < 2; ++j) c->firmware[j] = ((volatile uint32_t *)(uintptr_t)(data + 64 + j * 4))[0];
            c->nn = ((volatile uint32_t *)(uintptr_t)(data + 516))[0];
            c->identify_valid = 1;
            pmm_free_page(data);
            return 0;
        }
    }
    pmm_free_page(data);
    return -4;
}

int nvme_init(void) {
    count = 0;
    for (size_t i = 0; i < pci_device_count() && count < NVME_MAX_CONTROLLERS; ++i) {
        const rix_pci_device_t *d = pci_device(i);
        if (!d || d->class_code != NVME_CLASS || d->subclass != NVME_SUBCLASS) continue;
        uint64_t bar = bar_address(d);
        if (!bar) continue;
        volatile uint32_t *r = map_regs(bar);
        if (!r) continue;

        volatile uint64_t *cap = (volatile uint64_t *)(uintptr_t)((uint8_t *)r + NVME_CAP);
        uint64_t capv = *cap;
        uint32_t vs = r[NVME_VS / 4];
        uint32_t cc = r[NVME_CC / 4];
        uint32_t csts = r[NVME_CSTS / 4];
        rix_nvme_controller_t *c = &controllers[count++];
        c->bus = d->bus; c->device = d->device; c->function = d->function;
        c->bar0 = bar; c->cap = capv; c->version = vs; c->cc = cc; c->csts = csts;
        c->mqes = (uint16_t)(capv & 0xffffu);
        c->dstrd = (uint8_t)((capv >> 32) & 0xfu);
        c->css = (uint8_t)((capv >> 37) & 0xffu);
        c->admin_ready = 0; c->identify_valid = 0;
        if (controller_reset(r) == 0 && admin_setup(c, r) == 0) (void)admin_identify(c, r);
    }
    return 0;
}

size_t nvme_controller_count(void) { return count; }
const rix_nvme_controller_t *nvme_controller(size_t index) { return index < count ? &controllers[index] : NULL; }

int nvme_identify_controller(size_t index) {
    if (index >= count) return -1;
    volatile uint32_t *r = map_regs(controllers[index].bar0);
    if (!r) return -2;
    return admin_identify(&controllers[index], r);
}
