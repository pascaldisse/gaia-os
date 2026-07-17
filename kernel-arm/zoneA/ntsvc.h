// CHALICE C2a Atom A — NT service table + TEB/PEB floor (kernel side).
#pragma once
#include "kernel.h"
#include "../chalice/ntids.h"

// SYSTEM_SERVICE_TABLE, 32 B/entry — donor shape (matches
// wine/dlls/ntdll/unix/signal_arm64.c:1630+ offsets 0/16/24 used by
// ntdisp.S; CounterTable at +8 unused in Atom A, kept for layout parity).
typedef struct {
    void          **ServiceTable;   // +0
    void           *CounterTable;   // +8  (unused)
    unsigned long   ServiceLimit;   // +16
    unsigned char  *ArgumentTable;  // +24 — total arg bytes per id
} NtServiceTable;

// table 0 real (wired below), 1-3 left zeroed (ServiceLimit=0 -> every id
// on those tables is STATUS_INVALID_SYSTEM_SERVICE, per spec A3).
extern NtServiceTable nt_service_tables[4];

void nt_service_tables_init(void);   // idempotent, called by nt_build_teb

// ntdisp.S
long nt_dispatch(void);                             // blr target from PE stubs
long pe_enter(long (*entry)(void), void *teb);       // A4 TEB-install shim
// CHALICE C3b Atom 3B: pe_enter1 -- same TEB-install shim as pe_enter, but
// also loads a caller-supplied arg into x0 before the blr (Architecture
// verdict 2's "enter PE at LdrInitializeThunk with x0=&pushed_ctx" needs an
// explicit argument; pe_enter's entry signature takes none).
long pe_enter1(long (*entry)(void *arg), void *teb, void *arg);
long nt_setjmp(unsigned long *buf);
void nt_longjmp(unsigned long *buf, long val) __attribute__((noreturn));
// Atom B slice-2 boss: full ARM64 CONTEXT restore, never returns to its
// caller (the loader's leap off its own stack — lands directly at
// Context->Pc). Implemented in asm (ntdisp.S) since it must restore x0-x30
// as raw registers, not something a C function signature can express.
void NtContinue(void) __attribute__((noreturn));

// NtTerminateProcess unwind channel (PeRun sets jmpbuf before pe_enter;
// NtTerminateProcess sets exit_code then longjmps — see ntsvc.c receipt).
extern unsigned long *nt_exit_jmpbuf;
extern long nt_exit_code;

// A4: allocate + populate TEB/PEB/RTL_USER_PROCESS_PARAMETERS/PEB_LDR_DATA/
// LDR_DATA_TABLE_ENTRY for one PE image, wire the 4 trapdoor pointers +
// dispatch slots. Returns the TEB pointer (what x18 must hold).
void *nt_build_teb(void *image_base, void *entry, unsigned long image_size);

// A3 real handlers (Win ABI arg order; called only via nt_dispatch's blr,
// but prototyped here too so ntsvc.c and callers agree on signatures).
long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                  void *IoStatusBlock, const void *Buffer, unsigned long Length,
                  void *ByteOffset, unsigned long *Key);
long NtClose(void *Handle);
long NtTerminateProcess(void *ProcessHandle, long ExitStatus);
// CHALICE C3c-2k p1: legible-abort handlers.
long NtRaiseException(void *ExceptionRecord, void *Context, unsigned long FirstChance);
long NtRaiseHardError(unsigned long ErrorStatus, unsigned long NumberOfParameters,
                       unsigned long UnicodeStringParameterMask, void *Parameters,
                       unsigned long ValidResponseOptions, void *Response);

// Atom B real handlers.
long NtAllocateVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long ZeroBits,
                              unsigned long *RegionSize, unsigned long AllocationType,
                              unsigned long Protect);
long NtFreeVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                          unsigned long FreeType);
long NtQueryInformationProcess(void *ProcessHandle, unsigned long ProcessInformationClass,
                                void *ProcessInformation, unsigned long ProcessInformationLength,
                                unsigned long *ReturnLength);
long NtQuerySystemInformation(unsigned long SystemInformationClass, void *SystemInformation,
                               unsigned long SystemInformationLength, unsigned long *ReturnLength);

// Atom B: populate KUSER_SHARED_DATA @ NT_KUSER_SHARED_DATA_ADDR (fixed
// physical address, identity-mapped normal memory per kmain.c's mmu_on).
// Idempotent-by-caller: invoked once from nt_service_tables_init().
void nt_kuser_init(void);

// CHALICE C3c Atom 3C-1: dispatcher call-log. Cheap, removable-later debug
// aid -- serial line per incoming service id for the first
// NT_DISPATCH_LOG_FIRST_N calls after a caller-chosen point (nt_dispatch_
// log_reset, called by shell.c's pe6 verb right before nt_boot_process)
// PLUS every unknown (id beyond ServiceLimit, ntdisp.S bad_service path)
// or unimplemented (handler == NtStub) id hit regardless of the counter --
// this is what makes it useful as a fault log for stock ntdll's first walk
// (SLICE3-SPEC Atom 3C-1 deliverable). Gated by a runtime flag
// (nt_dispatch_log_enabled, default 0 -- shell.c's pe6 verb turns it on
// only around the nt_boot_process call so pe1-pe5/zc/aot/etc. stay quiet);
// called from ntdisp.S's nt_dispatch via `bl nt_dispatch_log`, x18 (TEB)
// stashed/restored around the call there since our own kernel C is free to
// clobber it (header note, ntdisp.S). handler==0 is the "unknown/
// out-of-range id" sentinel (bad_service path never resolves a handler
// ptr); any other value is compared against NtStub to detect
// "unimplemented but in-range".
extern int nt_dispatch_log_enabled;
void nt_dispatch_log_reset(void);
void nt_dispatch_log(unsigned long id, void *handler);
