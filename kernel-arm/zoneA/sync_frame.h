// CHALICE C4-20 — one source of truth: boot.S synchronous exception frame.
// Layout: x0-x30 plus 8-byte pad, then q0-q31.  Both asm and C include this.
#pragma once

#define SYNC_FRAME_X_COUNT       31
#define SYNC_FRAME_X_BYTES       (SYNC_FRAME_X_COUNT * 8)
#define SYNC_FRAME_GPR_BYTES     0x100
#define SYNC_FRAME_Q_OFF         SYNC_FRAME_GPR_BYTES
#define SYNC_FRAME_Q_BYTES       0x200
#define SYNC_FRAME_SIZE          (SYNC_FRAME_GPR_BYTES + SYNC_FRAME_Q_BYTES)
#define SYNC_FRAME_X18_OFF       (18 * 8)

// CHALICE C4-23 — ARM64 CONTEXT.X18 field offset (ARM64_CONTEXT_X_OFF=0x008
// + 18*8, kmain.c). guest_exception_stack()'s 0x460-byte CONTEXT/EX/RECORD
// zero-fill (ending at orig_sp) fully overlaps/clobbers the 0x300-byte
// SAVEALL_SYNC register frame at x21 (also ending at orig_sp) BEFORE boot.S
// used to reload x18 from it -- x18=0 was delivered to every guest
// exception dispatch. Fix: reload x18 from the delivered CONTEXT struct
// itself (built from a pre-clobber regs[] snapshot, kmain.c) instead.
#define ARM64_CONTEXT_X18_OFF    0x098
