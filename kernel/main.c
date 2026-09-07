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

static uint8_t klog_ready;
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
static void try_mount_root(void){const char *names[]={"nvme0n1","nvme0n1p1","nvme1n1","nvme1n1p1"};for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){rix_block_device_t*d=block_find(names[i]);if(!d)continue;int rc=vfs_mount_root(d);klog_write("VFS: mount ");klog_write(names[i]);klog_write(" rc=");klog_write_dec((uint64_t)(rc<0?-rc:rc));klog_write("\r\n");if(rc==0)return;}}
static volatile uint32_t *g_early_fb;
static uint32_t g_early_pitch;
static uint32_t g_early_width;
static uint16_t g_early_col;
static uint16_t g_early_row;
static uint32_t g_early_height;

#include "tty/font16x16.h"

static void early_fb_glyph(uint16_t col, uint16_t row, char ch, uint32_t fg, uint32_t bg) {
 uint32_t x0 = (uint32_t)col * 16u;
 uint32_t y0 = (uint32_t)row * 16u;
 const uint16_t *glyph = rix_font16x16[(uint8_t)ch < 128u ? (uint8_t)ch : (uint8_t)'?'];
 for (uint32_t y = 0; y < 16u; y++) {
  if (y0 + y >= g_early_height) return;
  uint16_t bits = glyph[y];
  for (uint32_t x = 0; x < 16u; x++) {
   if (x0 + x >= g_early_width) continue;
   volatile uint32_t *px = g_early_fb + (size_t)(y0 + y) * (g_early_pitch / 4u) + x0 + x;
   *px = bits & (1u << (15u - x)) ? fg : bg;
  }
 }
}

static void early_fb_putc(char c);
static void early_fb_puts(const char *s);

static void early_fb_putc(char c) {
 if (!g_early_fb) return;
 if (c == '\r') { g_early_col = 0; return; }
 if (c == '\n') { g_early_col = 0; g_early_row++; goto done; }
 uint32_t cols = g_early_width / 16u;
 if (cols > 120u) cols = 120u;
 if (g_early_col >= (uint16_t)cols) { g_early_col = 0; g_early_row++; }
 if (g_early_row >= 49u) { g_early_row = 0; }
 early_fb_glyph(g_early_col, g_early_row, c, 0x00FFFFFF, 0);
 g_early_col++;
done:;
}

static void early_fb_puts(const char *s) {
 if (!s) return;
 while (*s) early_fb_putc(*s++);
 early_fb_putc('\r');
 early_fb_putc('\n');
}

static void early_fb_put_hex(uint64_t v) {
 static const char d[] = "0123456789abcdef";
 early_fb_puts("0x");
 for (int i = 60; i >= 0; i -= 4) early_fb_putc(d[(v >> i) & 0xF]);
}

static void early_fb_put_dec(uint64_t v) {
 char buf[21]; int i = 20; buf[20] = 0;
 if (v == 0) { buf[--i] = '0'; } else { while (v) { buf[--i] = (char)('0' + v % 10); v /= 10; } }
 early_fb_puts(&buf[i]);
}

