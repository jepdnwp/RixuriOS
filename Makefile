CROSS ?= x86_64-elf-
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
CFLAGS := -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld
OBJ := kernel/boot.o kernel/main.o kernel/serial.o \
 kernel/arch/x86_64/cpu.o kernel/arch/x86_64/gdt.o kernel/arch/x86_64/idt.o kernel/arch/x86_64/interrupts.o kernel/arch/x86_64/irq.o kernel/arch/x86_64/apic.o kernel/arch/x86_64/acpi.o kernel/arch/x86_64/ioapic.o kernel/arch/x86_64/pic.o kernel/arch/x86_64/pit.o kernel/arch/x86_64/pci.o \
 kernel/sched/scheduler.o kernel/process/process.o kernel/syscall/syscall.o kernel/elf/elf.o kernel/vfs/vfs.o kernel/vfs/devfs.o kernel/vfs/procfs.o kernel/vfs/sysfs.o kernel/fs/rixfs.o \
 kernel/storage/block.o kernel/storage/nvme.o kernel/usb/xhci.o kernel/usb/hid.o kernel/tty/tty.o kernel/shell/shell.o kernel/shell/coreutils.o kernel/security/users.o kernel/net/net.o kernel/net/ipv4.o \
 kernel/mm/pmm.o kernel/mm/vmm.o kernel/mm/heap.o
.PHONY: all clean check
all: build/kernel.elf
build/kernel.elf: $(OBJ) linker/kernel.ld
	mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@
check: all
	$(CROSS)objdump -f build/kernel.elf
	$(CROSS)readelf -h build/kernel.elf
	$(CROSS)readelf -l build/kernel.elf
clean:
	rm -rf build kernel/*.o kernel/mm/*.o kernel/arch/x86_64/*.o kernel/sched/*.o kernel/process/*.o kernel/syscall/*.o kernel/elf/*.o kernel/vfs/*.o kernel/fs/*.o kernel/storage/*.o kernel/usb/*.o kernel/tty/*.o kernel/shell/*.o kernel/security/*.o kernel/net/*.o
