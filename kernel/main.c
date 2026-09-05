#include "kernel.h"
#include "serial.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/ioapic.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "pci/pci.h"
#include "sched/scheduler.h"
#include "process/process.h"
#include "syscall/syscall.h"
#include "vfs/vfs.h"
#include "storage/block.h"
#include "storage/nvme.h"
#include "usb/xhci.h"

static void halt_forever(void){for(;;)__asm__ volatile("hlt");}
void kernel_main(const rixuri_boot_info_t *boot){
 serial_init();serial_write("RixuriOS kernel: x86_64 / AMD64 64-bit\r\n");
 if(!boot||boot->magic!=RIXURI_BOOT_MAGIC||boot->version!=RIXURI_BOOT_VERSION||boot->size<sizeof(*boot))panic("invalid UEFI boot handoff");
 if(!boot->memory_map||!boot->memory_descriptor_size||!boot->memory_map_size)panic("missing UEFI memory map");
 serial_write("Boot handoff: version=");serial_write_dec(boot->version);serial_write(" size=");serial_write_dec(boot->size);serial_write("\r\n");
 serial_write("ACPI RSDP: ");serial_write_hex(boot->rsdp);serial_write("\r\n");
 gdt_init();idt_init();serial_write("GDT/IDT: initialized\r\n");
 pmm_init((const void*)(uintptr_t)boot->memory_map,boot->memory_map_size,boot->memory_descriptor_size,boot->kernel_phys_base,boot->kernel_phys_end,(uint64_t)(uintptr_t)boot,sizeof(*boot));
 if(!pmm_free_pages())panic("physical memory allocator has no free pages");
 serial_write("PMM: total=");serial_write_dec(pmm_total_pages());serial_write(" free=");serial_write_dec(pmm_free_pages());serial_write("\r\n");
 vmm_early_init();if(!vmm_kernel_pml4())panic("VMM initialization failed");serial_write("VMM: initialized\r\n");
 heap_init();void *probe=kmalloc(1,sizeof(uintptr_t));if(!probe)panic("kernel heap initialization failed");kfree(probe);serial_write("KHEAP: initialized\r\n");
 if(boot->rsdp){if(acpi_init(boot->rsdp)==0){serial_write("ACPI CPUs: ");serial_write_dec(acpi_cpu_count());serial_write(" IOAPICs: ");serial_write_dec(acpi_ioapic_count());serial_write("\r\n");}else serial_write("ACPI: unavailable\r\n");}
 if(lapic_init()!=0)panic("local APIC initialization failed");
 if(pci_init()!=0)panic("PCI initialization failed");
 serial_write("PCI: devices=");serial_write_dec(pci_device_count());serial_write("\r\n");
 if(nvme_init()!=0)panic("NVMe initialization failed");
 serial_write("NVMe: controllers=");serial_write_dec(nvme_controller_count());serial_write("\r\n");
 if(xhci_init()!=0)panic("xHCI initialization failed");
 serial_write("xHCI: controllers=");serial_write_dec(xhci_controller_count());serial_write("\r\n");
 if(pit_init(100)!=0)panic("PIT initialization failed");
 if(scheduler_init()!=0)panic("scheduler initialization failed");
 if(process_init()!=0)panic("process subsystem initialization failed");
 if(block_init()!=0)panic("block subsystem initialization failed");
 if(vfs_init()!=0)panic("VFS initialization failed");
 syscall_init();
 int io_ready=0;
 if(acpi_ioapic_count()&&ioapic_init()==0){
   if(ioapic_route_irq(0,32,(uint8_t)lapic_id())!=0)panic("failed to route PIT IRQ");
   ioapic_unmask_irq(0);pic_disable();io_ready=1;
 }
 if(io_ready){idt_enable();serial_write("IRQ: PIT routed to vector 32; interrupts enabled\r\n");}
 else serial_write("IRQ: no usable IOAPIC; interrupts remain disabled\r\n");
 serial_write("Core services: timer/scheduler/process/syscall/PCI/NVMe/xHCI/block/VFS initialized\r\n");
 serial_write("LAPIC: initialized, id=");serial_write_dec(lapic_id());serial_write("\r\n");
 halt_forever();
}
