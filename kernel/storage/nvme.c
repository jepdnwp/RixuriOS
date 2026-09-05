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
#define NVME_IDENTIFY_CNS_NAMESPACE 0x00u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u
#define NVME_POLL_LIMIT 1000000u

typedef struct { uint32_t cdw0,nsid,rsvd2,rsvd3; uint64_t mptr,prp1,prp2; uint32_t cdw10,cdw11,cdw12,cdw13,cdw14,cdw15; } nvme_command_t;
typedef struct { uint32_t result,rsvd; uint16_t sq_head,sq_id,command_id,status; } nvme_completion_t;
static rix_nvme_controller_t controllers[NVME_MAX_CONTROLLERS]; static size_t count;
static volatile uint32_t *map_regs(uint64_t bar){uint64_t page=bar&~0xFFFULL;if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return NULL;return(volatile uint32_t*)(uintptr_t)page;}
static uint64_t bar_address(const rix_pci_device_t*d){uint32_t lo=d->bars[0];if(lo&1u)return 0;uint64_t base=(uint64_t)(lo&NVME_BAR_MEM_MASK);if(((lo>>1)&3u)==2u)base|=(uint64_t)d->bars[1]<<32;return base;}
static void zero_page(uint64_t phys){volatile uint8_t*p=(volatile uint8_t*)(uintptr_t)phys;for(size_t i=0;i<4096;i++)p[i]=0;}
static void copy_trimmed(char*dst,size_t cap,const volatile uint8_t*src,size_t len){if(!dst||cap<1)return;size_t n=len<cap-1?len:cap-1;while(n&&src[n-1]==' ')n--;for(size_t i=0;i<n;i++)dst[i]=(char)src[i];dst[n]='\0';}
static int wait_ready(volatile uint32_t*r,int ready){for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint32_t csts=r[NVME_CSTS/4];if(((csts&NVME_CSTS_RDY)!=0)==(ready!=0))return(csts&NVME_CSTS_CFS)?-2:0;}return -1;}
static int controller_reset(volatile uint32_t*r){uint32_t cc=r[NVME_CC/4];if(cc&NVME_CC_EN){r[NVME_CC/4]=cc&~NVME_CC_EN;if(wait_ready(r,0)!=0)return -1;}return(r[NVME_CSTS/4]&NVME_CSTS_CFS)?-2:0;}
static int admin_setup(rix_nvme_controller_t*c,volatile uint32_t*r){uint32_t min_mps=(uint32_t)((c->cap>>48)&0xfu);if(min_mps>0)return -1;uint64_t sq=pmm_alloc_page(),cq=pmm_alloc_page();if(!sq||!cq){if(sq)pmm_free_page(sq);if(cq)pmm_free_page(cq);return -2;}zero_page(sq);zero_page(cq);r[NVME_AQA/4]=((NVME_ADMIN_DEPTH-1u)<<16)|(NVME_ADMIN_DEPTH-1u);*(volatile uint64_t*)((uint8_t*)r+NVME_ASQ)=sq;*(volatile uint64_t*)((uint8_t*)r+NVME_ACQ)=cq;uint32_t cc=r[NVME_CC/4];cc&=~((0xfu<<NVME_CC_MPS_SHIFT)|(7u<<NVME_CC_CSS_SHIFT)|(0xfu<<NVME_CC_IOSQES_SHIFT)|(0xfu<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN);cc|=(6u<<NVME_CC_IOSQES_SHIFT)|(4u<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN;r[NVME_CC/4]=cc;if(wait_ready(r,1)!=0){pmm_free_page(sq);pmm_free_page(cq);return -3;}c->admin_sq_phys=sq;c->admin_cq_phys=cq;c->admin_ready=1;c->admin_sq_tail=0;c->admin_cq_head=0;c->admin_cq_phase=1;c->admin_cid=0;c->cc=r[NVME_CC/4];c->csts=r[NVME_CSTS/4];return 0;}
static int admin_command(rix_nvme_controller_t*c,volatile uint32_t*r,uint32_t nsid,uint32_t cns,uint64_t data){if(!c->admin_ready||!data)return -1;volatile nvme_command_t*sq=(volatile nvme_command_t*)(uintptr_t)c->admin_sq_phys;volatile nvme_completion_t*cq=(volatile nvme_completion_t*)(uintptr_t)c->admin_cq_phys;uint16_t cid=++c->admin_cid;if(!cid)cid=++c->admin_cid;uint16_t slot=c->admin_sq_tail%NVME_ADMIN_DEPTH;for(size_t i=0;i<sizeof(nvme_command_t)/sizeof(uint32_t);i++)((volatile uint32_t*)&sq[slot])[i]=0;sq[slot].cdw0=NVME_ADMIN_OP_IDENTIFY|((uint32_t)cid<<16);sq[slot].nsid=nsid;sq[slot].prp1=data;sq[slot].cdw10=cns;c->admin_sq_tail=(uint16_t)((c->admin_sq_tail+1)%NVME_ADMIN_DEPTH);uint64_t stride=4ULL<<c->dstrd;*(volatile uint32_t*)((uint8_t*)r+0x1000)=c->admin_sq_tail;for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint16_t status=cq[c->admin_cq_head].status;if((status&1u)==c->admin_cq_phase){if(cq[c->admin_cq_head].command_id!=cid)return -3;uint16_t sc=(uint16_t)(status>>1);c->admin_cq_head=(uint16_t)((c->admin_cq_head+1)%NVME_ADMIN_DEPTH);if(c->admin_cq_head==0)c->admin_cq_phase^=1u;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride)=c->admin_cq_head;return sc?-4:0;}}return -5;}
static int admin_identify_controller(rix_nvme_controller_t*c,volatile uint32_t*r){uint64_t data=pmm_alloc_page();if(!data)return -1;zero_page(data);int rc=admin_command(c,r,0,NVME_IDENTIFY_CNS_CONTROLLER,data);if(rc==0){volatile uint8_t*b=(volatile uint8_t*)(uintptr_t)data;copy_trimmed(c->serial,sizeof(c->serial),b+4,20);copy_trimmed(c->model,sizeof(c->model),b+24,40);copy_trimmed(c->firmware,sizeof(c->firmware),b+64,8);c->nn=*(volatile uint32_t*)(uintptr_t)(data+516);c->identify_valid=1;}pmm_free_page(data);return rc;}
static uint32_t namespace_lba_size(volatile uint8_t*b){uint8_t flbas=b[26]&0xfu;size_t off=128u+(size_t)flbas*4u;uint8_t lbads=b[off+2];if(lbads<9||lbads>16)return 0;return 1u<<lbads;}
int nvme_identify_namespace(size_t index,uint32_t nsid){if(index>=count||nsid==0||nsid>controllers[index].nn)return -1;rix_nvme_controller_t*c=&controllers[index];volatile uint32_t*r=map_regs(c->bar0);if(!r||!c->admin_ready)return -2;uint64_t data=pmm_alloc_page();if(!data)return -3;zero_page(data);int rc=admin_command(c,r,nsid,NVME_IDENTIFY_CNS_NAMESPACE,data);if(rc==0){size_t slot=(size_t)nsid-1;if(slot>=RIX_NVME_MAX_NAMESPACES)rc=-4;else{volatile uint8_t*b=(volatile uint8_t*)(uintptr_t)data;rix_nvme_namespace_t*n=&c->namespaces[slot];n->nsid=nsid;n->size_lba=*(volatile uint64_t*)(uintptr_t)data;n->capacity_lba=*(volatile uint64_t*)(uintptr_t)(data+8);n->utilization_lba=*(volatile uint64_t*)(uintptr_t)(data+16);n->lba_format=b[26]&0xfu;n->lba_size=namespace_lba_size(b);n->used=(n->size_lba&&n->lba_size)?1:0;if(!n->used)rc=-5;}}pmm_free_page(data);return rc;}
int nvme_init(void){count=0;for(size_t i=0;i<pci_device_count()&&count<NVME_MAX_CONTROLLERS;i++){const rix_pci_device_t*d=pci_device(i);if(!d||d->class_code!=NVME_CLASS||d->subclass!=NVME_SUBCLASS)continue;uint64_t bar=bar_address(d);if(!bar)continue;volatile uint32_t*r=map_regs(bar);if(!r)continue;uint64_t capv=*(volatile uint64_t*)((uint8_t*)r+NVME_CAP);rix_nvme_controller_t*c=&controllers[count++];c->bus=d->bus;c->device=d->device;c->function=d->function;c->bar0=bar;c->cap=capv;c->version=r[NVME_VS/4];c->cc=r[NVME_CC/4];c->csts=r[NVME_CSTS/4];c->mqes=(uint16_t)(capv&0xffffu);c->dstrd=(uint8_t)((capv>>32)&0xfu);c->css=(uint8_t)((capv>>37)&0xffu);c->admin_ready=0;c->identify_valid=0;for(size_t n=0;n<RIX_NVME_MAX_NAMESPACES;n++)c->namespaces[n].used=0;if(controller_reset(r)==0&&admin_setup(c,r)==0){(void)admin_identify_controller(c,r);uint32_t limit=c->nn<RIX_NVME_MAX_NAMESPACES?c->nn:RIX_NVME_MAX_NAMESPACES;for(uint32_t nsid=1;nsid<=limit;nsid++)(void)nvme_identify_namespace(count-1,nsid);}}return 0;}
size_t nvme_controller_count(void){return count;}const rix_nvme_controller_t*nvme_controller(size_t i){return i<count?&controllers[i]:NULL;}int nvme_identify_controller(size_t i){if(i>=count)return -1;volatile uint32_t*r=map_regs(controllers[i].bar0);return r?admin_identify_controller(&controllers[i],r):-2;}
