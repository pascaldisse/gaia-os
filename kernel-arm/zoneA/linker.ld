/* GAIA-OS Zone A — QEMU virt: RAM base 0x40000000, -kernel loads Image at base+text_offset */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    _load_base = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;

    _kernel_end = .;
    _kernel_size = _kernel_end - _load_base;
}
