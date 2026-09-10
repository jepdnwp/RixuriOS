CROSS ?= x86_64-elf-
HOST_CC ?= gcc
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
OBJDUMP := $(CROSS)objdump
CFLAGS := -std=c17 -ffreestanding -fno-stack-protector -fno-pie -fcf-protection=none -mno-red-zone -m64 -Wall -Wextra -Werror -O2 -Iinclude -Ibuild
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld
OBJ := kernel/boot.o kernel/main.o kernel/serial.o kernel/user_init_blob.o \
 kernel/arch/x86_64/cpu.o kernel/arch/x86_64/gdt.o kernel/arch/x86_64/idt.o kernel/arch/x86_64/interrupts.o kernel/arch/x86_64/irq.o kernel/arch/x86_64/apic.o kernel/arch/x86_64/acpi.o kernel/arch/x86_64/ioapic.o kernel/arch/x86_64/pic.o kernel/arch/x86_64/pit.o kernel/arch/x86_64/ps2_keyboard.o kernel/arch/x86_64/user_entry.o kernel/arch/x86_64/cr3.o \
 kernel/pci/pci.o kernel/pci/dma.o kernel/pci/iommu.o kernel/pci/msix.o kernel/sched/scheduler.o kernel/sched/switch.o kernel/process/process.o kernel/process/signal.o kernel/process/address_space.o kernel/syscall/syscall.o kernel/vfs/vfs.o kernel/fs/rixfs.o kernel/fs/rixfs_ops.o kernel/fs/rixfs_dir.o kernel/fs/rixfs_fsck.o kernel/elf/elf.o kernel/elf/loader.o \
 kernel/mm/pmm.o kernel/mm/vmm.o kernel/mm/ptmap.o kernel/mm/uaccess.o kernel/mm/heap.o kernel/sync/lock.o kernel/sync/waitqueue.o kernel/ipc/channel.o kernel/ipc/pipe.o kernel/ipc/shared_memory.o kernel/tty/tty.o \
         kernel/storage/block.o kernel/storage/block_cache.o kernel/storage/nvme.o kernel/net/net.o kernel/net/ethernet.o kernel/net/arp.o kernel/net/ipv4.o kernel/net/ipv6.o kernel/net/udp.o kernel/net/tcp.o kernel/net/loopback.o kernel/net/socket.o kernel/net/device.o kernel/net/stack.o kernel/net/dhcp.o kernel/net/rtl8125.o kernel/net/e1000.o kernel/usb/xhci.o kernel/usb/usb.o kernel/usb/hid.o kernel/time/rtc.o kernel/time/time.o kernel/power/power.o kernel/tty/font_psf.o

PROGRAM_NAMES := echo cat args grep true false sleep ls mkdir rm rmdir touch stat ln head tail wc cut tr sort uniq env printf pwd which kill ps uname du cp mv find xargs sed test tee basename dirname seq id whoami date ping curl host help hostname credtest auditcheck capdelegatecheck capdelegatetest accountctl sessiontest sessionlisttest killtest metatest renametest authcheck abi-negative proc-test pipe-stress rixtest posix-test
PROGRAM_ELFS := $(addprefix build/programs/,$(addsuffix .elf,$(PROGRAM_NAMES)))
PROGRAM_START_OBJ := build/programs/start.o

.PHONY: all clean check image iso-test powerloss-test test-all run qemu build-run test user-init programs rixfs-image usb-test hid-test tty-test shell-test pipe-test net-test hosts-test rtl-test e1000-test acpi-test rixfs-mount-test auth-test phase20-test ring3-test sysroot
all: build/kernel.elf

build:
	mkdir -p build

FORCE:

build/build_id.h: FORCE | build
	printf '#pragma once\n#define RIXURI_BUILD_ID "%s"\n' "$$(git rev-parse --short HEAD 2>/dev/null)$$(git diff --quiet 2>/dev/null || echo -dirty)-$$(date -u +%Y%m%d-%H%M%S)" > $@

kernel/main.o: build/build_id.h

USER_INIT_CFLAGS := $(CFLAGS) -mcmodel=large -Iuser/shell -Iuser/libc/include
build/user_init.o: user/init.c user/shell/shell.h user/libc/include/unistd.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_shell.o: user/shell/shell.c user/shell/shell.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_unistd.o: user/libc/src/unistd.c user/libc/include/unistd.h user/libc/include/signal.h user/libc/include/fcntl.h user/libc/include/time.h user/libc/include/errno.h user/libc/include/string.h user/libc/include/sys/mman.h user/libc/include/sys/socket.h user/libc/include/netinet/in.h user/libc/include/poll.h user/libc/include/sys/ioctl.h user/libc/include/sys/stat.h user/libc/include/sys/types.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_libc.o: user/libc/src/libc.c user/libc/include/string.h user/libc/include/stdlib.h user/libc/include/errno.h user/libc/include/ctype.h user/libc/include/stdio.h user/libc/include/fcntl.h user/libc/include/dirent.h user/libc/include/sys/stat.h user/libc/include/time.h user/libc/include/stddef.h user/libc/include/stdint.h user/libc/include/signal.h user/libc/include/sys/time.h user/libc/include/sys/types.h user/libc/include/sys/socket.h user/libc/include/netinet/in.h user/libc/include/arpa/inet.h user/libc/include/pthread.h user/libc/include/locale.h user/libc/include/wchar.h user/libc/include/unistd.h | build
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/user_init.elf: build/user_init.o build/user_shell.o build/user_unistd.o build/user_libc.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ build/user_init.o build/user_shell.o build/user_unistd.o build/user_libc.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
kernel/user_init_blob.o: build/user_init.elf | build
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	$(OBJCOPY) --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=readonly,contents $@
kernel/tty/font_psf.o: assets/fonts/terminus-12x24.psf
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@
	$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents $@
user-init: build/user_init.elf

build/programs/%.o: user/programs/%.c user/libc/include/unistd.h user/programs/copy_metadata.h user/programs/auth_crypto.h user/programs/hosts.h | build
	mkdir -p build/programs
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/programs/hosts.o: user/programs/hosts.c user/programs/hosts.h user/libc/include/unistd.h | build
	mkdir -p build/programs
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/programs/start.o: user/programs/start.S | build
	mkdir -p build/programs
	$(CC) $(USER_INIT_CFLAGS) -c $< -o $@
build/programs/%.elf: build/programs/%.o $(PROGRAM_START_OBJ) build/user_unistd.o build/user_libc.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ $(PROGRAM_START_OBJ) build/programs/$*.o build/user_unistd.o build/user_libc.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
build/programs/curl.elf: build/programs/curl.o build/programs/hosts.o $(PROGRAM_START_OBJ) build/user_unistd.o build/user_libc.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ $(PROGRAM_START_OBJ) build/programs/curl.o build/programs/hosts.o build/user_unistd.o build/user_libc.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
build/programs/host.elf: build/programs/host.o build/programs/hosts.o $(PROGRAM_START_OBJ) build/user_unistd.o build/user_libc.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ $(PROGRAM_START_OBJ) build/programs/host.o build/programs/hosts.o build/user_unistd.o build/user_libc.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
build/programs/ping.elf: build/programs/ping.o build/programs/hosts.o $(PROGRAM_START_OBJ) build/user_unistd.o build/user_libc.o user/init.ld | build
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/init.ld -o $@ $(PROGRAM_START_OBJ) build/programs/ping.o build/programs/hosts.o build/user_unistd.o build/user_libc.o
	$(READELF) -h $@ >/dev/null
	$(READELF) -l $@ >/dev/null
programs: $(PROGRAM_ELFS)

