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
#include "arch/x86_64/smp.h"
#include "arch/x86_64/ioapic.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/ps2_keyboard.h"
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
#include "net/e1000.h"
#include "net/rtl8125.h"
#include "net/device.h"
#include "net/stack.h"
#include "net/dhcp.h"

static uint8_t klog_ready;
static rix_net_stack_t net_stack;
static size_t klog_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void klog_write(const char *s) {
    serial_write(s);
    if (klog_ready) { size_t w = 0; tty_output(0, s, klog_strlen(s), &w); }
}
static void klog_write_n(const char *s, size_t n) {
    serial_write_n(s, n);
    if (klog_ready) { size_t w = 0; tty_output(0, s, n, &w); }
}
static void klog_write_dec(uint64_t v) {
    char buf[21]; size_t i = sizeof(buf);
    if (v == 0) { klog_write("0"); return; }
    while (v) { buf[--i] = (char)('0' + v % 10ULL); v /= 10ULL; }
    klog_write(&buf[i]);
}
static void klog_write_hex(uint64_t v) {
    static const char d[] = "0123456789abcdef";
    klog_write("0x");
    for (int s = 60; s >= 0; s -= 4) {
        char c = d[(v >> s) & 0xFULL];
        klog_write_n(&c, 1);
    }
}

#define RIX_XHCI_CONFIG_CAPACITY 4096u
static uint8_t xhci_configuration[RIX_XHCI_CONFIG_CAPACITY];
static uint8_t xhci_hid_report[2048];
static rix_usb_interface_info_t xhci_interfaces[RIX_USB_MAX_INTERFACES];
static rix_usb_endpoint_info_t xhci_endpoints[RIX_USB_MAX_ENDPOINTS];

#define RIX_MAX_KEYBOARDS 4u
typedef struct { uint8_t used; size_t controller; uint8_t slot; uint8_t endpoint; } keyboard_info_t;
static keyboard_info_t known_keyboards[RIX_MAX_KEYBOARDS];
static uint8_t kbd_report_buf[RIX_HID_BOOT_KEYBOARD_REPORT];
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
  if(report_info.has_keyboard){
   for(size_t k=0;k<RIX_MAX_KEYBOARDS;k++){
    if(!known_keyboards[k].used){
     known_keyboards[k].used=1;known_keyboards[k].controller=controller;
     known_keyboards[k].slot=device->slot_id;
     for(size_t e=0;e<endpoint_count;e++){
      if((xhci_endpoints[e].attributes&RIX_USB_EP_TRANSFER_MASK)==RIX_USB_EP_INTERRUPT){
       known_keyboards[k].endpoint=xhci_endpoints[e].address;break;
      }
     }
     serial_write("xHCI: keyboard registered slot=");serial_write_dec(device->slot_id);
     serial_write(" ep=0x");serial_write_hex(known_keyboards[k].endpoint);serial_write("\r\n");
     break;
    }
   }
  }
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
     klog_write_dec((uint64_t)(-enum_rc));klog_write("\r\n");
     (void)xhci_device_detach(controller,device.slot_id);
    }
   }
   } else if(rc<0){
   klog_write("xHCI: hotplug service error=");klog_write_dec((uint64_t)(-rc));
   klog_write(" controller=");klog_write_dec(controller);klog_write("\r\n");
   }
  }
  scheduler_yield();
 }
}
static void serial_tty_worker(void *arg){
 (void)arg;
 for(;;){
  uint8_t byte;
  while(serial_read_byte(&byte)==0){
   if(byte=='\r')byte='\n';
   if(tty_input(0,byte)==0)serial_write_n((const char*)&byte,1);
  }
  scheduler_yield();
 }
}
static void keyboard_poll_worker(void *arg){
 (void)arg;
 for(;;){
  ps2_keyboard_poll();
  for(size_t k=0;k<RIX_MAX_KEYBOARDS;k++){
   if(!known_keyboards[k].used)continue;
   uint16_t actual=0;
   int rc=hid_xhci_keyboard_poll(known_keyboards[k].controller,
    known_keyboards[k].slot,known_keyboards[k].endpoint,
    0,kbd_report_buf,sizeof(kbd_report_buf),&actual);
   (void)rc;
  }
  scheduler_yield();
 }
}
static void network_poll_worker(void *arg){
 (void)arg;
 for(;;){
  /* Full poll (timers + DMA consume into the bounded RX queue). Fast,
     sub-millisecond consumes keep the e1000 descriptor ring turning;
     socket paths drain via take. */
  (void)rix_net_stack_poll(&net_stack,time_monotonic_ns()/1000000000ULL);
  scheduler_yield();
 }
}
static const char *vfs_mount_rc_string(int rc){switch(rc){case 0:return "ok";case -1:return "bad device";case -2:return "no memory";case -3:return "superblock read IO error";case -4:return "superblock too small";case -5:return "not a RixFS superblock";case -6:return "superblock geometry inconsistent";case -7:return "inode table overflow";case -8:return "superblock layout overlap";case -9:return "journal replay failed";default:return "unknown";}}
static void try_mount_root(void){const char *names[]={"nvme0n1","nvme0n1p1","nvme1n1","nvme1n1p1"};for(size_t b=0;b<block_device_count();b++){const rix_block_device_t*bd=block_device_at(b);if(!bd)continue;klog_write("BLOCK: ");klog_write(bd->name);klog_write(" sectors=");klog_write_dec(bd->sector_count);klog_write(" sector_size=");klog_write_dec(bd->sector_size);klog_write("\r\n");}for(size_t c=0;c<nvme_controller_count();c++){const rix_nvme_controller_t*nc=nvme_controller(c);if(!nc)continue;for(uint32_t ns=0;ns<32u;ns++){if(!nc->namespaces[ns].used)continue;klog_write("NVMe: ctrl=");klog_write_dec(c);klog_write(" ns=");klog_write_dec(nc->namespaces[ns].nsid);klog_write(" sectors=");klog_write_dec(nc->namespaces[ns].size_lba);klog_write(" sector_size=");klog_write_dec(nc->namespaces[ns].lba_size);klog_write("\r\n");}}int last_rc=0,tried=0;for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){rix_block_device_t*d=block_find(names[i]);if(!d)continue;tried=1;int rc=vfs_mount_root(d);last_rc=rc;klog_write("VFS: mount ");klog_write(names[i]);klog_write(" rc=");klog_write_dec((uint64_t)(rc<0?-rc:rc));klog_write("\r\n");if(rc==0)return;}if(!tried)klog_write("VFS: no candidate block devices present (embedded init continues)\r\n");else if(last_rc!=0){klog_write("VFS: disk root unavailable (");klog_write(vfs_mount_rc_string(last_rc));klog_write("); using embedded init\r\n");}}

