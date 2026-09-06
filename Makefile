CROSS ?= x86_64-elf-
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
OBJDUMP := $(CROSS)objdump
CFLAGS := -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld
OBJ := kernel/boot.o kernel/main.o kernel/serial.o kernel/user_init_blob.o \
 kernel/arch/x86_64/cpu.o kernel/arch/x86_64/gdt.o kernel/arch/x86_64/idt.o kernel/arch/x86_64/interrupts.o kernel/arch/x86_64/irq.o kernel/arch/x86_64/apic.o kernel/arch/x86_64/acpi.o kernel/arch/x86_64/ioapic.o kernel/arch/x86_64/pic.o kernel/arch/x86_64/pit.o kernel/arch/x86_64/user_entry.o \
 kernel/pci/pci.o kernel/pci/dma.o kernel/pci/iommu.o kernel/pci/msix.o kernel/sched/scheduler.o kernel/sched/switch.o kernel/process/process.o kernel/process/signal.o kernel/process/address_space.o kernel/syscall/syscall.o kernel/vfs/vfs.o kernel/fs/rixfs.o kernel/fs/rixfs_ops.o kernel/fs/rixfs_dir.o kernel/fs/rixfs_fsck.o kernel/elf/elf.o kernel/elf/loader.o \
 kernel/mm/pmm.o kernel/mm/vmm.o kernel/mm/uaccess.o kernel/mm/heap.o kernel/sync/lock.o kernel/sync/waitqueue.o kernel/ipc/channel.o kernel/ipc/shared_memory.o kernel/tty/tty.o \
 kernel/storage/block.o kernel/storage/block_cache.o kernel/storage/nvme.o kernel/usb/xhci.o kernel/time/rtc.o kernel/time/time.o
.PHONY: all clean check image run qemu build-run test user-init
all: build/kernel.elf
build:
	mkdir -p build
build/user_init.o: user/init.S | build
	$(CC) $(CFLAGS) -c $< -o $@
build/user_init.elf: build/user_init.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ build/user_init.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
kernel/user_init_blob.o: build/user_init.elf | build
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
user-init: build/user_init.elf
build/kernel.elf: $(OBJ) linker/kernel.ld build/user_init.elf | build
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
	$(READELF) -h $@ >/dev/null
	$(READELF) -l build/kernel.elf >/dev/null
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@
check: all
	$(OBJDUMP) -f build/kernel.elf
	$(READELF) -h build/kernel.elf
	$(READELF) -l build/kernel.elf
	$(OBJDUMP) -drwC build/kernel.elf > build/kernel.disasm
image: all
	bash ./scripts/build-uefi.sh
run: image
	bash ./scripts/run-qemu.sh
qemu: run
build-run: clean
	$(MAKE) all
	$(MAKE) check
	$(MAKE) image
	$(MAKE) run
test: check
	@echo 'Static kernel build checks completed.'
clean:
	rm -rf build kernel/*.o kernel/mm/*.o kernel/arch/x86_64/*.o kernel/pci/*.o kernel/sched/*.o kernel/process/*.o kernel/syscall/*.o kernel/vfs/*.o kernel/fs/*.o kernel/elf/*.o kernel/sync/*.o kernel/storage/*.o kernel/usb/*.o kernel/ipc/*.o kernel/tty/*.o kernel/time/*.o
