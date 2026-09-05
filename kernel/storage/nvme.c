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
#define NVME_CC 0x14
#define NVME_CSTS 0x1C
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
#define NVME_POLL_LIMIT 1000000u

typedef struct { uint32_t cdw0,nsid,rsvd2,rsvd3; uint64_t mptr,prp1,prp2; uint32_t cdw10,cdw11,cdw12,cdw13,cdw14,cdw15; } nvme_command_t;
typedef struct { uint32_t result,rsvd; uint16_t sq_head,sq_id,command_id,status; } nvme_completion_t;
static rix_nvme_controller_t controllers[NVME_MAX_CONTROLLERS];static size_t count;
static volatile uint32_t *map_regs(uint64_t bar){uint64_t page=bar&~0xFFFULL;if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return NULL;return(volatile uint32_t*)(uintptr_t)page;}
static uint64_t bar_address(const rix_pci_device_t*d){uint32_t lo=d->bars[0];if(lo&1u)return 0;uint64_t base=(uint64_t)(lo&NVME_BAR_MEM_MASK);if(((lo>>1)&3u)==2u)base|=(uint64_t)d->bars[1]<<32;return base;}
static void zero_page(uint64_t phys){volatile uint8_t*p=(volatile uint8_t*)(uintptr_t)phys;for(size_t i=0;i<4096;i++)p[i]=0;}
static int wait_ready(volatile uint32_t*r,int ready){for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint32_t csts=r[NVME_CSTS/4];if(((csts&NVME_CSTS_RDY)!=0)==(ready!=0))return(csts&NVME_CSTS_CFS)?-2:0;}return -1;}
static int controller_reset(volatile uint32_t*r){uint32_t cc=r[NVME_CC/4];if(cc&NVME_CC_EN){r[NVME_CC/4]=cc&~NVME_CC_EN;if(wait_ready(r,0)!=0)return -1;}return(r[NVME_CSTS/4]&NVME_CSTS_CFS)?-2:0;}
static int admin_setup(rix_nvme_controller_t*c,volatile uint32_t*r){uint32_t min_mps=(uint32_t)((c->cap>>48)&0xfu);if(min_mps>0)return -1;uint64_t sq=pmm_alloc_page(),cq=pmm_alloc_page();if(!sq||!cq){if(sq)pmm_free_page(sq);if(cq)pmm_free_page(cq);return -2;}zero_page(sq);zero_page(cq);r[NVME_AQA/4]=((NVME_ADMIN_DEPTH-1u)<<16)|(NVME_ADMIN_DEPTH-1u);*(volatile uint64_t*)((uint8_t*)r+NVME_ASQ)=sq;*(volatile uint64_t*)((uint8_t*)r+NVME_ACQ)=cq;uint32_t cc=r[NVME_CC/4];cc&=~((0xfu<<NVME_CC_MPS_SHIFT)|(7u<<NVME_CC_CSS_SHIFT)|(0xfu<<NVME_CC_IOSQES_SHIFT)|(0xfu<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN);cc|=(6u<<NVME_CC_IOSQES_SHIFT)|(4u<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN;r[NVME_CC/4]=cc;if(wait_ready(r,1)!=0){pmm_free_page(sq);pmm_free_page(cq);return -3;}c->admin_sq_phys=sq;c->admin_cq_phys=cq;c->admin_ready=1;c->admin_sq_tail=0;c->admin_cq_head=0;c->admin_cq_phase=1;c->admin_cid=0;c->cc=r[NVME_CC/4];c->csts=r[NVME_CSTS/4];return 0;}
static int admin_identify(rix_nvme_controller_t*c,volatile uint32_t*r){if(!c->admin_ready)return -1;uint64_t data=pmm_alloc_page();if(!data)return -2;zero_page(data);volatile nvme_command_t*sq=(volatile nvme_command_t*)(uintptr_t)c->admin_sq_phys;volatile nvme_completion_t*cq=(volatile nvme_completion_t*)(uintptr_t)c->admin_cq_phys;uint16_t cid=++c->admin_cid;if(!cid)cid=++c->admin_cid;uint16_t slot=c->admin_sq_tail%NVME_ADMIN_DEPTH;sq[slot].cdw0=NVME_ADMIN_OP_IDENTIFY|((uint32_t)cid<<16);sq[slot].nsid=0;sq[slot].mptr=0;sq[slot].prp1=data;sq[slot].prp2=0;sq[slot].cdw10=NVME_IDENTIFY_CNS_CONTROLLER;c->admin_sq_tail=(uint16_t)((c->admin_sq_tail+1)%NVME_ADMIN_DEPTH);uint64_t stride=4ULL<<c->dstrd;volatile uint32_t*sq_tail_db=(volatile uint32_t*)((uint8_t*)r+0x1000+stride*0);*sq_tail_db=c->admin_sq_tail;
 for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint16_t status=cq[c->admin_cq_head].status;if((status&1u)==c->admin_cq_phase){if(cq[c->admin_cq_head].command_id!=cid){pmm_free_page(data);return -3;}uint16_t sc=(uint16_t)(status>>1);c->admin_cq_head=(uint16_t)((c->admin_cq_head+1)%NVME_ADMIN_DEPTH);if(c->admin_cq_head==0)c->admin_cq_phase^=1u;volatile uint32_t*cq_head_db=(volatile uint32_t*)((uint8_t*)r+0x1000+stride*1);*cq_head_db=c->admin_cq_head;if(sc){pmm_free_page(data);return -4;}for(size_t j=0;j<5;j++)c->serial[j]=((volatile uint32_t*)(uintptr_t)(data+4+j*4))[0];for(size_t j=0;j<10;j++)c->model[j]=((volatile uint32_t*)(uintptr_t)(data+24+j*4))[0];for(size_t j=0;j<2;j++)c->firmware[j]=((volatile uint32_t*)(uintptr_t)(data+64+j*4))[0];c->nn=((volatile uint32_t*)(uintptr_t)(data+516))[0];c->identify_valid=1;pmm_free_page(data);return 0;}}
pmm_free_page(data);return -5;}
int nvme_init(void){count=0;for(size_t i=0;i<pci_device_count()&&count<NVME_MAX_CONTROLLERS;i++){const rix_pci_device_t*d=pci_device(i);if(!d||d->class_code!=NVME_CLASS||d->subclass!=NVME_SUBCLASS)continue;uint64_t bar=bar_address(d);if(!bar)continue;volatile uint32_t*r=map_regs(bar);if(!r)continue;uint64_t capv=*(volatile uint64_t*)((uint8_t*)r+NVME_CAP);rix_nvme_controller_t*c=&controllers[count++];c->bus=d->bus;c->device=d->device;c->function=d->function;c->bar0=bar;c->cap=capv;c->version=r[NVME_VS/4];c->cc=r[NVME_CC/4];c->csts=r[NVME_CSTS/4];c->mqes=(uint16_t)(capv&0xffffu);c->dstrd=(uint8_t)((capv>>32)&0xfu);c->css=(uint8_t)((capv>>37)&0xffu);c->admin_ready=0;c->identify_valid=0;if(controller_reset(r)==0&&admin_setup(c,r)==0)(void)admin_identify(c,r);}return 0;}
size_t nvme_controller_count(void){return count;}const rix_nvme_controller_t*nvme_controller(size_t i){return i<count?&controllers[i]:NULL;}
int nvme_identify_controller(size_t i){if(i>=count)return -1;volatile uint32_t*r=map_regs(controllers[i].bar0);return r?admin_identify(&controllers[i],r):-2;}
