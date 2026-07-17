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
long nt_setjmp(unsigned long *buf);
void nt_longjmp(unsigned long *buf, long val) __attribute__((noreturn));

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
