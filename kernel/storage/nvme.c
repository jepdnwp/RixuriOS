#include "nvme.h"
#include "block.h"
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
#define NVME_IO_DEPTH 16u
#define NVME_ADMIN_OP_IDENTIFY 0x06u
#define NVME_ADMIN_OP_CREATE_CQ 0x05u
#define NVME_ADMIN_OP_CREATE_SQ 0x01u
#define NVME_IDENTIFY_CNS_NAMESPACE 0x00u
#define NVME_IDENTIFY_CNS_CONTROLLER 0x01u
#define NVME_IO_OP_FLUSH 0x00u
#define NVME_IO_OP_WRITE 0x01u
#define NVME_IO_OP_READ 0x02u
#define NVME_POLL_LIMIT 1000000u
#define NVME_PAGE_SIZE 4096u

typedef struct { uint32_t cdw0,nsid,rsvd2,rsvd3; uint64_t mptr,prp1,prp2; uint32_t cdw10,cdw11,cdw12,cdw13,cdw14,cdw15; } nvme_command_t;
typedef struct { uint32_t result,rsvd; uint16_t sq_head,sq_id,command_id,status; } nvme_completion_t;
static rix_nvme_controller_t controllers[NVME_MAX_CONTROLLERS]; static size_t count;
static volatile uint32_t *map_regs(uint64_t bar){uint64_t page=bar&~0xFFFULL;if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return NULL;return(volatile uint32_t*)(uintptr_t)page;}
static uint64_t bar_address(const rix_pci_device_t*d){uint32_t lo=d->bars[0];if(lo&1u)return 0;uint64_t base=(uint64_t)(lo&NVME_BAR_MEM_MASK);if(((lo>>1)&3u)==2u)base|=(uint64_t)d->bars[1]<<32;return base;}
static void zero_page(uint64_t phys){volatile uint8_t*p=(volatile uint8_t*)(uintptr_t)phys;for(size_t i=0;i<NVME_PAGE_SIZE;i++)p[i]=0;}
static void copy_trimmed(char*dst,size_t cap,const volatile uint8_t*src,size_t len){if(!dst||cap<1)return;size_t n=len<cap-1?len:cap-1;while(n&&src[n-1]==' ')n--;for(size_t i=0;i<n;i++)dst[i]=(src[i]>=32&&src[i]<127)?(char)src[i]:'?';dst[n]=0;}
static int wait_ready(volatile uint32_t*r,int ready){for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint32_t csts=r[NVME_CSTS/4];if(((csts&NVME_CSTS_RDY)!=0)==(ready!=0))return(csts&NVME_CSTS_CFS)?-2:0;}return -1;}
static int controller_reset(volatile uint32_t*r){uint32_t cc=r[NVME_CC/4];if(cc&NVME_CC_EN){r[NVME_CC/4]=cc&~NVME_CC_EN;if(wait_ready(r,0)!=0)return -1;}return(r[NVME_CSTS/4]&NVME_CSTS_CFS)?-2:0;}
static int admin_command(rix_nvme_controller_t*c,volatile uint32_t*r,uint32_t nsid,uint32_t opcode,uint32_t cdw10,uint32_t cdw11,uint64_t data){if(!c->admin_ready)return -1;volatile nvme_command_t*sq=(volatile nvme_command_t*)(uintptr_t)c->admin_sq_phys;volatile nvme_completion_t*cq=(volatile nvme_completion_t*)(uintptr_t)c->admin_cq_phys;uint16_t cid=++c->admin_cid;if(!cid)cid=++c->admin_cid;uint16_t slot=c->admin_sq_tail%NVME_ADMIN_DEPTH;for(size_t i=0;i<sizeof(nvme_command_t)/4;i++)((volatile uint32_t*)&sq[slot])[i]=0;sq[slot].cdw0=opcode|((uint32_t)cid<<16);sq[slot].nsid=nsid;sq[slot].prp1=data;sq[slot].cdw10=cdw10;sq[slot].cdw11=cdw11;c->admin_sq_tail=(uint16_t)((c->admin_sq_tail+1)%NVME_ADMIN_DEPTH);uint64_t stride=4ULL<<c->dstrd;*(volatile uint32_t*)((uint8_t*)r+0x1000)=c->admin_sq_tail;for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint16_t status=cq[c->admin_cq_head].status;if((status&1u)==c->admin_cq_phase){if(cq[c->admin_cq_head].command_id!=cid)return -3;uint16_t sc=(uint16_t)(status>>1);c->admin_cq_head=(uint16_t)((c->admin_cq_head+1)%NVME_ADMIN_DEPTH);if(c->admin_cq_head==0)c->admin_cq_phase^=1u;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride)=c->admin_cq_head;return sc?-4:0;}}return -5;}
static int admin_setup(rix_nvme_controller_t*c,volatile uint32_t*r){if(((c->cap>>48)&0xfu)>0)return -1;uint64_t sq=pmm_alloc_page(),cq=pmm_alloc_page();if(!sq||!cq){if(sq)pmm_free_page(sq);if(cq)pmm_free_page(cq);return -2;}zero_page(sq);zero_page(cq);r[NVME_AQA/4]=((NVME_ADMIN_DEPTH-1u)<<16)|(NVME_ADMIN_DEPTH-1u);*(volatile uint64_t*)((uint8_t*)r+NVME_ASQ)=sq;*(volatile uint64_t*)((uint8_t*)r+NVME_ACQ)=cq;uint32_t cc=r[NVME_CC/4];cc&=~((0xfu<<NVME_CC_MPS_SHIFT)|(7u<<NVME_CC_CSS_SHIFT)|(0xfu<<NVME_CC_IOSQES_SHIFT)|(0xfu<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN);cc|=(6u<<NVME_CC_IOSQES_SHIFT)|(4u<<NVME_CC_IOCQES_SHIFT)|NVME_CC_EN;r[NVME_CC/4]=cc;if(wait_ready(r,1)!=0){pmm_free_page(sq);pmm_free_page(cq);return -3;}c->admin_sq_phys=sq;c->admin_cq_phys=cq;c->admin_ready=1;c->admin_sq_tail=0;c->admin_cq_head=0;c->admin_cq_phase=1;c->admin_cid=0;return 0;}
static int create_io_queue(rix_nvme_controller_t*c,volatile uint32_t*r){uint64_t sq=pmm_alloc_page(),cq=pmm_alloc_page();if(!sq||!cq){if(sq)pmm_free_page(sq);if(cq)pmm_free_page(cq);return -1;}zero_page(sq);zero_page(cq);uint32_t qsz=NVME_IO_DEPTH-1;uint32_t cq10=qsz|(1u<<16);uint32_t cq11=0;int rc=admin_command(c,r,0,NVME_ADMIN_OP_CREATE_CQ,cq10,cq11,cq);if(rc!=0){pmm_free_page(sq);pmm_free_page(cq);return -2;}uint32_t sq10=qsz|(1u<<16);uint32_t sq11=1;rc=admin_command(c,r,0,NVME_ADMIN_OP_CREATE_SQ,sq10,sq11,sq);if(rc!=0){pmm_free_page(sq);pmm_free_page(cq);return -3;}c->io_sq_phys=sq;c->io_cq_phys=cq;c->io_queue_depth=NVME_IO_DEPTH;c->io_sq_tail=0;c->io_cq_head=0;c->io_cq_phase=1;c->io_cid=0;c->io_ready=1;return 0;}
static uint32_t namespace_lba_size(volatile uint8_t*b){uint8_t flbas=b[26]&0xfu;size_t off=128u+(size_t)flbas*4u;uint8_t lbads=b[off+2];if(lbads<9||lbads>16)return 0;return 1u<<lbads;}
int nvme_identify_controller(size_t index){if(index>=count)return -1;rix_nvme_controller_t*c=&controllers[index];volatile uint32_t*r=map_regs(c->bar0);if(!r||!c->admin_ready)return -2;uint64_t data=pmm_alloc_page();if(!data)return -3;zero_page(data);int rc=admin_command(c,r,0,NVME_ADMIN_OP_IDENTIFY,NVME_IDENTIFY_CNS_CONTROLLER,0,data);if(rc==0){volatile uint8_t*b=(volatile uint8_t*)(uintptr_t)data;copy_trimmed(c->serial,sizeof(c->serial),b+4,20);copy_trimmed(c->model,sizeof(c->model),b+24,40);copy_trimmed(c->firmware,sizeof(c->firmware),b+64,8);c->nn=*(volatile uint32_t*)(uintptr_t)(data+516);c->identify_valid=1;}pmm_free_page(data);return rc;}
int nvme_identify_namespace(size_t index,uint32_t nsid){if(index>=count||nsid==0||nsid>controllers[index].nn||nsid>RIX_NVME_MAX_NAMESPACES)return -1;rix_nvme_controller_t*c=&controllers[index];volatile uint32_t*r=map_regs(c->bar0);if(!r||!c->admin_ready)return -2;uint64_t data=pmm_alloc_page();if(!data)return -3;zero_page(data);int rc=admin_command(c,r,nsid,NVME_ADMIN_OP_IDENTIFY,NVME_IDENTIFY_CNS_NAMESPACE,0,data);if(rc==0){volatile uint8_t*b=(volatile uint8_t*)(uintptr_t)data;rix_nvme_namespace_t*n=&c->namespaces[nsid-1];n->nsid=nsid;n->size_lba=*(volatile uint64_t*)(uintptr_t)data;n->capacity_lba=*(volatile uint64_t*)(uintptr_t)(data+8);n->utilization_lba=*(volatile uint64_t*)(uintptr_t)(data+16);n->lba_format=b[26]&0xfu;n->lba_size=namespace_lba_size(b);n->used=(n->size_lba&&n->lba_size)?1:0;if(!n->used)rc=-4;}pmm_free_page(data);return rc;}
static int io_command(size_t index,uint32_t nsid,uint8_t opcode,uint64_t lba,uint32_t count,void*buffer){if(index>=count||!buffer)return -1;rix_nvme_controller_t*c=&controllers[index];if(!c->io_ready||nsid==0||nsid>RIX_NVME_MAX_NAMESPACES)return -2;rix_nvme_namespace_t*n=&c->namespaces[nsid-1];if(!n->used||count==0||count>NVME_IO_DEPTH||lba>=n->size_lba||count>n->size_lba-lba)return -3;uint64_t bytes=(uint64_t)count*n->lba_size;if(bytes>NVME_PAGE_SIZE||((uintptr_t)buffer&0xfffU)!=0)return -4;volatile uint32_t*r=map_regs(c->bar0);if(!r)return -5;volatile nvme_command_t*sq=(volatile nvme_command_t*)(uintptr_t)c->io_sq_phys;volatile nvme_completion_t*cq=(volatile nvme_completion_t*)(uintptr_t)c->io_cq_phys;uint16_t cid=++c->io_cid;if(!cid)cid=++c->io_cid;uint16_t slot=c->io_sq_tail%NVME_IO_DEPTH;for(size_t i=0;i<sizeof(nvme_command_t)/4;i++)((volatile uint32_t*)&sq[slot])[i]=0;sq[slot].cdw0=opcode|((uint32_t)cid<<16);sq[slot].nsid=nsid;sq[slot].prp1=(uint64_t)(uintptr_t)buffer;sq[slot].cdw10=(uint32_t)lba;sq[slot].cdw11=(uint32_t)(lba>>32);sq[slot].cdw12=(count-1)&0xffffu;c->io_sq_tail=(uint16_t)((c->io_sq_tail+1)%NVME_IO_DEPTH);uint64_t stride=4ULL<<c->dstrd;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride*2)=c->io_sq_tail;for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint16_t st=cq[c->io_cq_head].status;if((st&1u)==c->io_cq_phase){if(cq[c->io_cq_head].command_id!=cid)return -6;uint16_t sc=st>>1;c->io_cq_head=(uint16_t)((c->io_cq_head+1)%NVME_IO_DEPTH);if(c->io_cq_head==0)c->io_cq_phase^=1u;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride*3)=c->io_cq_head;return sc?-7:0;}}return -8;}
int nvme_read(size_t controller,uint32_t nsid,uint64_t lba,uint32_t count,void*buffer,size_t buffer_size){if(controller>=count)return -1;rix_nvme_namespace_t*n=&controllers[controller].namespaces[nsid?nsid-1:0];if(!nsid||nsid>RIX_NVME_MAX_NAMESPACES||!n->used||buffer_size<(size_t)count*n->lba_size)return -2;return io_command(controller,nsid,NVME_IO_OP_READ,lba,count,buffer);}
int nvme_write(size_t controller,uint32_t nsid,uint64_t lba,uint32_t count,const void*buffer,size_t buffer_size){if(controller>=count)return -1;if(!buffer||nsid==0||nsid>RIX_NVME_MAX_NAMESPACES)return -2;rix_nvme_namespace_t*n=&controllers[controller].namespaces[nsid-1];if(!n->used||buffer_size<(size_t)count*n->lba_size)return -3;return io_command(controller,nsid,NVME_IO_OP_WRITE,lba,count,(void*)buffer);}
int nvme_flush(size_t controller,uint32_t nsid){if(controller>=count||nsid==0||nsid>RIX_NVME_MAX_NAMESPACES)return -1;rix_nvme_controller_t*c=&controllers[controller];rix_nvme_namespace_t*n=&c->namespaces[nsid-1];if(!n->used||!c->io_ready)return -2;volatile uint32_t*r=map_regs(c->bar0);if(!r)return -3;volatile nvme_command_t*sq=(volatile nvme_command_t*)(uintptr_t)c->io_sq_phys;volatile nvme_completion_t*cq=(volatile nvme_completion_t*)(uintptr_t)c->io_cq_phys;uint16_t cid=++c->io_cid;if(!cid)cid=++c->io_cid;uint16_t slot=c->io_sq_tail%NVME_IO_DEPTH;for(size_t i=0;i<sizeof(nvme_command_t)/4;i++)((volatile uint32_t*)&sq[slot])[i]=0;sq[slot].cdw0=NVME_IO_OP_FLUSH|((uint32_t)cid<<16);sq[slot].nsid=nsid;c->io_sq_tail=(uint16_t)((c->io_sq_tail+1)%NVME_IO_DEPTH);uint64_t stride=4ULL<<c->dstrd;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride*2)=c->io_sq_tail;for(uint32_t i=0;i<NVME_POLL_LIMIT;i++){uint16_t st=cq[c->io_cq_head].status;if((st&1u)==c->io_cq_phase){if(cq[c->io_cq_head].command_id!=cid)return -4;uint16_t sc=st>>1;c->io_cq_head=(uint16_t)((c->io_cq_head+1)%NVME_IO_DEPTH);if(c->io_cq_head==0)c->io_cq_phase^=1u;*(volatile uint32_t*)((uint8_t*)r+0x1000+stride*3)=c->io_cq_head;return sc?-5:0;}}return -6;}
int nvme_init(void){count=0;(void)block_init();for(size_t i=0;i<pci_device_count()&&count<NVME_MAX_CONTROLLERS;i++){const rix_pci_device_t*d=pci_device(i);if(!d||d->class_code!=NVME_CLASS||d->subclass!=NVME_SUBCLASS)continue;uint64_t bar=bar_address(d);if(!bar)continue;volatile uint32_t*r=map_regs(bar);if(!r)continue;uint64_t capv=*(volatile uint64_t*)((uint8_t*)r+NVME_CAP);rix_nvme_controller_t*c=&controllers[count++];c->bus=d->bus;c->device=d->device;c->function=d->function;c->bar0=bar;c->cap=capv;c->version=r[NVME_VS/4];c->cc=r[NVME_CC/4];c->csts=r[NVME_CSTS/4];c->mqes=(uint16_t)(capv&0xffffu);c->dstrd=(uint8_t)((capv>>32)&0xfu);c->css=(uint8_t)((capv>>37)&0xffu);c->admin_ready=0;c->io_ready=0;c->identify_valid=0;for(size_t n=0;n<RIX_NVME_MAX_NAMESPACES;n++)c->namespaces[n].used=0;if(controller_reset(r)==0&&admin_setup(c,r)==0){(void)nvme_identify_controller(count-1);uint32_t lim=c->nn<RIX_NVME_MAX_NAMESPACES?c->nn:RIX_NVME_MAX_NAMESPACES;for(uint32_t nsid=1;nsid<=lim;nsid++)(void)nvme_identify_namespace(count-1,nsid);if(create_io_queue(c,r)!=0)c->io_ready=0;}}return 0;}
size_t nvme_controller_count(void){return count;}
const rix_nvme_controller_t*nvme_controller(size_t i){return i<count?&controllers[i]:NULL;}
