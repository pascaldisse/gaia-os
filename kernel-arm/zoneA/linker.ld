/* GAIA-OS Zone A — QEMU virt: RAM base 0x40000000, but QEMU's arm64 "Image"
   boot loader (hw/arm/boot.c arm_load_kernel) reserves the first 2MB of RAM
   for the auto-generated DTB and always places a directly-booted kernel at
   RAM_base + 2MB regardless of the header's text_offset -- confirmed
   empirically (CHALICE C1: PC-relative &fn computations inside the guest
   read 0x402xxxxx while every baked absolute-address constant, e.g. a
   `static const` function-pointer table, stayed linked at 0x400xxxxx --
   a 0x200000 gap PC-relative code silently self-corrects across, masking
   the bug, but plain data constants don't). Link at the address QEMU
   actually uses so absolute constants match runtime reality too. */
ENTRY(_start)

SECTIONS
{
    . = 0x40200000;
    _load_base = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    /* CHALICE C1: mark end of actual FILE content (before .bss, which has
       no on-disk bytes) so boot.S can declare the Linux Image header's
       image_size field as the true file size instead of the full memsz
       (which wrongly includes BSS) -- correct per the Image header spec,
       independent of the base-address fix above. */
    _file_end = .;
    _file_size = _file_end - _load_base;

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;

    _kernel_end = .;
    _kernel_size = _kernel_end - _load_base;
}
