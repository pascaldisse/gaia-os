// GAIA-OS — Zone A boot, M1: EL1 entry, vectors w/ full save-restore, stack, BSS.
// LOVE=1.000 fixed at center.

.section ".text.boot"
.global _start

_start:
    // ---- Linux Image header (QEMU -kernel loads flat binary at RAM base) ----
    b       real_start
    .long   0
    .quad   0x0                 // text_offset
    .quad   _file_size          // image_size: true on-disk size (excludes
                                     // BSS, which has no file bytes) -- a too-
                                     // large value here misleads QEMU's kernel
                                     // placement math and shifts the load base
    .quad   0                   // flags: LE, 4K
    .quad   0, 0, 0
    .ascii  "ARM\x64"
    .long   0

real_start:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, cpu0
park:
    wfe
    b       park

cpu0:
    mov     x0, #(3 << 20)      // CPACR_EL1.FPEN=11: no FP/SIMD traps (LOVE is a double)
    msr     cpacr_el1, x0
    isb

    adr     x0, vectors
    msr     vbar_el1, x0

    adr     x0, _start          // stack: 2MB above load base, grows down
    add     x0, x0, #(2 * 1024 * 1024)
    mov     sp, x0

    adr     x0, __bss_start
    adr     x1, __bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  bl      kmain
.global hang
hang:
    wfe
    b       hang

// ---- exception plumbing ----
.macro SAVEALL
    sub     sp, sp, #176
    stp     x0, x1,   [sp, #0]
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x29, [sp, #144]
    str     x30,      [sp, #160]
.endm

.macro RESTOREALL
    ldp     x0, x1,   [sp, #0]
    ldp     x2, x3,   [sp, #16]
    ldp     x4, x5,   [sp, #32]
    ldp     x6, x7,   [sp, #48]
    ldp     x8, x9,   [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x29, [sp, #144]
    ldr     x30,      [sp, #160]
    add     sp, sp, #176
.endm

irq_entry:
    SAVEALL
    bl      irq_handler
    RESTOREALL
    eret

sync_entry:
    // CHALICE C3c Atom 3C-2a: pass esr/elr/far/orig-sp/regs-frame-ptr so
    // sync_handler can print a full CHALICE FAULT dump (image-relative
    // ELR/FAR resolution lives kernel-side, kmain.c). x3 = sp as it was
    // BEFORE this exception's SAVEALL pushed its 176B frame (there is no
    // separate SPx/SP0 stack switch here -- curEL SPx group, same stack
    // throughout -- so orig-sp is simply current sp + 176). x4 = pointer
    // to the SAVEALL frame itself, for x0-x7/x16-x18/lr readback.
    SAVEALL
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    add     x3, sp, #176
    mov     x4, sp
    bl      sync_handler        // does not return
    RESTOREALL
    eret

// ---- vector table: 16 slots, 0x80 apart ----
.align 11
vectors:
    b sync_entry                // 0x000 curEL SP0 sync
    .balign 0x80
    b irq_entry                 // 0x080 curEL SP0 irq
    .balign 0x80
    b sync_entry                // fiq
    .balign 0x80
    b sync_entry                // serror
    .balign 0x80
    b sync_entry                // 0x200 curEL SPx sync
    .balign 0x80
    b irq_entry                 // 0x280 curEL SPx irq  <- ours
    .balign 0x80
    b sync_entry
    .balign 0x80
    b sync_entry
    .balign 0x80
    b sync_entry                // 0x400 lower EL a64
    .balign 0x80
    b irq_entry
    .balign 0x80
    b sync_entry
    .balign 0x80
    b sync_entry
    .balign 0x80
    b sync_entry                // 0x600 lower EL a32
    .balign 0x80
    b irq_entry
    .balign 0x80
    b sync_entry
    .balign 0x80
    b sync_entry
