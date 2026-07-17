// CHALICE C2a Atom A — NT service-table dispatcher, single-address-space,
// no trap (HANDOFF: "the Temple-native gift" — Wine's ARM64 path never
// traps either, ref wine/dlls/ntdll/unix/signal_arm64.c:1630-1785; we mirror
// its table-dispatch shape but on one shared stack, since there is no
// user/kernel split here). Also carries pe_enter (A4 TEB-install shim) and
// nt_setjmp/nt_longjmp (NtTerminateProcess unwind primitive, ntsvc.c).
//
// Register discipline notes (deviations from donor called out inline):
//  - x16/x17 are AAPCS64 intra-procedure scratch (IP0/IP1) — never trusted
//    to survive a `blr`. x19-x28/x29 ARE callee-saved and safe across the
//    handler call as long as the handler (ntsvc.c, plain C) honors AAPCS64.
//  - x18 = TEB on the PE (Windows ABI) side, but our own kernel C
//    (aarch64-none-elf) may treat x18 as ordinary scratch — stash/restore
//    around the C handler call so the PE side never sees it clobbered.

.text

// ---- nt_dispatch: entry via `blr` from a PE-side stub (ntstub.S). ----
// x8 = table_idx<<12 | id (low 12 bits). x18 = TEB base.
.global nt_dispatch
nt_dispatch:
    mov  x9, sp                     // x9 = caller sp -> base of any AAPCS64
                                     // stack-passed args (single stack, no
                                     // privilege split: PE stub's caller
                                     // already pushed them here per its own
                                     // C compiler's calling convention).

    ldr  x19, [x18, #0x378]         // NT_TEB_SYSCALL_FRAME_OFF: reuse if set
    cbnz x19, 1f
    sub  x19, sp, #0x330            // carve sizeof(struct syscall_frame)
    str  x19, [x18, #0x378]
1:
    mov  sp, x19                    // frame becomes new top of stack

    stp  x0,  x1,  [x19, #0x00]     // syscall_frame x0..x28 @ 0 (spec layout)
    stp  x2,  x3,  [x19, #0x10]
    stp  x4,  x5,  [x19, #0x20]
    stp  x6,  x7,  [x19, #0x30]
    // DEVIATION vs donor (signal_arm64.c:1630+ spills all of x0-x28 and
    // q0-q31): Atom A only spills x0-x7 (the args every real handler here
    // needs) + fp/lr/sp/pc/cpsr/id. None of NtWriteFile/NtClose/
    // NtTerminateProcess touch x8-x28 or vector regs; NtContinue (Atom B,
    // full CONTEXT restore) will need the fuller spill and should extend
    // this. Noted in HANDOFF.md State.
    str  x29, [x19, #0xe8]          // frame->fp
    str  x9,  [x19, #0xf0]          // frame->sp  (caller's original sp)
    str  x30, [x19, #0x100]         // frame->pc  (return addr into PE stub)
    mrs  x10, NZCV
    str  w10, [x19, #0x108]         // frame->cpsr (condition flags only —
                                     // DEVIATION: donor stores full PSTATE
                                     // via separate mechanism; NZCV read
                                     // suffices for Atom A's no-trap model)
    and  x20, x8, #0xfff            // x20 = service id
    ubfx x21, x8, #12, #2           // x21 = table index (0..3)
    str  w20, [x19, #0x120]         // frame->id

    // ---- service table lookup: &nt_service_tables[table_idx], 32B/entry ----
    adrp x10, nt_service_tables
    add  x10, x10, :lo12:nt_service_tables
    add  x21, x10, x21, lsl #5
    ldr  x10, [x21, #16]            // ->ServiceLimit
    cmp  x20, x10
    bcs  bad_service

    // ---- stack-arg marshal (ArgumentTable[id] = total arg bytes) ----
    // Mirrors donor's byte-table convention exactly (signal_arm64.c:1697-1710).
    ldr  x10, [x21, #24]            // ->ArgumentTable
    ldrb w10, [x10, x20]            // total bytes this call needs
    subs x10, x10, #64              // bytes beyond x0..x7 (8 regs * 8B)
    bls  have_args
    add  x11, x10, #15
    and  x11, x11, #-16
    sub  sp, sp, x11
    mov  x12, sp
copy_args:
    subs x10, x10, #8
    ldr  x13, [x9, x10]
    str  x13, [x12, x10]
    bne  copy_args
have_args:
    ldr  x10, [x21]                 // ->ServiceTable
    ldr  x10, [x10, x20, lsl #3]    // handler fn ptr
    ldp  x0, x1, [x19, #0x00]       // reload x0..x7 fresh from the frame
    ldp  x2, x3, [x19, #0x10]
    ldp  x4, x5, [x19, #0x20]
    ldp  x6, x7, [x19, #0x30]
    mov  x22, x18                   // stash TEB (x18 discipline, see header)
    blr  x10
    mov  x18, x22                   // restore TEB before returning to PE side
    b    dispatch_return
bad_service:
    mov  x0, #0xc0000000
    movk x0, #0x001c                // STATUS_INVALID_SYSTEM_SERVICE
dispatch_return:
    // reload frame ptr via TEB (x19 is callee-saved so would also survive,
    // but reload for symmetry with donor's __wine_syscall_dispatcher_return
    // path, which always re-derives from a known-good pointer).
    ldr  x10, [x18, #0x378]
    ldr  x16, [x10, #0x100]         // frame->pc  ("ret x16" pattern, donor)
    ldr  x17, [x10, #0xf0]          // frame->sp
    mov  sp, x17
    str  xzr, [x18, #0x378]         // Atom A: one-shot frame, no re-entrancy
    ret  x16

// ---- pe_enter: TEB-install shim (A4). long pe_enter(long (*entry)(void), void *teb) ----
// x0 = entry fn ptr, x1 = teb.
.global pe_enter
pe_enter:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x9, x18                    // save kernel-side x18 (whatever our own
                                     // aarch64-none-elf caller left there)
    mov  x18, x1                    // x18 = TEB (Windows ARM64 ABI convention)
    blr  x0
    mov  x18, x9                    // restore kernel x18 (normal-return path;
                                     // the NtTerminateProcess path instead
                                     // restores x18 via nt_longjmp below)
    ldp  x29, x30, [sp], #16
    ret

// ---- nt_setjmp/nt_longjmp: minimal internal-use longjmp for
// NtTerminateProcess's "unwind to PeRun w/ exit code" (A3). Saves x18 too
// (not just x19-x30/sp) so a longjmp from inside a PE call (x18=TEB) lands
// back in PeRun with the kernel's own pre-entry x18 restored automatically
// — pe_enter's own restore is skipped on this path, this is what covers it.
// 14 slots * 8B = 112B buffer: x18,x19,...,x28,x29,x30,sp.
.global nt_setjmp
nt_setjmp:
    stp  x18, x19, [x0, #0]
    stp  x20, x21, [x0, #16]
    stp  x22, x23, [x0, #32]
    stp  x24, x25, [x0, #48]
    stp  x26, x27, [x0, #64]
    stp  x28, x29, [x0, #80]
    mov  x1, sp
    stp  x30, x1, [x0, #96]
    mov  x0, #0
    ret

.global nt_longjmp
nt_longjmp:
    ldp  x18, x19, [x0, #0]
    ldp  x20, x21, [x0, #16]
    ldp  x22, x23, [x0, #32]
    ldp  x24, x25, [x0, #48]
    ldp  x26, x27, [x0, #64]
    ldp  x28, x29, [x0, #80]
    ldp  x30, x2,  [x0, #96]
    mov  sp, x2
    mov  x0, x1                     // val becomes nt_setjmp's "return value"
    ret
