// CHALICE C2a Atom A — NT service-table dispatcher, single-address-space,
// no trap (HANDOFF: "the Temple-native gift" — Wine's ARM64 path never
// traps either, ref wine/dlls/ntdll/unix/signal_arm64.c:1630-1785; we mirror
// its table-dispatch shape but on one shared stack, since there is no
// user/kernel split here). Also carries pe_enter (A4 TEB-install shim),
// nt_setjmp/nt_longjmp (NtTerminateProcess unwind primitive, ntsvc.c), and
// (Atom B) NtContinue (full CONTEXT restore, ntids.h receipt).
//
// Register discipline notes (deviations from donor called out inline):
//  - x16/x17 are AAPCS64 intra-procedure scratch (IP0/IP1) — never trusted
//    to survive a `blr`. x19-x28/x29 ARE callee-saved and safe across the
//    handler call as long as the handler (ntsvc.c, plain C) honors AAPCS64.
//  - x18 = TEB on the PE (Windows ABI) side, but our own kernel C
//    (aarch64-none-elf) may treat x18 as ordinary scratch — stash/restore
//    around the C handler call so the PE side never sees it clobbered.
//
// ---- Atom B correctness fix: stub-side x30 (LR) preservation ----
// Task-flagged bug: Atom A's PE-side stubs (ntstub.S) stashed their own
// pre-blr x30 into x23 — CALLEE-SAVED per the Windows ARM64 AAPCS64. A
// stub squatting on a callee-saved register without saving/restoring the
// CALLER's incoming value violates the stub's own ABI contract with
// whatever compiler-generated code calls it (latent corruption for any
// real caller, not just our own hand-written hello3.c). Verified against
// the donor return-path shape (wine/dlls/ntdll/unix/signal_arm64.c:1630-
// 1785): donor's __wine_syscall_dispatcher entry does
//   `mov x19, sp` ; `stp x9, x19, [x10, #0xf0]`   (frame+0xf0=x9, +0xf8=sp)
//   ... and on return: `ldp x30, x17, [sp, #0xf0]` ; `mov sp, x17` ; `ret x16`
// i.e. x30 is restored from frame+0xf0 = whatever the donor's real PE-side
// stub placed in (caller-saved) x9 before its `blr` — donor's stub is a
// PURE TAIL CALL (nothing after the blr at all: wine/include/wine/asm.h
// __ASM_SYSCALL_FUNC's aarch64 branch, quoted in ntids.h) so frame->pc
// (dispatcher-entry x30) IS the real original-caller return address, and
// `ret x16` skips the stub's own frame entirely. Our stubs are NOT pure
// tail calls (each has cleanup after the blr), so we keep frame->pc's role
// (return INTO the stub) but ALSO restore the true x30 via a GAIA-OS
// extension slot (NT_FRAME_STUB_LR_OFF, ntids.h receipt) — functionally
// the same donor mechanism (capture a caller-saved temp into the frame at
// entry, before the C handler can clobber it; restore into x30 at return),


// ---- CHALICE C3c-2d: real-Wine-stub calling convention ----
// Stock Wine ARM64 syscall stubs (decoded from stock ntdll.dll) are:
//   `mov x8,#num ; mov x9,x30 ; ldr x16,[disp] ; blr x16 ; ret` -- caller LR
// travels in **x9**, not the x14 this floor used pre-C3c-2d (that was our
// own stub-only convention, ntstub.S, never matched what stock ntdll's PE
// image actually contains -- x30 on return was wrong for any stock stub,
// so its trailing `ret` bounced to itself). Fix: nt_dispatch now stashes
// the incoming x9 (real caller LR) first thing, before x9 gets reused below
// as the stack-arg base, into the same NT_FRAME_STUB_LR_OFF frame slot the
// old x14 path used -- return-path restore (dispatch_return) is unchanged.
// ntstub.S (our OWN PE-side stubs) migrated to match: `mov x9, x30` before
// `blr x16`, same as a stock stub. This is the same donor mechanism the
// prior Atom-B paragraph above describes, just on the donor's own register.

.text

// ---- nt_dispatch: entry via `blr` from a PE-side stub (ntstub.S). ----
// x8 = table_idx<<12 | id (low 12 bits). x18 = TEB base.
.global nt_dispatch
nt_dispatch:
    mov  x11, x9                    // stash real caller LR (x9, the ACTUAL
                                     // Windows-ABI stub convention — see
                                     // header C3c-2d note) before the
                                     // stack-arg-base clobber below
    mov  x9, sp                     // x9 = caller sp -> base of any AAPCS64
                                     // stack-passed args (single stack, no
                                     // privilege split: PE stub's caller
                                     // already pushed them here per its own
                                     // C compiler's calling convention).

    // ---- CHALICE C4-48: nested-syscall-safe frame allocation (donor idiom:
    // wine/dlls/ntdll/unix/signal_arm64.c's syscall_frame is carved fresh
    // from the thread stack (frame = sp - frame_size) on EVERY dispatcher
    // entry -- nesting is then automatic, since a nested call's own sp is
    // already lower than the enclosing call's, so its fresh carve can never
    // collide with the still-live outer frame). Replaces Atom A's one-shot
    // `cbnz x10,1f` reuse-or-carve-once check (C4-47: unsafe for nesting --
    // reused the STALE outer frame's address instead of carving fresh space
    // below the current, now-lower, sp, corrupting the in-flight outer call).
    // 0x330B layout is UNCHANGED -- only the allocation policy changes: we
    // ALWAYS carve (never reuse), and chain to whatever frame was already
    // live via a new prev-link slot (NT_FRAME_PREV_OFF, the 8 free bytes
    // between stub_lr@0x110 and id@0x120), restored (not zeroed) on return
    // -- a minimal LIFO chain. At top level [x18+0x378] is always 0 before
    // entry, so frame->prev==0 and dispatch_return's restore writes back 0 --
    // BIT-FOR-BIT identical to today's `str xzr,...` for the non-nested case.
    ldr  x12, [x18, #0x378]         // x12 = currently-live frame ptr (0 at
                                     // top level; the OUTER frame's ptr if
                                     // this dispatch is nested inside it)
    sub  x10, sp, #0x330            // fresh frame, always carved below THIS
                                     // call's own (lower, if nested) sp
    str  x12, [x10, #0x118]         // frame->prev (NT_FRAME_PREV_OFF) = the
                                     // captured previous value -- LIFO link
    str  x10, [x18, #0x378]         // publish new frame as current
    // ---- CHALICE C3c-2e p2: PE-side callee-saved x19-x28 preservation.
    // Root-caused via disasm (stock ntdll.dll allocate_region, heap.c):
    // real ARM64 `svc` leaves EVERY GPR except x0 exactly as the caller
    // left it (no real trap here -- this dispatcher is a `blr` target, so
    // it must emulate that contract by hand). x19-x24 below are used as
    // this dispatcher's OWN internal scratch (frame ptr, service id, table
    // index/entry, handler-ptr stash, TEB stash) WITHOUT ever being saved
    // -- a caller that keeps its own locals in x19-x24 across two
    // back-to-back syscalls (stock allocate_region does exactly this: its
    // own x19=commit_size ptr, x21=Protect value, live across the
    // RESERVE-then-COMMIT call pair) gets those locals silently
    // overwritten with dispatcher internals (a frame-pointer kernel
    // address landed in the caller's x19; &nt_service_tables[idx] landed
    // in the caller's x21) -- confirmed against the pe6 trace: call #2's
    // *RegionSize read back as -1 and Protect read back as 0x4021D780 (a
    // live kernel .data address, matching &nt_service_tables[0] range),
    // both exactly the dispatcher's own x19/x21 scratch values. Fix: save
    // the INCOMING x19-x28 into the frame (using x10, not yet repurposed,
    // as frame base) before ANY of them are touched; dispatch_return
    // restores them verbatim, so the PE caller sees x19-x28 completely
    // unchanged across the call, exactly like a real `svc`.
    stp  x19, x20, [x10, #0x130]
    stp  x21, x22, [x10, #0x140]
    stp  x23, x24, [x10, #0x150]
    stp  x25, x26, [x10, #0x160]
    stp  x27, x28, [x10, #0x170]
    mov  x19, x10                   // NOW safe to repurpose x19 as frame ptr
    mov  sp, x19                    // frame becomes new top of stack

    stp  x0,  x1,  [x19, #0x00]     // syscall_frame x0..x28 @ 0 (spec layout)
    stp  x2,  x3,  [x19, #0x10]
    stp  x4,  x5,  [x19, #0x20]
    stp  x6,  x7,  [x19, #0x30]
    // DEVIATION vs donor (signal_arm64.c:1630+ spills all of x0-x28 and
    // q0-q31): Atom A only spills x0-x7 (the args every real handler here
    // needs) + fp/lr/sp/pc/cpsr/id, PLUS (C3c-2e p2) x19-x28 for PE-caller
    // preservation (see note above -- this closes that gap; x8-x18/x29-x30
    // are covered separately: x8 is an id-carrying scratch reg by the NT
    // ABI itself, not caller-preserved; x18=TEB is the dispatcher's own
    // domain; fp/pc already spilled below; x9-x17 are AAPCS64 caller-saved,
    // never guaranteed to survive any call including a real `svc`-adjacent
    // sequence). None of NtWriteFile/NtClose/NtTerminateProcess touch
    // x8-x18 or vector regs. Atom B's NtContinue also fits this spill range
    // unchanged: its only real arguments are x0 (ContextRecord ptr) and x1
    // (TestAlert), both within x0-x7 -- the full x0-x30 restore it performs
    // reads directly from *ContextRecord in memory, not from this frame's
    // spilled registers.
    str  x29, [x19, #0xe8]          // frame->fp
    str  x9,  [x19, #0xf0]          // frame->sp  (caller's original sp)
    str  x30, [x19, #0x100]         // frame->pc  (return addr into PE stub)
    str  x11, [x19, #0x110]         // frame->stub_lr (C3c-2d: real x9 caller
                                     // LR, stashed into x11 above before the
                                     // stack-arg-base clobber -- restored
                                     // into the real x30 at dispatch_return)
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

    // ---- CHALICE C3c Atom 3C-1: dispatcher call-log hook (ntsvc.c
    // nt_dispatch_log, runtime-flag-gated -- see ntsvc.h). Called BEFORE
    // the x0..x7 frame reload below (so no need to preserve those, they're
    // reloaded fresh from the frame afterward regardless). x23 stashes the
    // resolved handler ptr across the call (x10 is caller-saved, would not
    // survive); x24 stashes x18 (TEB) -- our own kernel C is free to treat
    // x18 as ordinary scratch (file header note), so a plain `bl` into it
    // is not safe to assume TEB-preserving, unlike the PE-side `blr x10`
    // below which gets the deliberate x22 stash/restore for the same reason.
    // x19/x20/x21 (frame/id/table-entry) are callee-saved -- an AAPCS64-
    // compliant C callee preserves them automatically, no manual stash
    // needed for those three. ----
    mov  x23, x10                   // stash handler ptr
    mov  x24, x18                   // stash TEB
    mov  x0, x20                    // arg0 = service id
    mov  x1, x10                    // arg1 = handler ptr (0 never reaches here)
    bl   nt_dispatch_log
    mov  x18, x24                   // restore TEB
    mov  x10, x23                   // restore handler ptr

    ldp  x0, x1, [x19, #0x00]       // reload x0..x7 fresh from the frame
    ldp  x2, x3, [x19, #0x10]
    ldp  x4, x5, [x19, #0x20]
    ldp  x6, x7, [x19, #0x30]
    mov  x22, x18                   // stash TEB (x18 discipline, see header)
    blr  x10
    mov  x18, x22                   // restore TEB before returning to PE side
    b    dispatch_return
bad_service:
    // ---- same call-log hook, "unknown" sentinel (handler=0 -- id was
    // beyond ServiceLimit, no handler ever resolved). Always logged
    // (nt_dispatch_log's own unknown/unimplemented-bypass-the-counter
    // rule), not just the first NT_DISPATCH_LOG_FIRST_N calls. ----
    mov  x24, x18
    mov  x0, x20
    mov  x1, #0
    bl   nt_dispatch_log
    mov  x18, x24
    mov  x0, #0xc0000000
    movk x0, #0x001c                // STATUS_INVALID_SYSTEM_SERVICE
dispatch_return:
    // reload frame ptr via TEB (x19 is callee-saved so would also survive,
    // but reload for symmetry with donor's __wine_syscall_dispatcher_return
    // path, which always re-derives from a known-good pointer).
    ldr  x10, [x18, #0x378]
    ldr  x16, [x10, #0x100]         // frame->pc  ("ret x16" pattern, donor)
    ldr  x17, [x10, #0xf0]          // frame->sp
    ldr  x30, [x10, #0x110]         // frame->stub_lr (Atom B fix, see header)
    // CHALICE C3c-2e p2: restore the PE caller's original x19-x28 verbatim
    // (saved at entry above) -- must happen before `mov sp,x17` clobbers
    // our own x19 (this dispatcher's frame ptr, no longer needed past this
    // point) and before the frame slot is zeroed.
    ldp  x19, x20, [x10, #0x130]
    ldp  x21, x22, [x10, #0x140]
    ldp  x23, x24, [x10, #0x150]
    ldp  x25, x26, [x10, #0x160]
    ldp  x27, x28, [x10, #0x170]
    // CHALICE C4-48: pop the LIFO frame chain -- restore whatever frame
    // ptr was live BEFORE this one (frame->prev, NT_FRAME_PREV_OFF) instead
    // of unconditionally zeroing (Atom A's old one-shot-only behavior).
    // At top level frame->prev is always 0 (nothing was live before us),
    // so this restores 0 -- IDENTICAL to the old `str xzr,...` there. If
    // this call was nested, frame->prev is the enclosing OUTER frame's
    // still-valid ptr, correctly re-published so the OUTER dispatch's own
    // eventual dispatch_return finds ITS frame intact, not zeroed early.
    ldr  x11, [x10, #0x118]         // frame->prev
    mov  sp, x17
    str  x11, [x18, #0x378]
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

// ---- pe_enter1: CHALICE C3b Atom 3B -- same shim as pe_enter (TEB
// install) but also passes an explicit arg in x0 to entry, needed for the
// Phase-0 boot leap ("enter PE at LdrInitializeThunk with x0=&pushed_ctx",
// SLICE3-SPEC Architecture verdict 2). long pe_enter1(long (*entry)(void*),
// void *teb, void *arg) -- x0=entry fn ptr, x1=teb, x2=arg.
.global pe_enter1
pe_enter1:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x9, x18                    // save kernel-side x18
    mov  x10, x0                    // x10 = entry fn ptr (x0 about to be
                                     // overwritten with the arg -- must
                                     // stash the entry address first)
    mov  x18, x1                    // x18 = TEB (Windows ARM64 ABI convention)
    mov  x0, x2                     // x0 = arg (entry's own first param)
    blr  x10
    mov  x18, x9                    // restore kernel x18 (normal-return path)
    ldp  x29, x30, [sp], #16
    ret

// ---- pe_enter6: CHALICE C4-48 bug1 fix -- x18(TEB) stash/restore bracket
// around a native (non-syscall-stub) crossing into PE-ABI-respecting code,
// extending the SAME pe_enter/pe_enter1 idiom (this floor's OWN existing
// mechanism, not a new one) to a 6-argument callee. Needed because this
// floor's own kernel C (aarch64-elf, no -ffixed-x18 -- confirmed via
// build.sh grep) is free to clobber x18 as ordinary scratch between the
// dispatcher's TEB install and any deep call site reached via plain C
// function calls (unlike nt_dispatch's own x22/x24 bracket immediately
// around ITS blr, or pe_enter/pe_enter1's x9 bracket) -- ntfile.c's
// NtMapViewOfSection -> nt_wow64_maybe_notify_map_view -> raw fn() call
// into BTCpuNotifyMapViewOfSection (native ARM64, Windows-ARM64-ABI-
// respecting, expects x18==guest TEB) is exactly such a site (C4-47).
// long pe_enter6(long (*entry)(...), void *teb, a0,a1,a2,a3,a4,a5) --
// x0=entry fn ptr, x1=teb, x2..x7=six args (matches
// BTCpuNotifyMapViewOfSectionFn's six-param shape exactly, all in
// registers, no stack-arg marshal needed).
.global pe_enter6
pe_enter6:
    stp  x29, x30, [sp, #-16]!
    mov  x29, sp
    mov  x9, x18                    // save kernel-side x18
    mov  x10, x0                    // stash entry fn ptr (x0 about to be
                                     // overwritten by the shifted args)
    mov  x18, x1                    // x18 = TEB captured by the CALLER at
                                     // its own entry (see ntfile.c), NOT
                                     // whatever x18 happens to hold here --
                                     // this is the actual bug1 fix.
    mov  x0, x2
    mov  x1, x3
    mov  x2, x4
    mov  x3, x5
    mov  x4, x6
    mov  x5, x7
    blr  x10
    mov  x18, x9                    // restore kernel x18
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

// ---- NtContinue: Atom B slice-2 boss. Full ARM64 CONTEXT restore (layout
// per reactos/sdk/include/xdk/arm64/ke.h _CONTEXT, receipted below), "the
// loader's leap off its own stack" -- lands directly at Context->Pc with
// Context->Sp as the new stack, NEVER returning to nt_dispatch's normal
// dispatch_return path (which only round-trips frame->pc/sp, not a full
// register file -- insufficient for this). Called via the same `blr x10`
// handler-call slot as any other service (x0=ContextRecord, x1=TestAlert,
// AAPCS64, per nt_dispatch's arg reload before the blr).
//
// CONTEXT layout (ke.h _CONTEXT, LP64/_STRUCT64, all fields naturally
// aligned so no padding surprises): ContextFlags(u32)@0x00, Cpsr(u32)@0x04,
// X[31](u64 each, =X0..X28,Fp(X29),Lr(X30))@0x08..0x100, Sp@0x100, Pc@0x108,
// V[32](NEON128,16B each)@0x110..0x310, Fpcr/Fpsr@0x310/0x314,
// Bcr[8]@0x318, Bvr[8]@0x338, Wcr[2]@0x378, Wvr[2]@0x380 (size 0x390 --
// matches the well-known public Win64 CONTEXT size for ARM64).
//
// C4-18: the dispatcher can return a FEX JIT CONTEXT, so NtContinue is a
// literal ARM64 CONTEXT restore: x0-x30 (including x16/x17), SP, PC, PSTATE,
// q0-q31, FPCR and FPSR.  ERET, rather than BR x16, lets x16/x17 be restored
// too; ELR/SPSR are written before the last scratch registers disappear.
.global NtContinue
NtContinue:
    // C4-18 receipt: trace the first continuations; save x0/x18 because this
    // is entered from a PE syscall stub and kernel C may treat x18 as scratch.
    stp  x0, x18, [sp, #-16]!
    bl   nt_continue_trace
    ldp  x0, x18, [sp], #16
    str  xzr, [x18, #0x378]          // clear syscall-frame while old TEB lives
    mov  x9, x0                      // ContextRecord; retained until final load
    ldr  x16, [x9, #0x108]
    msr  elr_el1, x16
    ldr  w16, [x9, #0x04]
    // CONTEXT.Cpsr carries flags/masks; this flat floor always resumes guest
    // code at EL1h. Legacy callers (hello3) leave its mode field zero.
    bic  x16, x16, #0xf
    mov  x17, #0x5
    orr  x16, x16, x17
    msr  spsr_el1, x16
    ldr  w16, [x9, #0x310]
    msr  fpcr, x16
    ldr  w16, [x9, #0x314]
    msr  fpsr, x16
    ldp  q0,  q1,  [x9, #0x110]
    ldp  q2,  q3,  [x9, #0x130]
    ldp  q4,  q5,  [x9, #0x150]
    ldp  q6,  q7,  [x9, #0x170]
    ldp  q8,  q9,  [x9, #0x190]
    ldp  q10, q11, [x9, #0x1b0]
    ldp  q12, q13, [x9, #0x1d0]
    ldp  q14, q15, [x9, #0x1f0]
    ldp  q16, q17, [x9, #0x210]
    ldp  q18, q19, [x9, #0x230]
    ldp  q20, q21, [x9, #0x250]
    ldp  q22, q23, [x9, #0x270]
    ldp  q24, q25, [x9, #0x290]
    ldp  q26, q27, [x9, #0x2b0]
    ldp  q28, q29, [x9, #0x2d0]
    ldp  q30, q31, [x9, #0x2f0]
    ldp  x0,  x1,  [x9, #0x08]
    ldp  x2,  x3,  [x9, #0x18]
    ldp  x4,  x5,  [x9, #0x28]
    ldp  x6,  x7,  [x9, #0x38]
    ldr  x8,       [x9, #0x48]
    ldp  x10, x11, [x9, #0x58]
    ldp  x12, x13, [x9, #0x68]
    ldp  x14, x15, [x9, #0x78]
    ldr  x18,      [x9, #0x98]
    ldp  x19, x20, [x9, #0xa0]
    ldp  x21, x22, [x9, #0xb0]
    ldp  x23, x24, [x9, #0xc0]
    ldp  x25, x26, [x9, #0xd0]
    ldp  x27, x28, [x9, #0xe0]
    ldp  x29, x30, [x9, #0xf0]
    ldr  x17,      [x9, #0x100]
    mov  sp, x17
    ldr  x16,      [x9, #0x88]
    ldr  x17,      [x9, #0x90]
    ldr  x9,       [x9, #0x50]
    eret

// Enter a measured KiUserExceptionDispatcher stack from NtRaiseException.
// x18 remains the guest TEB; dispatcher owns all other incoming registers.
.global nt_exception_enter
nt_exception_enter:
    mov  sp, x0
    br   x1
