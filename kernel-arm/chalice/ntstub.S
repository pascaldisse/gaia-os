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

// ---- CHALICE C3c-2d: real Wine-stub calling convention (supersedes the
// Atom-B x14 fix below; full receipt in zoneA/ntdisp.S's header — read
// that first) ----
// Atom A/B stashed each stub's own pre-blr x30 into x14 (a floor-only
// convention, never matched by any real Windows-ABI binary). Stock Wine
// ARM64 syscall stubs (decoded from stock ntdll.dll) use `mov x9,x30`
// instead — so migrated here to match: `mov x9, x30` before `blr x16`, same
// as every real stub in stock ntdll. nt_dispatch (ntdisp.S) now captures
// the incoming x9 into the syscall_frame at NT_FRAME_STUB_LR_OFF *before*
// x9 gets reused as the stack-arg base, and restores the real x30 from that
// frame slot immediately before its own `ret x16` — meaning by the time
// control lands back here (right after each stub's `blr x16`), x30 is
// *already* correct. No post-blr restore instruction is needed or present.
.global NtWriteFile
NtWriteFile:
    mov  x9, x30
    mov  x8, #NT_SVC_NTWRITEFILE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtClose
NtClose:
    mov  x9, x30
    mov  x8, #NT_SVC_NTCLOSE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtTerminateProcess
NtTerminateProcess:
    mov  x9, x30
    mov  x8, #NT_SVC_NTTERMINATEPROCESS
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

// ---- Atom B new stubs ----
.global NtAllocateVirtualMemory
NtAllocateVirtualMemory:
    mov  x9, x30
    mov  x8, #NT_SVC_NTALLOCATEVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtFreeVirtualMemory
NtFreeVirtualMemory:
    mov  x9, x30
    mov  x8, #NT_SVC_NTFREEVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtQueryInformationProcess
NtQueryInformationProcess:
    mov  x9, x30
    mov  x8, #NT_SVC_NTQUERYINFORMATIONPROCESS
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtQuerySystemInformation
NtQuerySystemInformation:
    mov  x9, x30
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
    mov  x9, x30
    mov  x8, #NT_SVC_NTCONTINUE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

// ---- CHALICE C3a Atom 3A: loader-path stubs (ntfile.c). Same shape as
// every stub above -- arg count/width is entirely the C caller's/kernel
// handler's business (nt_dispatch marshals stack-spilled args generically,
// ntdisp.S), the stub itself never needs to know how many. ----
.global NtOpenFile
NtOpenFile:
    mov  x9, x30
    mov  x8, #NT_SVC_NTOPENFILE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtReadFile
NtReadFile:
    mov  x9, x30
    mov  x8, #NT_SVC_NTREADFILE
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtCreateSection
NtCreateSection:
    mov  x9, x30
    mov  x8, #NT_SVC_NTCREATESECTION
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtMapViewOfSection
NtMapViewOfSection:
    mov  x9, x30
    mov  x8, #NT_SVC_NTMAPVIEWOFSECTION
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtProtectVirtualMemory
NtProtectVirtualMemory:
    mov  x9, x30
    mov  x8, #NT_SVC_NTPROTECTVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret

.global NtQueryVirtualMemory
NtQueryVirtualMemory:
    mov  x9, x30
    mov  x8, #NT_SVC_NTQUERYVIRTUALMEMORY
    ldr  x16, [x18, #NT_TEB_DISPATCH_ENTRY_OFF]
    blr  x16
    ret
