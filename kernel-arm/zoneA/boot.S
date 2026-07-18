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

// CHALICE C4-18: sync frame = x0-x30 (0x100) + q0-q31 (0x200), 16-aligned.
// A guest exception must round-trip the COMPLETE ARM64 CONTEXT; q values are
// captured before C can touch caller-saved SIMD state.  q0 begins at +0x100.
.macro SAVEALL_SYNC
    sub     sp, sp, #768
    stp     x0, x1,   [sp, #0]
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]
    stp     q0, q1,   [sp, #256]
    stp     q2, q3,   [sp, #288]
    stp     q4, q5,   [sp, #320]
    stp     q6, q7,   [sp, #352]
    stp     q8, q9,   [sp, #384]
    stp     q10,q11,  [sp, #416]
    stp     q12,q13,  [sp, #448]
    stp     q14,q15,  [sp, #480]
    stp     q16,q17,  [sp, #512]
    stp     q18,q19,  [sp, #544]
    stp     q20,q21,  [sp, #576]
    stp     q22,q23,  [sp, #608]
    stp     q24,q25,  [sp, #640]
    stp     q26,q27,  [sp, #672]
    stp     q28,q29,  [sp, #704]
    stp     q30,q31,  [sp, #736]
.endm

.macro RESTOREALL_SYNC
    ldp     q0, q1,   [sp, #256]
    ldp     q2, q3,   [sp, #288]
    ldp     q4, q5,   [sp, #320]
    ldp     q6, q7,   [sp, #352]
    ldp     q8, q9,   [sp, #384]
    ldp     q10,q11,  [sp, #416]
    ldp     q12,q13,  [sp, #448]
    ldp     q14,q15,  [sp, #480]
    ldp     q16,q17,  [sp, #512]
    ldp     q18,q19,  [sp, #544]
    ldp     q20,q21,  [sp, #576]
    ldp     q22,q23,  [sp, #608]
    ldp     q24,q25,  [sp, #640]
    ldp     q26,q27,  [sp, #672]
    ldp     q28,q29,  [sp, #704]
    ldp     q30,q31,  [sp, #736]
    ldp     x0, x1,   [sp, #0]
    ldp     x2, x3,   [sp, #16]
    ldp     x4, x5,   [sp, #32]
    ldp     x6, x7,   [sp, #48]
    ldp     x8, x9,   [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30,      [sp, #240]
    add     sp, sp, #768
.endm

irq_entry:
    // CHALICE C3c Atom 3C-2b: pass elr_el1 (the interrupted PC) to
    // irq_handler so a PC-sampling profiler can be driven from the
    // timer IRQ -- mirrors sync_entry's register-capture pattern below.
    SAVEALL
    mrs     x0, elr_el1
    bl      irq_handler
    RESTOREALL
    eret

sync_entry:
    // CHALICE C3c Atom 3C-2a: pass esr/elr/far/orig-sp/regs-frame-ptr so
    // sync_handler can print a full CHALICE FAULT dump (image-relative
    // ELR/FAR resolution lives kernel-side, kmain.c). x3 = sp as it was
    // BEFORE this exception's frame push (there is no separate SPx/SP0
    // stack switch here -- curEL SPx group, same stack throughout -- so
    // orig-sp is simply current sp + frame size). x4 = pointer to the
    // frame itself, for full x0-x30 readback.
    SAVEALL_SYNC
    // Advisory SVC -> demand-commit are transparent, ordered before NT SEH.
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mov     x2, sp
    bl      sync_try_fex_linux_hint
    cbnz    x0, 2f
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    bl      sync_try_arena_demand_commit
    cbnz    x0, 2f

    // Guest-owned non-arena fault -> stack {CONTEXT, CONTEXT_EX, RECORD,
    // redzone}, ELR=KiUserExceptionDispatcher. x0 returns its new guest SP.
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    add     x3, sp, #768
    mov     x4, sp
    bl      sync_try_guest_exception
    cbz     x0, 1f
    ldr     x18, [sp, #144]     // dispatcher needs the interrupted TEB
    mov     sp, x0
    // C4-19: this is a same-EL flat floor, not a real exception return to a
    // lower privilege.  ERET retained the synchronous fault PC under QEMU;
    // a direct branch is the required control transfer into ntdll.
    adrp    x16, nt_exception_dispatch_trace
    add     x16, x16, :lo12:nt_exception_dispatch_trace
    br      x16
2:
    RESTOREALL_SYNC
    eret
1:
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    add     x3, sp, #768
    mov     x4, sp
    bl      sync_handler        // kernel fault / unknown guest syndrome: fatal

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
