// CHALICE C2a slice 2 Atom A — PE-side (aarch64-windows-gnu / Windows ARM64
// ABI) NT service stubs. No DLL import: table-dispatched indirect call
// through TEB+0x380 (NT_TEB_DISPATCH_ENTRY_OFF), matching HANDOFF's
// "PE stub loads a fn-ptr from TEB and blrs to it". See ntids.h for the
// receipted deviation from Wine's real ARM64 syscall thunk shape (donor:
// wine/include/wine/asm.h __ASM_SYSCALL_FUNC, __aarch64__ branch — fixed
// literal-pool pointer to __wine_syscall_dispatcher instead of a TEB slot).
// Shape otherwise mirrors the donor stub: x8 = id, indirect blr, ret.
#include "ntids.h"

.text

// NOTE (bugfix during Atom A integration): each stub is a non-leaf fn (it
// blr's into nt_dispatch) but nt_dispatch's own internal `blr x10` handler
// call clobbers x30 before it returns control here via `ret x16` (frame->pc
// deliberately points back at THIS stub's post-blr instruction, not at our
// caller). So x30 must be saved across the blr in a register nt_dispatch
// never touches (x23: nt_dispatch only uses x0-x22,x29 + the frame) --
// NOT on the stack, which would shift the outgoing stack-arg base (x9 in
// nt_dispatch, used by the ArgumentTable stack-arg marshal for calls like
// NtWriteFile's 9th arg) out from under the caller's actual arg placement.

.global NtWriteFile
NtWriteFile:
    mov  x23, x30
    mov  x8, #NT_SVC_NTWRITEFILE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    mov  x30, x23
    ret

.global NtClose
NtClose:
    mov  x23, x30
    mov  x8, #NT_SVC_NTCLOSE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    mov  x30, x23
    ret

.global NtTerminateProcess
NtTerminateProcess:
    mov  x23, x30
    mov  x8, #NT_SVC_NTTERMINATEPROCESS
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    mov  x30, x23
    ret
