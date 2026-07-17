// CHALICE C3a Atom 3A — hello4.exe: the loader-path/DLL-machinery gate.
// NtOpenFile("mini.dll") -> NtCreateSection(SEC_IMAGE) -> NtMapViewOfSection
// -> walks mini.dll's own export directory ITSELF (find_export below, no
// kernel service for this -- the mapped image is ordinary flat-arena memory,
// directly readable, per SLICE3-SPEC's "flat arena = no page protection")
// -> calls MiniAdd(1,1) (expect 60) -> NtWriteFile "CHALICE: the cup links"
// -> NtClose file+section -> NtTerminateProcess(60).
// No DLL import (dispatch goes through TEB, not IAT, same as hello3).
// LLP64 note: aarch64-windows-gnu `unsigned long` = 32-bit. Every
// pointer-target integer below (ViewSize, IO_STATUS_BLOCK.Information) is
// explicit `unsigned long long` (SIZE_T semantics) -- by-value ULONG args
// (DesiredAccess, ShareAccess, ...) stay plain `unsigned long`, safe per
// the established SCARS law (w-reg writes zero-extend on the by-value path).
#include "ntids.h"

typedef struct { unsigned short Length, MaximumLength; unsigned short *Buffer; } UNICODE_STRING;
typedef struct {
    unsigned long Length; void *RootDirectory; UNICODE_STRING *ObjectName;
    unsigned long Attributes; void *SecurityDescriptor; void *SecurityQualityOfService;
} OBJECT_ATTRIBUTES;
typedef struct { unsigned long long Status, Information; } IO_STATUS_BLOCK;

extern long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                         void *IoStatusBlock, const void *Buffer, unsigned long Length,
                         void *ByteOffset, unsigned long *Key);
extern long NtClose(void *Handle);
extern long NtTerminateProcess(void *ProcessHandle, long ExitStatus);
extern long NtOpenFile(void **FileHandle, unsigned long DesiredAccess, OBJECT_ATTRIBUTES *ObjectAttributes,
                        IO_STATUS_BLOCK *IoStatusBlock, unsigned long ShareAccess, unsigned long OpenOptions);
extern long NtCreateSection(void **SectionHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                             void *MaximumSize, unsigned long SectionPageProtection,
                             unsigned long AllocationAttributes, void *FileHandle);
extern long NtMapViewOfSection(void *SectionHandle, void *ProcessHandle, void **BaseAddress,
                                unsigned long long ZeroBits, unsigned long long CommitSize,
                                void *SectionOffset, unsigned long long *ViewSize,
                                unsigned long InheritDisposition, unsigned long AllocationType,
                                unsigned long Win32Protect);

#define SEC_IMAGE_LOCAL 0x1000000UL

static const unsigned short g_path[] = {
    '\\', '?', '?', '\\', 'C', ':', '\\', 'w', 'i', 'n', 'd', 'o', 'w', 's', '\\',
    's', 'y', 's', 't', 'e', 'm', '3', '2', '\\', 'm', 'i', 'n', 'i', '.', 'd', 'l', 'l'
};

static unsigned int rd32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// ~40 loc test-side export-directory walker (mirrors pemod.c's
// pe_find_named_export algorithm, kept independent here since this is the
// point of the gate: hello4 resolves its own DLL imports, the kernel never
// touches this). Returns 0 on unresolved.
static void *find_export(const unsigned char *base, const char *name) {
    unsigned int e_lfanew = rd32(base + 0x3C);
    const unsigned char *pe = base + e_lfanew;
    const unsigned char *coff = pe + 4;
    const unsigned char *opt = coff + 20;
    unsigned int ndirs = rd32(opt + 0x6C);
    if (ndirs < 1) return 0;
    const unsigned char *dirs = opt + 0x70;
    unsigned int exp_rva = rd32(dirs + 0);
    if (!exp_rva) return 0;
    const unsigned char *exp = base + exp_rva;
    unsigned int nnames        = rd32(exp + 24);
    unsigned int addr_funcs    = rd32(exp + 28);
    unsigned int addr_names    = rd32(exp + 32);
    unsigned int addr_ordinals = rd32(exp + 36);
    for (unsigned int i = 0; i < nnames; i++) {
        unsigned int name_rva = rd32(base + addr_names + i * 4);
        const char *cand = (const char *)(base + name_rva);
        if (!streq(cand, name)) continue;
        unsigned int oi = addr_ordinals + i * 2;
        unsigned short ord = (unsigned short)(base[oi] | (base[oi + 1] << 8));
        unsigned int func_rva = rd32(base + addr_funcs + (unsigned int)ord * 4);
        return (void *)(base + func_rva);
    }
    return 0;
}

