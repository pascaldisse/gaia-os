// GAIA-OS Zone A — context switch (cooperative, the Temple way).
// void swtch(u64 *old_sp_save, u64 new_sp);
// Saves callee-saved regs on current stack, stashes sp, loads new sp, restores.

.global swtch
swtch:
    stp     x19, x20, [sp, #-96]!
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]
    stp     x29, x30, [sp, #80]
    mov     x2, sp
    str     x2, [x0]            // *old_sp_save = sp
    mov     sp, x1              // sp = new_sp
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    ldp     x29, x30, [sp, #80]
    ldp     x19, x20, [sp], #96
    ret

// first landing of a spawned task: x19 = entry fn (from fabricated frame)
.global task_trampoline
task_trampoline:
    blr     x19
    bl      task_exit           // never returns