build/rixfs.img: programs scripts/build-rixfs-image.py etc/hosts etc/hostname etc/resolv.conf | build
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
			--file /bin/rmdir=build/programs/rmdir.elf \
			--file /bin/touch=build/programs/touch.elf \
			--file /bin/stat=build/programs/stat.elf \
			--file /bin/ln=build/programs/ln.elf \
			--file /bin/head=build/programs/head.elf \
			--file /bin/tail=build/programs/tail.elf \
			--file /usr/bin/wc=build/programs/wc.elf \
			--file /usr/bin/cut=build/programs/cut.elf \
			--file /usr/bin/tr=build/programs/tr.elf \
			--file /usr/bin/sort=build/programs/sort.elf \
			--file /usr/bin/uniq=build/programs/uniq.elf \
			--file /usr/bin/env=build/programs/env.elf \
			--file /usr/bin/printf=build/programs/printf.elf \
			--file /bin/pwd=build/programs/pwd.elf \
			--file /usr/bin/which=build/programs/which.elf \
			--file /usr/bin/kill=build/programs/kill.elf \
			--file /usr/bin/ps=build/programs/ps.elf \
			--file /usr/bin/uname=build/programs/uname.elf \
			--file /usr/bin/du=build/programs/du.elf \
			--file /bin/cp=build/programs/cp.elf \
			--file /bin/mv=build/programs/mv.elf \
			--file /usr/bin/find=build/programs/find.elf \
			--file /usr/bin/xargs=build/programs/xargs.elf \
			--file /usr/bin/sed=build/programs/sed.elf \
				--file /bin/test=build/programs/test.elf \
				--file /usr/bin/tee=build/programs/tee.elf \
				--file /usr/bin/basename=build/programs/basename.elf \
				--file /usr/bin/dirname=build/programs/dirname.elf \
				--file /usr/bin/seq=build/programs/seq.elf \
				--file /usr/bin/id=build/programs/id.elf \
				--file /usr/bin/whoami=build/programs/whoami.elf \
					--file /bin/date=build/programs/date.elf \
					--file /usr/bin/ping=build/programs/ping.elf \
					--file /usr/bin/curl=build/programs/curl.elf \
				--file /usr/bin/credtest=build/programs/credtest.elf \
					--file /usr/bin/auditcheck=build/programs/auditcheck.elf \
					--file /usr/bin/capdelegatecheck=build/programs/capdelegatecheck.elf \
					--file /usr/bin/capdelegatetest=build/programs/capdelegatetest.elf \
					--file /usr/bin/accountctl=build/programs/accountctl.elf \
					--file /usr/bin/sessiontest=build/programs/sessiontest.elf \
				--file /usr/bin/sessionlisttest=build/programs/sessionlisttest.elf \
				--file /usr/bin/killtest=build/programs/killtest.elf \
				--file /usr/bin/metatest=build/programs/metatest.elf \
				--file /usr/bin/renametest=build/programs/renametest.elf \
				--file /usr/bin/authcheck=build/programs/authcheck.elf \
				--file /etc/passwd=etc/passwd \
				--file /etc/shadow=etc/shadow \
				--file /etc/hosts=etc/hosts \
				--file /etc/hostname=etc/hostname \
				--file /etc/resolv.conf=etc/resolv.conf \
				--file /usr/bin/host=build/programs/host.elf \
				--file /usr/bin/help=build/programs/help.elf \
				--file /usr/bin/hostname=build/programs/hostname.elf \
				--dir /dev \
				--dir /var \
				--dir /var/log \
				--dir /var/tmp \
				--dir /tmp \
				--dir /proc \
				--dir /sys \
				--dir /home \
				--file /usr/bin/abi-negative=build/programs/abi-negative.elf \
		--file /usr/bin/posix-test=build/programs/posix-test.elf \
		--file /usr/bin/proc-test=build/programs/proc-test.elf \
		--file /usr/bin/pipe-stress=build/programs/pipe-stress.elf \
		--file /usr/bin/rixtest=build/programs/rixtest.elf \
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
iso: image
		bash ./scripts/build-iso.sh
