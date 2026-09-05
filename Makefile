CROSS ?= x86_64-elf-
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

CFLAGS := -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/kernel.ld

OBJ := \
	kernel/boot.o \
	kernel/main.o \
	kernel/serial.o \
	kernel/arch/x86_64/gdt.o \
	kernel/arch/x86_64/idt.o \
	kernel/mm/pmm.o \
	kernel/mm/vmm.o \
	kernel/mm/heap.o

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
	rm -rf build kernel/*.o kernel/mm/*.o kernel/arch/x86_64/*.o
