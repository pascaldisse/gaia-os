// CHALICE C3b Atom 3B — hello5.exe: Phase-0 boot target. Plain entry
// (no CONTEXT/NtContinue tricks of its own -- fake-ntdll.dll's
// RtlUserThreadStart already delivered it here via the boot chain).
// Walks the TEB (x18, restored by the NtContinue leap through the whole
// LdrInitializeThunk -> RtlUserThreadStart chain, same continuity hello3's
// Part2/hello4 already rely on) -> PEB -> ProcessParameters -> hStdOutput,
// prints, exits. No DLL import (ntstub.S TEB-dispatch stubs, same as
// hello3/hello4).
#include "ntids.h"

extern long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                         void *IoStatusBlock, const void *Buffer, unsigned long Length,
                         void *ByteOffset, unsigned long *Key);
extern long NtTerminateProcess(void *ProcessHandle, long ExitStatus);

long PeMain(void) {
    unsigned char *teb;
    __asm__ volatile("mov %0, x18" : "=r"(teb));
    unsigned char *peb    = *(unsigned char **)(teb + 0x60);
    unsigned char *params = *(unsigned char **)(peb + 0x20);
    void *hstdout = *(void **)(params + 0x28);

    static const char msg[] = "CHALICE: the chain speaks\n";
    NtWriteFile(hstdout, 0, 0, 0, 0, msg, sizeof(msg) - 1, 0, 0);
    NtTerminateProcess((void *)(unsigned long long)-1, 99);
    return -1;   // unreachable
}
