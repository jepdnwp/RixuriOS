CROSS ?= x86_64-elf-
HOST_CC ?= gcc
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
 kernel/mm/pmm.o kernel/mm/vmm.o kernel/mm/uaccess.o kernel/mm/heap.o kernel/sync/lock.o kernel/sync/waitqueue.o kernel/ipc/channel.o kernel/ipc/pipe.o kernel/ipc/shared_memory.o kernel/tty/tty.o \
 kernel/storage/block.o kernel/storage/block_cache.o kernel/storage/nvme.o kernel/usb/xhci.o kernel/usb/usb.o kernel/usb/hid.o kernel/time/rtc.o kernel/time/time.o kernel/power/power.o

PROGRAM_NAMES := echo cat args grep true false sleep ls mkdir rm abi-negative
PROGRAM_ELFS := $(addprefix build/programs/,$(addsuffix .elf,$(PROGRAM_NAMES)))
PROGRAM_START_OBJ := build/programs/start.o

.PHONY: all clean check image run qemu build-run test user-init programs rixfs-image usb-test hid-test tty-test shell-test pipe-test
all: build/kernel.elf

build:
	mkdir -p build

USER_INIT_CFLAGS := $(CFLAGS) -mcmodel=large -Iuser/shell -Iuser/libc/include
build/user_init.o: user/init.c user/shell/shell.h user/libc/include/unistd.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_shell.o: user/shell/shell.c user/shell/shell.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_unistd.o: user/libc/src/unistd.c user/libc/include/unistd.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_init.elf: build/user_init.o build/user_shell.o build/user_unistd.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ build/user_init.o build/user_shell.o build/user_unistd.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
kernel/user_init_blob.o: build/user_init.elf | build
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	$(OBJCOPY) --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=readonly,contents $@
user-init: build/user_init.elf

build/programs/%.o: user/programs/%.c user/libc/include/unistd.h | build
	mkdir -p build/programs
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/programs/start.o: user/programs/start.S | build
	mkdir -p build/programs
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/programs/%.elf: build/programs/%.o $(PROGRAM_START_OBJ) build/user_unistd.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ $(PROGRAM_START_OBJ) build/programs/$*.o build/user_unistd.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
programs: $(PROGRAM_ELFS)

build/rixfs.img: programs scripts/build-rixfs-image.py | build
	python3 scripts/build-rixfs-image.py -o $@ \
		--file /bin/echo=build/programs/echo.elf \
		--file /bin/cat=build/programs/cat.elf \
		--file /bin/true=build/programs/true.elf \
		--file /usr/bin/args=build/programs/args.elf \
		--file /usr/bin/grep=build/programs/grep.elf \
		--file /bin/sleep=build/programs/sleep.elf \
		--file /bin/ls=build/programs/ls.elf \
		--file /bin/mkdir=build/programs/mkdir.elf \
		--file /bin/rm=build/programs/rm.elf \
		--file /usr/bin/abi-negative=build/programs/abi-negative.elf \
		--file /sbin/false=build/programs/false.elf \
		--file /usr/sbin/true=build/programs/true.elf
rixfs-image: build/rixfs.img

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

image: all rixfs-image
	bash ./scripts/build-uefi.sh
run: image
	bash ./scripts/run-qemu.sh
qemu: run
build-run: clean
	$(MAKE) all
	$(MAKE) check
	$(MAKE) image
	$(MAKE) run

usb-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/usb_descriptor_test.c kernel/usb/usb.c -o build/usb_descriptor_test
	build/usb_descriptor_test
hid-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DHID_PARSER_HOST_TEST -I. tests/hid_report_test.c kernel/usb/hid.c -o build/hid_report_test
	build/hid_report_test
tty-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/tty_test.c kernel/tty/tty.c -o build/tty_test
	build/tty_test
shell-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/shell_test.c user/shell/shell.c -o build/shell_test
		build/shell_test

pipe-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DRIX_HOST_TEST -I. tests/pipe_test.c kernel/ipc/channel.c kernel/ipc/pipe.c kernel/sync/lock.c -o build/pipe_test
		build/pipe_test

test: check usb-test hid-test tty-test shell-test pipe-test
	@echo 'Static kernel build checks completed.'

clean:
	rm -rf build kernel/*.o kernel/mm/*.o kernel/arch/x86_64/*.o kernel/pci/*.o kernel/sched/*.o kernel/process/*.o kernel/syscall/*.o kernel/vfs/*.o kernel/fs/*.o kernel/elf/*.o kernel/sync/*.o kernel/storage/*.o kernel/usb/*.o kernel/ipc/*.o kernel/tty/*.o kernel/time/*.o kernel/power/*.o
