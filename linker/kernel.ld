ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(4);
}

SECTIONS
{
    . = 0x4000000;

    .text : ALIGN(4096) {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : ALIGN(4096) {
        *(.rodata*)
    } :rodata

    .data : ALIGN(4096) {
        *(.data*)
    } :data

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
