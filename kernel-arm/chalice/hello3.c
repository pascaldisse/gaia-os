// CHALICE C2a slice 2 Atom A — PE test artifact 3: exercises the NT service
// ABI (TEB/PEB trapdoors + NtWriteFile/NtClose/NtTerminateProcess). No DLL
// import: NtWriteFile/NtClose/NtTerminateProcess (ntstub.S) are
// table-dispatched indirect calls through TEB+0x380, not gaia.dll imports
// (see ntids.h for why this deviates from a real Wine ntdll stub).
extern long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                         void *IoStatusBlock, const void *Buffer, unsigned long Length,
                         void *ByteOffset, unsigned long *Key);
extern long NtClose(void *Handle);
extern long NtTerminateProcess(void *ProcessHandle, long ExitStatus);

typedef struct { unsigned long long Status; unsigned long long Information; } IO_STATUS_BLOCK;

static const char msg[] = "CHALICE: the cup speaks\n";

long PeMain(void) {
    // Walk x18 -> TEB -> PEB -> ProcessParameters -> hStdOutput (the 4
    // trapdoors, HANDOFF: "ntdll dereferences these BEFORE the first
    // service call"). Offsets match ntsvc.c's nt_build_teb receipts:
    // TEB.ProcessEnvironmentBlock @0x60, PEB.ProcessParameters @0x20,
    // RTL_USER_PROCESS_PARAMETERS.StandardOutput @0x28.
    unsigned char *teb;
    __asm__ volatile("mov %0, x18" : "=r"(teb));
    unsigned char *peb    = *(unsigned char **)(teb + 0x60);
    unsigned char *params = *(unsigned char **)(peb + 0x20);
    void *hstdout = *(void **)(params + 0x28);

    unsigned long n = 0; const char *p = msg; while (*p++) n++;

    IO_STATUS_BLOCK iosb;
    NtWriteFile(hstdout, 0, 0, 0, &iosb, msg, n, 0, 0);
    NtClose(hstdout);
    NtTerminateProcess((void *)(unsigned long long)-1, 99);   // -1 = NtCurrentProcess()
    return -1;   // unreachable: NtTerminateProcess never returns
}
