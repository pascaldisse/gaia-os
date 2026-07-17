// CHALICE C2a slice 2 Atom A/B — PE test artifact 3: exercises the NT
// service ABI (TEB/PEB trapdoors + NtWriteFile/NtClose/NtTerminateProcess,
// Atom A) plus the Atom B additions (NtAllocateVirtualMemory VM arena,
// KUSER_SHARED_DATA read, NtContinue full-CONTEXT leap). No DLL import: all
// Nt* entry points (ntstub.S) are table-dispatched indirect calls through
// TEB+0x380, not gaia.dll imports (see ntids.h for why this deviates from a
// real Wine ntdll stub).
#include "ntids.h"

extern long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                         void *IoStatusBlock, const void *Buffer, unsigned long Length,
                         void *ByteOffset, unsigned long *Key);
extern long NtClose(void *Handle);
extern long NtTerminateProcess(void *ProcessHandle, long ExitStatus);
// RegionSize is SIZE_T* = 64-bit on ARM64 NT. `unsigned long` is 32-bit on
// this aarch64-windows-gnu/LLP64 target -- a 4-byte slot the LP64 kernel
// would read 8 bytes from (garbage in the high half). Must be ull.
extern long NtAllocateVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long ZeroBits,
                                     unsigned long long *RegionSize, unsigned long AllocationType,
                                     unsigned long Protect);
extern long NtFreeVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long long *RegionSize,
                                 unsigned long FreeType);
// x0=ContextRecord, x1=TestAlert (AAPCS64, matches nt_dispatch's own x0/x1
// reload) -- never returns; kernel side is noreturn.
extern void NtContinue(void *ContextRecord, unsigned char TestAlert);

typedef struct { unsigned long long Status; unsigned long long Information; } IO_STATUS_BLOCK;

// ---- Atom B: ARM64 CONTEXT, layout per reactos/sdk/include/xdk/arm64/ke.h
// _CONTEXT (receipted in full in zoneA/ntdisp.S's NtContinue comment — same
// offsets, mirrored here so hello3's own field writes land where NtContinue
// expects them). All fields naturally aligned under normal AAPCS64 struct
// layout rules, no manual padding required.
typedef union {
    struct { unsigned long long Low; long long High; } s;
    double D[2]; float S[4]; unsigned short H[8]; unsigned char B[16];
} NEON128;

typedef struct {
    unsigned int ContextFlags;
    unsigned int Cpsr;
    union {
        struct {
            unsigned long long X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13,
                               X14, X15, X16, X17, X18, X19, X20, X21, X22, X23, X24, X25,
                               X26, X27, X28, Fp, Lr;
        } r;
        unsigned long long X[31];
    } u;
    unsigned long long Sp;
    unsigned long long Pc;
    NEON128 V[32];
    unsigned int Fpcr;
    unsigned int Fpsr;
    unsigned int Bcr[8];
    unsigned long long Bvr[8];
    unsigned int Wcr[2];
    unsigned long long Wvr[2];
} NT_CONTEXT;

#define CONTINUE_COOKIE 0xC0FFEEULL

static const char msg_speaks[]   = "CHALICE: the cup speaks\n";
static const char msg_continue[] = "CHALICE: continue lands\n";

// Set by PeMain before the NtContinue leap, read by Part2 after it lands —
// same static image/address space either side of the leap, so plain
// globals work fine (no need to round-trip everything through CONTEXT
// registers; X0=cookie and X18=TEB are the two the probe specifically
// requires, per SLICE2-SPEC Atom B: "CONTEXT{pc=part2, sp, x0=cookie}").
static void *g_hstdout;
static unsigned char *g_vmbuf;
static unsigned long g_vmbuf_len;

static unsigned long u32_to_dec(char *buf, unsigned int v) {
    char tmp[10]; int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    unsigned long n = 0;
    while (i > 0) buf[n++] = tmp[--i];
    return n;
}

// ---- Part2: the landing site of the NtContinue leap. Runs with a fresh
// stack (part2_stack, set as Context->Sp) and x18 restored = the same TEB
// (Context->X18, set by PeMain before the leap), so it can freely call
// further NT services. x0 arrives as Context->X0 (the cookie) -- NtContinue
// branches straight here with x0 already populated, which lines up exactly
// with a normal one-argument AAPCS64 C function call even though no `bl`
// ever targeted this address directly.
static long Part2(unsigned long long cookie) {
    if (cookie != CONTINUE_COOKIE) {
        static const char bad[] = "CHALICE: cookie mismatch\n";
        NtWriteFile(g_hstdout, 0, 0, 0, 0, bad, sizeof(bad) - 1, 0, 0);
        NtTerminateProcess((void *)(unsigned long long)-1, 3);
        return -1;   // unreachable
    }

    // Use the Atom-B-allocated VM page (filled + verified by PeMain) as the
    // NtWriteFile buffer -- "proves the arena" per SLICE2-SPEC Atom B.
    IO_STATUS_BLOCK iosb;
    NtWriteFile(g_hstdout, 0, 0, 0, &iosb, g_vmbuf, g_vmbuf_len, 0, 0);
    NtClose(g_hstdout);
    NtTerminateProcess((void *)(unsigned long long)-1, 99);
    return -1;   // unreachable: NtTerminateProcess never returns
}

static NT_CONTEXT g_ctx;
static unsigned char g_part2_stack[4096] __attribute__((aligned(16)));

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
    g_hstdout = hstdout;

    unsigned long n = 0; const char *p = msg_speaks; while (*p++) n++;
    IO_STATUS_BLOCK iosb;
    NtWriteFile(hstdout, 0, 0, 0, &iosb, msg_speaks, n, 0, 0);
    NtClose(hstdout);

    // ---- Atom B: KUSER_SHARED_DATA floor. Read NtMajorVersion straight
    // from the fixed physical address (no TEB indirection needed -- we ARE
    // the unix side, per HANDOFF). ----
    volatile unsigned int *nt_major =
        (volatile unsigned int *)(unsigned long)(NT_KUSER_SHARED_DATA_ADDR + NT_KUSER_NTMAJORVERSION_OFF);
    char kbuf[40];
    unsigned long klen = 0;
    const char kprefix[] = "CHALICE: NT";
    for (unsigned long i = 0; i < sizeof(kprefix) - 1; i++) kbuf[klen++] = kprefix[i];
    klen += u32_to_dec(kbuf + klen, *nt_major);
    const char ksuffix[] = " floor\n";
    for (unsigned long i = 0; i < sizeof(ksuffix) - 1; i++) kbuf[klen++] = ksuffix[i];
    NtWriteFile(hstdout, 0, 0, 0, &iosb, kbuf, klen, 0, 0);

    // ---- Atom B: VM arena -- allocate a page, write it, read it back,
    // stash it for Part2 to use as the "continue lands" NtWriteFile buffer
    // (proves the arena, per SLICE2-SPEC Atom B). ----
    void *vmbase = 0;
    unsigned long long vmsize = 4096;   // 1 page; SIZE_T = 64-bit (LLP64: see extern note)
    long vmret = NtAllocateVirtualMemory((void *)(unsigned long long)-1, &vmbase, 0, &vmsize,
                                          0x1000 /* MEM_COMMIT */ | 0x2000 /* MEM_RESERVE */,
                                          0x04 /* PAGE_READWRITE, ignored by the shim */);
    if (vmret != 0 || !vmbase) {
        static const char vmfail[] = "CHALICE: vm alloc failed\n";
        NtWriteFile(hstdout, 0, 0, 0, 0, vmfail, sizeof(vmfail) - 1, 0, 0);
        NtClose(hstdout);
        NtTerminateProcess((void *)(unsigned long long)-1, 1);
        return -1;
    }

    unsigned char *vp = (unsigned char *)vmbase;
    unsigned long mlen = 0; const char *mp = msg_continue; while (*mp++) mlen++;
    for (unsigned long i = 0; i < mlen; i++) vp[i] = msg_continue[i];
    int ok = 1;
    for (unsigned long i = 0; i < mlen; i++) if (vp[i] != msg_continue[i]) ok = 0;
    if (!ok) {
        static const char vmbad[] = "CHALICE: vm readback mismatch\n";
        NtWriteFile(hstdout, 0, 0, 0, 0, vmbad, sizeof(vmbad) - 1, 0, 0);
        NtClose(hstdout);
        NtTerminateProcess((void *)(unsigned long long)-1, 2);
        return -1;
    }
    g_vmbuf = vp;
    g_vmbuf_len = mlen;

    // ---- Atom B: the leap. Build CONTEXT{pc=Part2, sp=fresh stack,
    // x0=cookie, x18=TEB (continuity)} -> NtContinue -> never returns here.
    unsigned char *cp = (unsigned char *)&g_ctx;
    for (unsigned long i = 0; i < sizeof(g_ctx); i++) cp[i] = 0;
    // Straight to unsigned long long (not via `unsigned long`, which is
    // only 32-bit on this aarch64-windows-gnu/LLP64 target -- would silently
    // truncate a pointer before widening back).
    g_ctx.Pc = (unsigned long long)(void *)&Part2;
    g_ctx.Sp = (unsigned long long)(void *)(g_part2_stack + sizeof(g_part2_stack));
    g_ctx.u.r.X0 = CONTINUE_COOKIE;
    g_ctx.u.r.X18 = (unsigned long long)(void *)teb;

    NtContinue(&g_ctx, 0);
    return -1;   // unreachable
}
