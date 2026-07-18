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
