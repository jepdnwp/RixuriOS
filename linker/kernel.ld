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
        __text_start = .;
        KEEP(*(.text.boot))
        *(.text*)
        __text_end = .;
    } :text

    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    } :rodata

    .data : ALIGN(4096) {
        __data_start = .;
        *(.data*)
        __data_end = .;
    } :data

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