iso-test: iso
		python3 scripts/qemu_iso_boot_test.py
powerloss-test: image
		python3 scripts/qemu_powerloss_test.py
test-all:
		bash ./scripts/run-all-tests.sh
run: image
	 RIXURI_QEMU_DISPLAY=$${RIXURI_QEMU_DISPLAY:-gtk} bash ./scripts/run-qemu.sh
qemu: run
auth-test: image
		python3 scripts/qemu_auth_test.py
phase20-test: image
		python3 scripts/qemu_phase20_cred_test.py
		python3 scripts/qemu_session_test.py
		python3 scripts/qemu_auth_test.py
ring3-test: image
		python3 scripts/qemu_ring3_test.py

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
		$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DRIX_HOST_TEST -I. tests/tty_test.c kernel/tty/tty.c -o build/tty_test
	build/tty_test
shell-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/shell_test.c user/shell/shell.c -o build/shell_test
		build/shell_test

pipe-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DRIX_HOST_TEST -I. tests/pipe_test.c kernel/ipc/channel.c kernel/ipc/pipe.c kernel/sync/lock.c -o build/pipe_test
		build/pipe_test

net-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DRIX_HOST_TEST -I. -Iinclude tests/net_test.c kernel/net/net.c kernel/net/ethernet.c kernel/net/arp.c kernel/net/ipv4.c kernel/net/udp.c kernel/net/ipv6.c kernel/net/tcp.c kernel/net/loopback.c kernel/net/socket.c kernel/net/dhcp.c -o build/net_test
			build/net_test
libc-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -fno-builtin -DRIX_HOST_TEST -Iuser/libc/include tests/libc_test.c user/libc/src/libc.c -o build/libc_test
	build/libc_test

hosts-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. -Iuser/programs -Iuser/libc/include tests/hosts_test.c user/programs/hosts.c -o build/hosts_test
	build/hosts_test

rtl-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -DRIX_HOST_TEST -I. tests/rtl8125_test.c kernel/net/rtl8125.c -o build/rtl8125_test
		build/rtl8125_test

acpi-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/acpi_test.c kernel/arch/x86_64/acpi.c -o build/acpi_test
		build/acpi_test

rixfs-mount-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/rixfs_mount_test.c kernel/fs/rixfs.c -o build/rixfs_mount_test
		build/rixfs_mount_test

symlink-test: | build
	@if [ -f tests/symlink_test.c ]; then \
		$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/symlink_test.c kernel/fs/rixfs.c kernel/fs/rixfs_ops.c kernel/fs/rixfs_dir.c kernel/fs/rixfs_fsck.c -o build/symlink_test && \
		build/symlink_test; \
	else \
		echo "symlink tests: SKIPPED (tests/symlink_test.c not present)"; \
	fi

e1000-test: | build
	$(HOST_CC) -std=c17 -Wall -Wextra -Werror -I. tests/e1000_test.c kernel/net/e1000.c -o build/e1000_test
		build/e1000_test

test: check usb-test hid-test tty-test shell-test pipe-test net-test libc-test hosts-test rtl-test e1000-test acpi-test rixfs-mount-test symlink-test
	@echo 'Static kernel build checks completed.'

sysroot:
	bash ./scripts/musl-sysroot.sh

clean:
	rm -rf build kernel/*.o kernel/mm/*.o kernel/arch/x86_64/*.o kernel/pci/*.o kernel/sched/*.o kernel/process/*.o kernel/syscall/*.o kernel/vfs/*.o kernel/fs/*.o kernel/elf/*.o kernel/sync/*.o kernel/storage/*.o kernel/usb/*.o kernel/ipc/*.o kernel/tty/*.o kernel/time/*.o kernel/power/*.o