void kernel_main(const rixuri_boot_info_t *boot){
 /* Early framebuffer text output for real hardware debug */
 if(boot&&boot->framebuffer_base&&boot->framebuffer_width&&boot->framebuffer_height){
  g_early_fb=(volatile uint32_t *)(uintptr_t)boot->framebuffer_base;
  g_early_pitch=boot->framebuffer_pitch?boot->framebuffer_pitch:boot->framebuffer_width*4u;
  g_early_width=boot->framebuffer_width;
  g_early_height=boot->framebuffer_height;
  early_fb_puts("[EARLY] kernel_main entered");
 }
 serial_init();serial_write("RixuriOS kernel: x86_64 / AMD64 64-bit\r\n");
 early_fb_puts("[EARLY] serial_init done");
 if(!boot||boot->magic!=RIXURI_BOOT_MAGIC||boot->version!=RIXURI_BOOT_VERSION||boot->size<sizeof(*boot))panic("invalid UEFI boot handoff");
 if(!boot->memory_map||!boot->memory_descriptor_size||!boot->memory_map_size)panic("missing UEFI memory map");
 early_fb_puts("[EARLY] boot info OK");
 early_fb_puts("[EARLY] memory_map="); early_fb_put_hex((uint64_t)(uintptr_t)boot->memory_map);
 early_fb_puts("[EARLY] map_size="); early_fb_put_dec(boot->memory_map_size);
 early_fb_puts("[EARLY] desc_size="); early_fb_put_dec(boot->memory_descriptor_size);
 early_fb_puts("[EARLY] kernel_phys="); early_fb_put_hex(boot->kernel_phys_base);
 early_fb_puts("[EARLY] fb_base="); early_fb_put_hex(boot->framebuffer_base);
 early_fb_puts("[EARLY] fb_w="); early_fb_put_dec(boot->framebuffer_width);
 early_fb_puts("[EARLY] fb_h="); early_fb_put_dec(boot->framebuffer_height);
 early_fb_puts("[EARLY] fb_pitch="); early_fb_put_dec(boot->framebuffer_pitch);
 early_fb_puts("[EARLY] fb_format="); early_fb_put_dec(boot->framebuffer_format);
 klog_write("Boot handoff: version=");klog_write_dec(boot->version);klog_write(" size=");klog_write_dec(boot->size);klog_write("\r\n");
 klog_write("ACPI RSDP: ");klog_write_hex(boot->rsdp);klog_write("\r\n");
 early_fb_puts("[EARLY] gdt_init...");
 gdt_init();idt_init();
 early_fb_puts("[EARLY] GDT/IDT done");
 early_fb_puts("[EARLY] pmm_init...");
 pmm_init((const void*)(uintptr_t)boot->memory_map,boot->memory_map_size,boot->memory_descriptor_size,boot->kernel_phys_base,boot->kernel_phys_end,(uint64_t)(uintptr_t)boot,sizeof(*boot));
 early_fb_puts("[EARLY] PMM done free="); early_fb_put_dec(pmm_free_pages());
 if(boot->framebuffer_base&&boot->framebuffer_size){
  uint64_t fb_end=boot->framebuffer_base+boot->framebuffer_size;
  for(uint64_t page=boot->framebuffer_base&~0xfffULL;page<fb_end;page+=0x1000ULL)
   pmm_reserve_page(page);
 }
 early_fb_puts("[EARLY] FB reserved");
 if(!pmm_free_pages())panic("physical memory allocator has no free pages");
 early_fb_puts("[EARLY] vmm_early_init...");
 vmm_early_init();if(!vmm_kernel_pml4())panic("VMM initialization failed");
 early_fb_puts("[EARLY] VMM done");
 early_fb_puts("[EARLY] heap_init...");
 heap_init();void *probe=kmalloc(1,sizeof(uintptr_t));if(!probe)panic("kernel heap init failed");kfree(probe);
 early_fb_puts("[EARLY] heap done");
 early_fb_puts("[EARLY] tty_init...");
 tty_init();
 early_fb_puts("[EARLY] TTY done");
 klog_write("PMM: total=");klog_write_dec(pmm_total_pages());klog_write(" free=");klog_write_dec(pmm_free_pages());klog_write("\r\n");
 klog_write("GOP: base=");klog_write_hex(boot->framebuffer_base);
 klog_write(" size=");klog_write_dec(boot->framebuffer_size);
 klog_write(" width=");klog_write_dec(boot->framebuffer_width);
 klog_write(" height=");klog_write_dec(boot->framebuffer_height);
 klog_write(" pitch=");klog_write_dec(boot->framebuffer_pitch);
 klog_write(" format=");klog_write_dec(boot->framebuffer_format);klog_write("\r\n");
 if(boot->framebuffer_base&&boot->framebuffer_width&&boot->framebuffer_height)
  tty_set_framebuffer(boot->framebuffer_base,(uint32_t)boot->framebuffer_size,
                       boot->framebuffer_width,boot->framebuffer_height,
                       boot->framebuffer_pitch,boot->framebuffer_format);
 klog_ready=1;
 hid_init();klog_write("TTY/HID: initialized\r\n");
 if(boot->rsdp){if(acpi_init(boot->rsdp)==0){klog_write("ACPI CPUs: ");klog_write_dec(acpi_cpu_count());klog_write(" IOAPICs: ");klog_write_dec(acpi_ioapic_count());klog_write("\r\n");}else klog_write("ACPI: unavailable\r\n");}
 if(lapic_init()!=0)panic("local APIC initialization failed");
 if(pci_init()!=0)panic("PCI initialization failed");
 klog_write("PCI: devices=");klog_write_dec(pci_device_count());klog_write("\r\n");
 if(block_init()!=0)panic("block subsystem initialization failed");
 if(vfs_init()!=0)panic("VFS initialization failed");
 if(nvme_init()!=0)panic("NVMe initialization failed");
 klog_write("NVMe: controllers=");klog_write_dec(nvme_controller_count());klog_write("\r\n");
 try_mount_root();
 if(xhci_init()!=0)panic("xHCI initialization failed");
 klog_write("xHCI: controllers=");klog_write_dec(xhci_controller_count());klog_write("\r\n");
 if(pit_init(100)!=0)panic("PIT initialization failed");
 if(rtc_init()!=0)klog_write("RTC: unavailable or non-24-hour mode\r\n");
 if(time_init(100)!=0)klog_write("TIME: realtime clock unavailable; monotonic clock active\r\n");
 else {rix_timespec_t now;if(time_realtime(&now)==0){klog_write("TIME: realtime=");klog_write_dec(now.sec);klog_write("\r\n");}}
 if(scheduler_init()!=0)panic("scheduler initialization failed");
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
 klog_write("USER: embedded init prepared, pid=");klog_write_dec(user_pid);klog_write(" task=");klog_write_dec(user_task);klog_write("\r\n");
 ps2_keyboard_init();
 int io_ready=0;if(acpi_ioapic_count()&&ioapic_init()==0){if(ioapic_route_irq(0,32,(uint8_t)lapic_id())!=0)panic("failed to route PIT IRQ");if(ioapic_route_irq(1,33,(uint8_t)lapic_id())!=0)klog_write("IOAPIC: failed to route IRQ1 (keyboard)\r\n");else{ioapic_unmask_irq(0);ioapic_unmask_irq(1);}pic_disable();io_ready=1;}
 if(io_ready){idt_enable();klog_write("IRQ: PIT routed to vector 32; interrupts enabled\r\n");}else klog_write("IRQ: no usable IOAPIC; interrupts remain disabled\r\n");
 klog_write("xHCI: hotplug worker task=");klog_write_dec(xhci_worker_task);klog_write(" serial TTY worker task=");klog_write_dec(serial_worker_task);klog_write(" kbd poll task=");klog_write_dec(kbd_poll_task);klog_write("\r\n");
 klog_write("Core services: timer/scheduler/process/syscall/PCI/NVMe/xHCI/HID/block/VFS/time initialized\r\n");klog_write("LAPIC: initialized, id=");klog_write_dec(lapic_id());klog_write("\r\n");klog_write("RIXURI:KERNEL_READY\r\n");for(;;)scheduler_yield();
}