static void fail(void *hstdout, const char *msg, unsigned long len) {
    NtWriteFile(hstdout, 0, 0, 0, 0, msg, len, 0, 0);
    NtTerminateProcess((void *)(unsigned long long)-1, 1);
}

long PeMain(void) {
    unsigned char *teb;
    __asm__ volatile("mov %0, x18" : "=r"(teb));
    unsigned char *peb    = *(unsigned char **)(teb + 0x60);
    unsigned char *params = *(unsigned char **)(peb + 0x20);
    void *hstdout = *(void **)(params + 0x28);

    static const char e_open[]  = "CHALICE: hello4 NtOpenFile failed\n";
    static const char e_sec[]   = "CHALICE: hello4 NtCreateSection failed\n";
    static const char e_map[]   = "CHALICE: hello4 NtMapViewOfSection failed\n";
    static const char e_exp[]   = "CHALICE: hello4 MiniAdd export not found\n";
    static const char e_sum[]   = "CHALICE: hello4 MiniAdd result mismatch\n";

    // Field-by-field, NOT an aggregate initializer: at -O0 on this
    // freestanding target, clang lowers `= {...}` struct literals here to a
    // memcpy-from-compound-literal, which pulls the mingw CRT's memcpy
    // fallback (and everything IT drags in) into the link -- a ~145KB
    // bloat measured empirically (147968B vs 3072B for the field-by-field
    // form, both otherwise identical). No functional difference either way.
    UNICODE_STRING uname;
    uname.Length = sizeof(g_path); uname.MaximumLength = sizeof(g_path);
    uname.Buffer = (unsigned short *)g_path;
    OBJECT_ATTRIBUTES oa;
    oa.Length = sizeof(OBJECT_ATTRIBUTES); oa.RootDirectory = 0; oa.ObjectName = &uname;
    oa.Attributes = 0; oa.SecurityDescriptor = 0; oa.SecurityQualityOfService = 0;
    IO_STATUS_BLOCK iosb;
    void *fh = 0;
    if (NtOpenFile(&fh, 0, &oa, &iosb, 0, 0) != 0 || !fh) {
        fail(hstdout, e_open, sizeof(e_open) - 1); return -1;
    }

    void *sh = 0;
    if (NtCreateSection(&sh, 0, 0, 0, 0, SEC_IMAGE_LOCAL, fh) != 0 || !sh) {
        fail(hstdout, e_sec, sizeof(e_sec) - 1); return -1;
    }

    void *base = 0;
    unsigned long long viewsize = 0;
    if (NtMapViewOfSection(sh, (void *)(unsigned long long)-1, &base, 0, 0, 0, &viewsize, 0, 0, 0) != 0
        || !base) {
        fail(hstdout, e_map, sizeof(e_map) - 1); return -1;
    }

    long (*MiniAdd)(long, long) = (long (*)(long, long))find_export((const unsigned char *)base, "MiniAdd");
    if (!MiniAdd) { fail(hstdout, e_exp, sizeof(e_exp) - 1); return -1; }

    long sum = MiniAdd(1, 1);
    if (sum != 60) { fail(hstdout, e_sum, sizeof(e_sum) - 1); return -1; }

    static const char msg[] = "CHALICE: the cup links\n";
    NtWriteFile(hstdout, 0, 0, 0, &iosb, msg, sizeof(msg) - 1, 0, 0);
    NtClose(fh);
    NtClose(sh);
    NtTerminateProcess((void *)(unsigned long long)-1, 60);
    return -1;   // unreachable
}
