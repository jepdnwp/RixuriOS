ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : ALIGN(4096) {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
