#include "kernel.h"
#include "serial.h"
#include "user_init.h"
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
#include "usb/hid.h"
#include "tty/tty.h"
#include "time/rtc.h"
#include "time/time.h"
#define RIX_XHCI_CONFIG_CAPACITY 4096u
static uint8_t xhci_configuration[RIX_XHCI_CONFIG_CAPACITY];
static uint8_t xhci_hid_report[2048];
static rix_usb_interface_info_t xhci_interfaces[RIX_USB_MAX_INTERFACES];
static rix_usb_endpoint_info_t xhci_endpoints[RIX_USB_MAX_ENDPOINTS];
static int xhci_enumerate_and_configure(size_t controller, const rix_xhci_device_t *device){
 rix_usb_device_descriptor_t usb_device;rix_usb_configuration_info_t configuration;
 size_t interface_count=0,endpoint_count=0;
 int rc=xhci_enumerate_device(controller,device->slot_id,&usb_device,xhci_configuration,
                              sizeof(xhci_configuration),&configuration,xhci_interfaces,
                              RIX_USB_MAX_INTERFACES,xhci_endpoints,RIX_USB_MAX_ENDPOINTS,
                              &interface_count,&endpoint_count);
 if(rc!=0)return rc;
 for(size_t i=0;i<endpoint_count;i++){
  const rix_usb_endpoint_info_t *endpoint=&xhci_endpoints[i];
  uint8_t transfer=endpoint->attributes&RIX_USB_EP_TRANSFER_MASK;
  if(transfer==RIX_USB_EP_CONTROL||transfer==RIX_USB_EP_ISOCHRONOUS)continue;
  rix_xhci_endpoint_config_t config={endpoint->address,endpoint->attributes,
                                      endpoint->max_packet_size,endpoint->interval,0};
  rc=xhci_configure_endpoint(controller,device->slot_id,&config);
  if(rc!=0)return rc;
 }
 for(size_t i=0;i<interface_count;i++){
  const rix_usb_interface_info_t *interface=&xhci_interfaces[i];
  if(interface->class_code!=3u || interface->hid_report_descriptor_length==0u)continue;
  if(interface->hid_report_descriptor_length>sizeof(xhci_hid_report))return -7;
  uint16_t actual_report=0;
  rc=xhci_get_hid_report_descriptor(controller,device->slot_id,interface->number,
                                     xhci_hid_report,interface->hid_report_descriptor_length,
                                     &actual_report);
  if(rc!=0 || actual_report!=interface->hid_report_descriptor_length)return -8;
  rix_hid_report_info_t report_info;
  if(hid_parse_report_descriptor(xhci_hid_report,actual_report,&report_info)!=0)continue;
  if(!report_info.has_keyboard && !report_info.has_mouse)continue;
  rc=xhci_hid_set_protocol(controller,device->slot_id,interface->number,1u);
  if(rc!=0)return -9;
  rc=xhci_hid_set_idle(controller,device->slot_id,interface->number,report_info.report_id,0u);
  if(rc!=0)return -10;
  serial_write("xHCI: HID interface=");serial_write_dec(interface->number);
  serial_write(" keyboard=");serial_write_dec(report_info.has_keyboard);
  serial_write(" mouse=");serial_write_dec(report_info.has_mouse);
  serial_write(" report-id=");serial_write_dec(report_info.report_id);serial_write("\r\n");
 }
 serial_write("xHCI: enumerated vid=");serial_write_hex(usb_device.vendor_id);
 serial_write(" pid=");serial_write_hex(usb_device.product_id);
 serial_write(" interfaces=");serial_write_dec(interface_count);
 serial_write(" endpoints=");serial_write_dec(endpoint_count);serial_write("\r\n");
 return 0;
}
static int terminal_signal_group(uint32_t process_group,unsigned signal){return process_signal_group((pid_t)process_group,signal);}
static void xhci_hotplug_worker(void *arg){
 (void)arg;
 for(;;){
  for(size_t controller=0;controller<xhci_controller_count();controller++){
   rix_xhci_device_t device;uint8_t connected=0;
   int rc=xhci_service_hotplug(controller,&device,&connected);
   if(rc>0){
    serial_write("xHCI: device ");serial_write(connected?"attached":"detached");
    serial_write(" controller=");serial_write_dec(controller);
    serial_write(" port=");serial_write_dec(device.port);
    serial_write(" slot=");serial_write_dec(device.slot_id);serial_write("\r\n");
    if(connected){
     int enum_rc=xhci_enumerate_and_configure(controller,&device);
     if(enum_rc!=0){
      serial_write("xHCI: enumeration/configuration failed=");
      serial_write_dec((uint64_t)(-enum_rc));serial_write("\r\n");
      (void)xhci_device_detach(controller,device.slot_id);
     }
    }
   } else if(rc<0){
    serial_write("xHCI: hotplug service error=");serial_write_dec((uint64_t)(-rc));
    serial_write(" controller=");serial_write_dec(controller);serial_write("\r\n");
   }
  }
  scheduler_yield();
 }
}
static void try_mount_root(void){const char *names[]={"nvme0n1","nvme0n1p1","nvme1n1","nvme1n1p1"};for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){rix_block_device_t*d=block_find(names[i]);if(!d)continue;int rc=vfs_mount_root(d);serial_write("VFS: mount ");serial_write(names[i]);serial_write(" rc=");serial_write_dec((uint64_t)(rc<0?-rc:rc));serial_write("\r\n");if(rc==0)return;}}
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
 tty_init();hid_init();serial_write("TTY/HID: initialized\r\n");
 if(boot->rsdp){if(acpi_init(boot->rsdp)==0){serial_write("ACPI CPUs: ");serial_write_dec(acpi_cpu_count());serial_write(" IOAPICs: ");serial_write_dec(acpi_ioapic_count());serial_write("\r\n");}else serial_write("ACPI: unavailable\r\n");}
 if(lapic_init()!=0)panic("local APIC initialization failed");
 if(pci_init()!=0)panic("PCI initialization failed");
 serial_write("PCI: devices=");serial_write_dec(pci_device_count());serial_write("\r\n");
 if(block_init()!=0)panic("block subsystem initialization failed");
 if(vfs_init()!=0)panic("VFS initialization failed");
 if(nvme_init()!=0)panic("NVMe initialization failed");
 serial_write("NVMe: controllers=");serial_write_dec(nvme_controller_count());serial_write("\r\n");
 try_mount_root();
 if(xhci_init()!=0)panic("xHCI initialization failed");
 serial_write("xHCI: controllers=");serial_write_dec(xhci_controller_count());serial_write("\r\n");
 if(pit_init(100)!=0)panic("PIT initialization failed");
 if(rtc_init()!=0)serial_write("RTC: unavailable or non-24-hour mode\r\n");
 if(time_init(100)!=0)serial_write("TIME: realtime clock unavailable; monotonic clock active\r\n");
 else {rix_timespec_t now;if(time_realtime(&now)==0){serial_write("TIME: realtime=");serial_write_dec(now.sec);serial_write("\r\n");}}
 if(scheduler_init()!=0)panic("scheduler initialization failed");
 if(process_init()!=0)panic("process initialization failed");
 tty_set_signal_hook(terminal_signal_group);
 syscall_init();
 uint64_t user_entry=0,user_stack=0;pid_t user_pid=0;rix_task_id_t user_task=0;
 if(process_create_user("init",0,rixuri_user_init_image(),rixuri_user_init_image_size(),&user_pid,&user_entry,&user_stack)!=0)panic("failed to create embedded user init");
 rix_process_t *user_proc=process_lookup(user_pid);if(!user_proc)panic("embedded user init process lookup failed");
 if(scheduler_create_user_process(user_pid,user_entry,user_stack,&user_task)!=0)panic("failed to create user init task");
 rix_task_id_t xhci_worker_task=0;
 if(scheduler_create_kernel_thread(xhci_hotplug_worker,0,&xhci_worker_task)!=0)panic("failed to create xHCI hotplug worker");
 serial_write("USER: embedded init prepared, pid=");serial_write_dec(user_pid);serial_write(" task=");serial_write_dec(user_task);serial_write("\r\n");
 int io_ready=0;if(acpi_ioapic_count()&&ioapic_init()==0){if(ioapic_route_irq(0,32,(uint8_t)lapic_id())!=0)panic("failed to route PIT IRQ");ioapic_unmask_irq(0);pic_disable();io_ready=1;}
 if(io_ready){idt_enable();serial_write("IRQ: PIT routed to vector 32; interrupts enabled\r\n");}else serial_write("IRQ: no usable IOAPIC; interrupts remain disabled\r\n");
 serial_write("xHCI: hotplug worker task=");serial_write_dec(xhci_worker_task);serial_write("\r\n");
 serial_write("Core services: timer/scheduler/process/syscall/PCI/NVMe/xHCI/HID/block/VFS/time initialized\r\n");serial_write("LAPIC: initialized, id=");serial_write_dec(lapic_id());serial_write("\r\n");serial_write("RIXURI:KERNEL_READY\r\n");scheduler_yield();serial_write("USER: init returned to kernel\r\n");for(;;)scheduler_yield();
}