void kernel_main(const rixuri_boot_info_t *boot){
 /* serial + GDT/IDT first. NO framebuffer access before VMM init! */
 serial_init();
 gdt_init();idt_init();
 serial_write("[EARLY] GDT/IDT done\r\n");
 if(!boot||boot->magic!=RIXURI_BOOT_MAGIC||boot->version!=RIXURI_BOOT_VERSION||boot->size<sizeof(*boot))panic("invalid UEFI boot handoff");
 if(!boot->memory_map||!boot->memory_descriptor_size||!boot->memory_map_size)panic("missing UEFI memory map");
 serial_write("[EARLY] boot info OK\r\n");
 serial_write("[EARLY] fb_base=");serial_write_hex(boot->framebuffer_base);
 serial_write(" w=");serial_write_dec(boot->framebuffer_width);
 serial_write(" h=");serial_write_dec(boot->framebuffer_height);
 serial_write(" pitch=");serial_write_dec(boot->framebuffer_pitch);
 serial_write(" fmt=");serial_write_dec(boot->framebuffer_format);serial_write("\r\n");
 pmm_init((const void*)(uintptr_t)boot->memory_map,boot->memory_map_size,boot->memory_descriptor_size,boot->kernel_phys_base,boot->kernel_phys_end,(uint64_t)(uintptr_t)boot,sizeof(*boot));
 serial_write("[EARLY] PMM done\r\n");
 if(boot->framebuffer_base&&boot->framebuffer_size){
  uint64_t fb_end=boot->framebuffer_base+boot->framebuffer_size;
  for(uint64_t page=boot->framebuffer_base&~0xfffULL;page<fb_end;page+=0x1000ULL)
   pmm_reserve_page(page);
 }
 if(!pmm_free_pages())panic("physical memory allocator has no free pages");
 serial_write("[EARLY] starting vmm_init...\r\n");
 vmm_early_init();if(!vmm_kernel_pml4())panic("VMM initialization failed");
 serial_write("[EARLY] VMM done\r\n");
 heap_init();void *probe=kmalloc(1,sizeof(uintptr_t));if(!probe)panic("kernel heap init failed");kfree(probe);
 serial_write("[EARLY] heap done\r\n");
 tty_init();
 serial_write("[EARLY] TTY done\r\n");
 /* Attach GOP immediately after TTY setup so all following diagnostics are
  * visible on the physical display, not only on a debug serial console. */
  if(boot->framebuffer_base&&boot->framebuffer_width&&boot->framebuffer_height)
   tty_set_framebuffer(boot->framebuffer_base,(uint32_t)boot->framebuffer_size,
                       boot->framebuffer_width,boot->framebuffer_height,
                       boot->framebuffer_pitch,boot->framebuffer_format);
  serial_console_enable();
 klog_ready=1;
 klog_write("RixuriOS kernel: display diagnostics enabled\r\n");
 klog_write("PMM: total=");klog_write_dec(pmm_total_pages());klog_write(" free=");klog_write_dec(pmm_free_pages());klog_write("\r\n");
 klog_write("GOP: base=");klog_write_hex(boot->framebuffer_base);
 klog_write(" size=");klog_write_dec(boot->framebuffer_size);
 klog_write(" width=");klog_write_dec(boot->framebuffer_width);
 klog_write(" height=");klog_write_dec(boot->framebuffer_height);
 klog_write(" pitch=");klog_write_dec(boot->framebuffer_pitch);
 klog_write(" format=");klog_write_dec(boot->framebuffer_format);klog_write("\r\n");
  hid_init();klog_write("TTY/HID: initialized\r\n");
  klog_write("BOOT: ACPI begin\r\n");
  if(boot->rsdp){int acpi_rc=acpi_init(boot->rsdp);if(acpi_rc==0){klog_write("ACPI CPUs: ");klog_write_dec(acpi_cpu_count());klog_write(" IOAPICs: ");klog_write_dec(acpi_ioapic_count());klog_write("\r\n");}else{klog_write("ACPI: unavailable (");klog_write(acpi_error_string(acpi_rc));klog_write(")\r\n");}}
  klog_write("BOOT: ACPI done\r\n");
  if(lapic_init()!=0)panic("local APIC initialization failed");
  if(smp_discover()!=0)klog_write("SMP: discovery unavailable, uniprocessor boot\r\n");
  else{klog_write("SMP: cpus=");klog_write_dec(smp_cpu_count());klog_write(" online=");klog_write_dec(smp_online_count());klog_write(" bsp_apic=");klog_write_dec(smp_bsp_apic());klog_write("\r\n");}
  if(smp_cpu_count()>1){int smp_online=smp_start_aps();klog_write("SMP: online=");klog_write_dec(smp_online<0?0u:(uint64_t)smp_online);klog_write("\r\n");for(size_t smp_i=0;smp_i<smp_cpu_count();smp_i++){const smp_cpu_t*smp_c=smp_cpu(smp_i);if(smp_c&&!smp_c->is_bsp&&smp_c->state==SMP_CPU_ONLINE){klog_write("AP ");klog_write_dec(smp_c->apic_id);klog_write(" online\r\n");}}}
  if(pci_init()!=0)panic("PCI initialization failed");
  klog_write("PCI: devices=");klog_write_dec(pci_device_count());klog_write("\r\n");
  pci_print_devices();
   klog_write("BOOT: PCI done\r\n");
   (void)rix_e1000_init();
 (void)rix_rtl8125_init();
 if(rix_net_device_init()==0){const rix_net_device_info_t *net=rix_net_device_info();klog_write("NET: backend=");klog_write(rix_net_device_backend());klog_write(" device link=");klog_write_dec(net->link_up);klog_write(" ip=");klog_write_hex(net->address);klog_write(" gateway=");klog_write_hex(net->gateway);klog_write(" dns=");klog_write_hex(net->dns);klog_write("\r\n");if(rix_net_stack_init(&net_stack)!=0)klog_write("NET: stack init failed\r\n");else if(!net->link_up)klog_write("NET: link down, keeping static config\r\n");else if(rix_net_dhcp_run()==0){net=rix_net_device_info();klog_write("NET: dhcp ip=");klog_write_hex(net->address);klog_write(" gateway=");klog_write_hex(net->gateway);klog_write(" dns=");klog_write_hex(net->dns);klog_write("\r\n");}else klog_write("NET: dhcp failed, using static config\r\n");}
 else klog_write("NET: no usable network device\r\n");
 if(block_init()!=0)panic("block subsystem initialization failed");
 if(vfs_init()!=0)panic("VFS initialization failed");
  if(nvme_init()!=0)panic("NVMe initialization failed");
  klog_write("NVMe: controllers=");klog_write_dec(nvme_controller_count());klog_write("\r\n");
  klog_write("BOOT: NVMe done\r\n");
  try_mount_root();
  klog_write("BOOT: VFS mount done\r\n");
  klog_write("BOOT: xHCI begin\r\n");
  if(xhci_init()!=0)panic("xHCI initialization failed");
  klog_write("xHCI: controllers=");klog_write_dec(xhci_controller_count());klog_write("\r\n");
  klog_write("BOOT: xHCI done\r\n");
 if(pit_init(100)!=0)panic("PIT initialization failed");
 if(rtc_init()!=0)klog_write("RTC: unavailable or non-24-hour mode\r\n");
 if(time_init(100)!=0)klog_write("TIME: realtime clock unavailable; monotonic clock active\r\n");
 else {rix_timespec_t now;if(time_realtime(&now)==0){klog_write("TIME: realtime=");klog_write_dec(now.sec);klog_write("\r\n");}}
  if(scheduler_init()!=0)panic("scheduler initialization failed");
  klog_write("BOOT: scheduler begin\r\n");
  if(process_init()!=0)panic("process initialization failed");
 tty_set_signal_hook(terminal_signal_group);
 syscall_init();
 uint64_t user_entry=0,user_stack=0;pid_t user_pid=0;rix_task_id_t user_task=0;
 if(process_create_user("init",0,rixuri_user_init_image(),rixuri_user_init_image_size(),&user_pid,&user_entry,&user_stack)!=0)panic("failed to create embedded user init");
 rix_process_t *user_proc=process_lookup(user_pid);if(!user_proc)panic("embedded user init process lookup failed");
 if(scheduler_create_user_process(user_pid,user_entry,user_stack,&user_task)!=0)panic("failed to create user init task");
 tty_set_foreground_pgrp(0, (uint32_t)user_proc->process_group);
 rix_task_id_t xhci_worker_task=0;
 if(scheduler_create_kernel_thread(xhci_hotplug_worker,0,&xhci_worker_task)!=0)panic("failed to create xHCI hotplug worker");
 rix_task_id_t serial_worker_task=0;
 if(scheduler_create_kernel_thread(serial_tty_worker,0,&serial_worker_task)!=0)panic("failed to create serial TTY worker");
 rix_task_id_t kbd_poll_task=0;
 if(scheduler_create_kernel_thread(keyboard_poll_worker,0,&kbd_poll_task)!=0)panic("failed to create keyboard poll worker");
 rix_task_id_t network_poll_task=0;
 if(scheduler_create_kernel_thread(network_poll_worker,0,&network_poll_task)!=0)panic("failed to create network poll worker");
	 klog_write("USER: embedded init prepared, pid=");klog_write_dec(user_pid);klog_write(" task=");klog_write_dec(user_task);klog_write("\r\n");
	 klog_write("BOOT: ps2 init begin\r\n");
	 ps2_keyboard_init();
	 klog_write("BOOT: ps2 init done\r\n");
	 klog_write("BOOT: ioapic init begin\r\n");
	 int io_ready=0;if(acpi_ioapic_count()&&ioapic_init()==0){if(ioapic_route_irq(0,32,(uint8_t)lapic_id())!=0)panic("failed to route PIT IRQ");if(ioapic_route_irq(1,33,(uint8_t)lapic_id())!=0)klog_write("IOAPIC: failed to route IRQ1 (keyboard)\r\n");else{ioapic_unmask_irq(0);ioapic_unmask_irq(1);}pic_disable();io_ready=1;}
	 klog_write("BOOT: ioapic init done\r\n");
 if(io_ready){idt_enable();klog_write("IRQ: PIT routed through IOAPIC; interrupts enabled\r\n");}
 else if(pic_init()==0){lapic_enable_pic_extint();idt_enable();klog_write("IRQ: IOAPIC unavailable; LAPIC ExtINT/PIC fallback enabled\r\n");}
 else klog_write("IRQ: no usable interrupt controller; interrupts remain disabled\r\n");
         klog_write("xHCI: hotplug worker task=");klog_write_dec(xhci_worker_task);klog_write(" serial TTY worker task=");klog_write_dec(serial_worker_task);klog_write(" kbd poll task=");klog_write_dec(kbd_poll_task);klog_write(" net poll task=");klog_write_dec(network_poll_task);klog_write("\r\n");
 klog_write("Core services: timer/scheduler/process/syscall/PCI/NVMe/xHCI/HID/block/VFS/time initialized\r\n");klog_write("LAPIC: initialized, id=");klog_write_dec(lapic_id());klog_write("\r\n");klog_write("RIXURI:KERNEL_READY\r\n");for(;;)scheduler_yield();
}
