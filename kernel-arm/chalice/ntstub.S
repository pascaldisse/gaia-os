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

// ---- Atom B correctness fix (task-flagged, full receipt in
// zoneA/ntdisp.S's file header — read that first) ----
// Atom A stashed each stub's own pre-blr x30 into x23, a CALLEE-SAVED
// register per the Windows ARM64 AAPCS64 — clobbering it without
// saving/restoring the caller's incoming value violates our own stub's ABI
// contract with whatever calls it. Fixed by using x14 instead: (a)
// caller-saved, so no contract violation, and (b) nt_dispatch (ntdisp.S)
// now captures x14 into the syscall_frame at NT_FRAME_STUB_LR_OFF *before*
// the C handler runs (so the handler is free to clobber x14 as ordinary
// scratch — its saved copy already lives in memory by then) and restores
// the real x30 from that frame slot immediately before its own
// `ret x16` — meaning by the time control lands back here (right after
// each stub's `blr x16`), x30 is *already* correct. No post-blr restore
// instruction is needed or present anymore.
.global NtWriteFile
NtWriteFile:
    mov  x14, x30
    mov  x8, #NT_SVC_NTWRITEFILE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtClose
NtClose:
    mov  x14, x30
    mov  x8, #NT_SVC_NTCLOSE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtTerminateProcess
NtTerminateProcess:
    mov  x14, x30
    mov  x8, #NT_SVC_NTTERMINATEPROCESS
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

// ---- Atom B new stubs ----
.global NtAllocateVirtualMemory
NtAllocateVirtualMemory:
    mov  x14, x30
    mov  x8, #NT_SVC_NTALLOCATEVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtFreeVirtualMemory
NtFreeVirtualMemory:
    mov  x14, x30
    mov  x8, #NT_SVC_NTFREEVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtQueryInformationProcess
NtQueryInformationProcess:
    mov  x14, x30
    mov  x8, #NT_SVC_NTQUERYINFORMATIONPROCESS
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtQuerySystemInformation
NtQuerySystemInformation:
    mov  x14, x30
    mov  x8, #NT_SVC_NTQUERYSYSTEMINFORMATION
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

// NtContinue never actually returns here (the kernel-side handler jumps
// straight to the target Context->Pc, bypassing nt_dispatch's normal
// return path entirely — see ntdisp.S). The trailing `ret` below is
// mechanically present for shape-uniformity with the other stubs but is
// unreachable dead code on every real call.
.global NtContinue
NtContinue:
    mov  x14, x30
    mov  x8, #NT_SVC_NTCONTINUE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret
