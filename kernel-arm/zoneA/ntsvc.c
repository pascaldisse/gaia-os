// CHALICE C2a Atom A — NT service table + TEB/PEB floor.
// Struct layouts are hand-mirrored from reactos/sdk/include/ndk/{peb_teb.h,
// rtltypes.h,ldrtypes.h} (LP64 / _STRUCT64 branch) by walking field order +
// natural alignment; every offset used below is receipted against either an
// explicit C_ASSERT in the donor header or a hand-computed one (shown in the
// comment) derived the same way (sequential fields, 8-byte pointer/ULONG_PTR
// alignment, 4-byte ULONG, matching the well-known public Win64 TEB/PEB
// offsets as a cross-check). We use raw byte-offset writes (not nested C
// structs) so compiler struct-packing quirks can never silently diverge from
// the receipted layout — same pattern pemod.c uses for the PE header fields.
#include "kernel.h"
#include "ntsvc.h"

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;

static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr16(u8 *p, u16 v)  { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static void *nt_zalloc(u64b size) {
    u8 *p = (u8 *)MAlloc(size);
    if (p) for (u64b i = 0; i < size; i++) p[i] = 0;
    return p;
}

// =====================================================================
// A3 — service table + the 3 real handlers.
// =====================================================================

static long NtStub(void) { return STATUS_NOT_IMPLEMENTED; }

// IO_STATUS_BLOCK: union{NTSTATUS Status; PVOID Pointer;} (8B, PVOID-sized
// on LP64) + ULONG_PTR Information (8B) = 16B total (HANDOFF/A3 receipt:
// "IO_STATUS_BLOCK(Information=bytes), 16 B struct").
typedef struct { u64b status; u64b information; } NtIoStatusBlock;

long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                  void *IoStatusBlock, const void *Buffer, unsigned long Length,
                  void *ByteOffset, unsigned long *Key) {
    (void)Event; (void)ApcRoutine; (void)ApcContext; (void)ByteOffset; (void)Key;
    if ((u64b)FileHandle != (u64b)NT_STDOUT_HANDLE) return STATUS_NOT_IMPLEMENTED;
    const u8 *p = (const u8 *)Buffer;
    for (unsigned long i = 0; i < Length; i++) putc((char)p[i]);
    if (IoStatusBlock) {
        NtIoStatusBlock *iosb = (NtIoStatusBlock *)IoStatusBlock;
        iosb->status = STATUS_SUCCESS;
        iosb->information = Length;
    }
    return STATUS_SUCCESS;
}

long NtClose(void *Handle) { (void)Handle; return STATUS_SUCCESS; }

// NtTerminateProcess: "unwind to PeRun w/ exit code — same mechanism PeRun
// already uses for entry return" (SLICE2-SPEC A3). PeRun brackets its call
// to entry (via pe_enter) with nt_setjmp/nt_longjmp; we just stash the exit
// code and jump. Sentinel val=1 (never 0) sidesteps the classic setjmp/
// longjmp "val==0 looks like the first call" ambiguity — the real exit
// code travels out-of-band via nt_exit_code, not via the longjmp val.
unsigned long *nt_exit_jmpbuf;
long nt_exit_code;

long NtTerminateProcess(void *ProcessHandle, long ExitStatus) {
    (void)ProcessHandle;
    nt_exit_code = ExitStatus;
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    return STATUS_SUCCESS;   // unreachable
}

// =====================================================================
// Atom B — flat page-aligned VM arena (NtAllocateVirtualMemory /
// NtFreeVirtualMemory). HANDOFF: "flat arena OK, skip VAD tree". Backed by
// mem.c's MAlloc (over-allocate + align, same trick pemod.c already uses
// for PE image bases) rather than a dedicated static region — a dedicated
// large static .bss array was considered and rejected: the kernel currently
// links+bss's to ~0x40229688 (verified via `nm`), MemInit's HEAP_BASE is a
// hardcoded 0x41000000 (mem.c), leaving only ~14 MB of headroom before the
// heap base — a multi-MB static arena would risk colliding with the heap.
// Routing through MAlloc keeps everything inside the existing 64 MB heap
// (0x41000000-0x45000000), nowhere near KUSER_SHARED_DATA @ 0x7ffe0000 (the
// mem.c arena-bounds check SLICE2-SPEC Atom B asks to verify — confirmed by
// inspection: heap ceiling 0x45000000 is ~0.8 GB below 0x7ffe0000, no guard
// needed in mem.c itself).
#define NT_PAGE_SIZE 4096UL
#define NT_VM_MAX_REGIONS 64

static struct {
    void   *raw;        // the underlying MAlloc pointer (pre-alignment)
    void   *aligned;     // page-aligned pointer handed to the PE side
    u64b    size;         // page-rounded size
    int     used;
} nt_vm_regions[NT_VM_MAX_REGIONS];

static void *nt_vm_alloc(u64b size) {
    u8 *raw = (u8 *)MAlloc(size + NT_PAGE_SIZE);
    if (!raw) return 0;
    u8 *aligned = (u8 *)(((u64b)raw + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1));
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (nt_vm_regions[i].used) continue;
        nt_vm_regions[i].raw = raw;
        nt_vm_regions[i].aligned = aligned;
        nt_vm_regions[i].size = size;
        nt_vm_regions[i].used = 1;
        return aligned;
    }
    Free(raw);   // no tracking slot left — fail cleanly rather than leak untracked
    return 0;
}

// Returns page-rounded size of the freed region via *out_size, or 0 if
// aligned wasn't a live region (unknown/already-freed base).
static int nt_vm_free(void *aligned, u64b *out_size) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used || nt_vm_regions[i].aligned != aligned) continue;
        if (out_size) *out_size = nt_vm_regions[i].size;
        Free(nt_vm_regions[i].raw);
        nt_vm_regions[i].used = 0;
        return 1;
    }
    return 0;
}

long NtAllocateVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long ZeroBits,
                              unsigned long *RegionSize, unsigned long AllocationType,
                              unsigned long Protect) {
    (void)ProcessHandle; (void)ZeroBits; (void)Protect;
    if (!BaseAddress || !RegionSize) return STATUS_INVALID_PARAMETER;

    // exotic flags -> STATUS_INVALID_PARAMETER (SLICE2-SPEC Atom B); must
    // specify at least one of COMMIT/RESERVE, both together also accepted
    // ("MEM_COMMIT|MEM_RESERVE both = alloc").
    if (AllocationType == 0 || (AllocationType & ~(u64b)(MEM_COMMIT | MEM_RESERVE)) != 0)
        return STATUS_INVALID_PARAMETER;

    u64b size = *RegionSize;
    if (size == 0) return STATUS_INVALID_PARAMETER;
    u64b pages = (size + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);

    // DEVIATION (documented, not silent): a caller-supplied *BaseAddress
    // hint can't be honored by a flat bump/free-list arena (no VAD tree, no
    // control over where MAlloc's free list happens to have room) --
    // rejected explicitly rather than silently ignored. hello3's probe
    // always passes NULL (system-chosen base), so this path isn't exercised
    // by the Atom B gate.
    if (*BaseAddress != 0) return STATUS_INVALID_PARAMETER;

    void *mem = nt_vm_alloc(pages);
    if (!mem) return STATUS_NO_MEMORY;

    *BaseAddress = mem;
    *RegionSize = pages;
    return STATUS_SUCCESS;
}

long NtFreeVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                          unsigned long FreeType) {
    (void)ProcessHandle;
    if (!BaseAddress || !*BaseAddress) return STATUS_INVALID_PARAMETER;
    if (FreeType != MEM_RELEASE && FreeType != MEM_DECOMMIT) return STATUS_INVALID_PARAMETER;

    if (FreeType == MEM_RELEASE) {
        u64b freed = 0;
        if (!nt_vm_free(*BaseAddress, &freed)) return STATUS_INVALID_PARAMETER;
        if (RegionSize) *RegionSize = freed;
        return STATUS_SUCCESS;
    }
    // MEM_DECOMMIT: flat arena has no separate commit/decommit state (every
    // page is committed at alloc time) -- no-op success, matches HANDOFF's
    // "flat arena OK" simplification.
    if (RegionSize) *RegionSize = (*RegionSize + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    return STATUS_SUCCESS;
}

// =====================================================================
// Atom B — NtQueryInformationProcess / NtQuerySystemInformation. Only the
// two classes SLICE2-SPEC calls out; everything else STATUS_NOT_IMPLEMENTED.
// =====================================================================

// PROCESS_BASIC_INFORMATION (winternl.h), LP64 layout: ExitStatus(NTSTATUS,
// 4B+4 pad)@0, PebBaseAddress@0x08, AffinityMask@0x10, BasePriority(8,
// padded to keep UniqueProcessId 8-aligned)@0x18, UniqueProcessId@0x20,
// InheritedFromUniqueProcessId@0x28 -- size 0x30 (48B, matches the
// well-known public sizeof(PROCESS_BASIC_INFORMATION) on x64/ARM64).
#define PBI_SIZE 0x30
#define PBI_PEBBASEADDRESS_OFF 0x08
#define PBI_AFFINITYMASK_OFF   0x10
#define PBI_BASEPRIORITY_OFF   0x18
#define PBI_UNIQUEPROCESSID_OFF 0x20

long NtQueryInformationProcess(void *ProcessHandle, unsigned long ProcessInformationClass,
                                void *ProcessInformation, unsigned long ProcessInformationLength,
                                unsigned long *ReturnLength) {
    (void)ProcessHandle;
    if (ProcessInformationClass != 0 /* ProcessBasicInformation */) return STATUS_NOT_IMPLEMENTED;
    if (!ProcessInformation) return STATUS_INVALID_PARAMETER;
    if (ProcessInformationLength < PBI_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    // Current TEB (x18, Windows ARM64 ABI reserved platform reg) -> PEB.
    // Single-address-space, single-"process" kernel: reading our own live
    // x18 at call time is the only "current process" we have.
    u64b teb; __asm__ volatile("mov %0, x18" : "=r"(teb));
    u64b peb = 0;
    if (teb) peb = *(u64b *)(teb + 0x60);   // TEB_PEB_OFF (A4 section below, ~line 314)

    u8 *out = (u8 *)ProcessInformation;
    for (int i = 0; i < PBI_SIZE; i++) out[i] = 0;
    wr64(out + PBI_PEBBASEADDRESS_OFF, peb);
    wr64(out + PBI_AFFINITYMASK_OFF, 1);        // single logical CPU (KUSER floor, ntsvc.c)
    wr64(out + PBI_BASEPRIORITY_OFF, 8);        // NORMAL_PRIORITY_CLASS-ish, not load-bearing
    wr64(out + PBI_UNIQUEPROCESSID_OFF, 1);     // single-process kernel: PID is always 1

    if (ReturnLength) *ReturnLength = PBI_SIZE;
    return STATUS_SUCCESS;
}

// SYSTEM_BASIC_INFORMATION (ntsvc.c receipt: reactos/sdk/include/ndk/
// extypes.h _SYSTEM_BASIC_INFORMATION), LP64 layout: Reserved(4)@0,
// TimerResolution(4)@4, PageSize(4)@8, NumberOfPhysicalPages(4)@0xC,
// LowestPhysicalPageNumber(4)@0x10, HighestPhysicalPageNumber(4)@0x14,
// AllocationGranularity(4)@0x18 + 4 pad, MinimumUserModeAddress(8)@0x20,
// MaximumUserModeAddress(8)@0x28, ActiveProcessorsAffinityMask(8)@0x30,
// NumberOfProcessors(1)@0x38 -- size 0x40 (64B).
#define SBI_SIZE 0x40
#define SBI_PAGESIZE_OFF 0x08
#define SBI_NUMBEROFPHYSICALPAGES_OFF 0x0C
#define SBI_NUMBEROFPROCESSORS_OFF 0x38

long NtQuerySystemInformation(unsigned long SystemInformationClass, void *SystemInformation,
                               unsigned long SystemInformationLength, unsigned long *ReturnLength) {
    if (SystemInformationClass != 0 /* SystemBasicInformation */) return STATUS_NOT_IMPLEMENTED;
    if (!SystemInformation) return STATUS_INVALID_PARAMETER;
    if (SystemInformationLength < SBI_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    u8 *out = (u8 *)SystemInformation;
    for (int i = 0; i < SBI_SIZE; i++) out[i] = 0;
    wr32(out + SBI_PAGESIZE_OFF, (u32b)NT_PAGE_SIZE);
    wr32(out + SBI_NUMBEROFPHYSICALPAGES_OFF, NT_KUSER_NUM_PHYS_PAGES);
    out[SBI_NUMBEROFPROCESSORS_OFF] = 1;

    if (ReturnLength) *ReturnLength = SBI_SIZE;
    return STATUS_SUCCESS;
}

// =====================================================================
// Atom B — KUSER_SHARED_DATA @ NT_KUSER_SHARED_DATA_ADDR (fixed physical
// address, we populate directly — HANDOFF: "we ARE the unix side").
// =====================================================================
void nt_kuser_init(void) {
    u8 *k = (u8 *)(u64b)NT_KUSER_SHARED_DATA_ADDR;
    // Zero the page first: QEMU RAM content at an address never previously
    // written is otherwise unspecified, and only a handful of fields below
    // are populated -- the rest of KUSER_SHARED_DATA must read as zero, not
    // whatever pattern happened to be in DRAM.
    for (u64b i = 0; i < NT_PAGE_SIZE; i++) k[i] = 0;

    wr32(k + NT_KUSER_NTBUILDNUMBER_OFF, 19041);   // arbitrary Win10 build, not load-bearing
    wr32(k + NT_KUSER_NTMAJORVERSION_OFF, 10);
    wr32(k + NT_KUSER_NTMINORVERSION_OFF, 0);
    wr32(k + NT_KUSER_NUMBEROFPHYSICALPAGES_OFF, NT_KUSER_NUM_PHYS_PAGES);

    // ProcessorFeatures[]: sane subset a real cortex-a72 (our -cpu, build.sh)
    // genuinely has, indices per reactos/sdk/include/xdk/ketypes.h PF_* --
    // not an exhaustive/authoritative CPUID probe (none exists here), just
    // "sane" per SLICE2-SPEC Atom B, rest of the array left zero (=absent).
    k[NT_KUSER_PROCESSORFEATURES_OFF + 18] = 1;   // PF_ARM_VFP_32_REGISTERS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 19] = 1;   // PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 25] = 1;   // PF_ARM_64BIT_LOADSTORE_ATOMIC
    k[NT_KUSER_PROCESSORFEATURES_OFF + 29] = 1;   // PF_ARM_V8_INSTRUCTIONS_AVAILABLE
}

static void  *nt_svc_table0[NT_SERVICE_COUNT];
static u8     nt_svc_argtable0[NT_SERVICE_COUNT];
NtServiceTable nt_service_tables[4];

static int nt_svc_inited;

void nt_service_tables_init(void) {
    if (nt_svc_inited) return;
    nt_svc_inited = 1;
    for (int i = 0; i < NT_SERVICE_COUNT; i++) {
        nt_svc_table0[i] = (void *)NtStub;
        nt_svc_argtable0[i] = 0;             // register-only, all ignored by NtStub
    }
    nt_svc_table0[NT_SVC_NTWRITEFILE]        = (void *)NtWriteFile;
    nt_svc_argtable0[NT_SVC_NTWRITEFILE]     = 9 * 8;   // 9 args (HANDOFF/A2 receipt)
    nt_svc_table0[NT_SVC_NTCLOSE]            = (void *)NtClose;
    nt_svc_argtable0[NT_SVC_NTCLOSE]         = 1 * 8;
    nt_svc_table0[NT_SVC_NTTERMINATEPROCESS] = (void *)NtTerminateProcess;
    nt_svc_argtable0[NT_SVC_NTTERMINATEPROCESS] = 2 * 8;

    // ---- Atom B real handlers ----
    nt_svc_table0[NT_SVC_NTALLOCATEVIRTUALMEMORY] = (void *)NtAllocateVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTALLOCATEVIRTUALMEMORY] = 6 * 8;
    nt_svc_table0[NT_SVC_NTFREEVIRTUALMEMORY] = (void *)NtFreeVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTFREEVIRTUALMEMORY] = 4 * 8;
    nt_svc_table0[NT_SVC_NTQUERYINFORMATIONPROCESS] = (void *)NtQueryInformationProcess;
    nt_svc_argtable0[NT_SVC_NTQUERYINFORMATIONPROCESS] = 5 * 8;
    nt_svc_table0[NT_SVC_NTQUERYSYSTEMINFORMATION] = (void *)NtQuerySystemInformation;
    nt_svc_argtable0[NT_SVC_NTQUERYSYSTEMINFORMATION] = 4 * 8;
    nt_svc_table0[NT_SVC_NTCONTINUE] = (void *)NtContinue;
    nt_svc_argtable0[NT_SVC_NTCONTINUE] = 2 * 8;

    nt_service_tables[0].ServiceTable  = nt_svc_table0;
    nt_service_tables[0].CounterTable  = 0;
    nt_service_tables[0].ServiceLimit  = NT_SERVICE_COUNT;
    nt_service_tables[0].ArgumentTable = nt_svc_argtable0;
    // tables[1..3]: zero-initialized statics -> ServiceLimit=0 -> any id on
    // them is STATUS_INVALID_SYSTEM_SERVICE (spec A3: "1-3 limit=0").

    nt_kuser_init();   // Atom B: populate KUSER_SHARED_DATA before any PE
                        // code (which reads it before its first service
                        // call, per HANDOFF's trapdoor framing) can run.
}

// =====================================================================
// A4 — TEB/PEB floor. Offsets receipted against
// reactos/sdk/include/ndk/{peb_teb.h,rtltypes.h,ldrtypes.h} (_STRUCT64).
// =====================================================================

// ---- TEB (peb_teb.h _TEB) ----
// NT_TIB64.Self: wine/include/wine/winternl.h:732 "ULONG64 Self; /* 0030 */"
#define TEB_NTTIB_SELF_OFF   0x30
// EnvironmentPointer: peb_teb.h:494 C_ASSERT(...EnvironmentPointer)==0x038)
// -> NtTib occupies exactly [0x00,0x38). ProcessEnvironmentBlock follows
// EnvironmentPointer(8B)+ClientId(2 ULONG_PTR=16B)+ActiveRpcHandle(8B)+
// ThreadLocalStoragePointer(8B): 0x38+8+16+8+8 = 0x60 (matches the
// well-known public Win64 TEB.ProcessEnvironmentBlock offset).
#define TEB_PEB_OFF           0x60
#define TEB_SIZE              0x400   // generous; only the slots above + A1
                                       // dispatch slots (<=0x388) are live.

// ---- PEB (peb_teb.h _PEB), sequential from field order + C_ASSERT anchors
// (Ldr==0x18, FastPebLock==0x038, TlsExpansionCounter==0x070,
// NtGlobalFlag==0x0BC, GdiSharedHandleTable==0x0F8, LoaderLock==0x110,
// ImageSubsystem==0x128 — all confirmed in peb_teb.h _STRUCT64 block) ----
#define PEB_BEINGDEBUGGED_OFF     0x02
#define PEB_IMAGEBASEADDRESS_OFF  0x10
#define PEB_LDR_OFF               0x18   // C_ASSERT-anchored
#define PEB_PROCESSPARAMETERS_OFF 0x20
#define PEB_PROCESSHEAP_OFF       0x30
// NumberOfProcessors precedes NtGlobalFlag(C_ASSERT 0x0BC) by one ULONG:
// UnicodeCaseTableData(ptr)@0xB0 -> NumberOfProcessors(ULONG)@0xB8 -> 0xBC.
#define PEB_NUMBEROFPROCESSORS_OFF 0xB8
// OSMajorVersion/Minor/Build precede ImageSubsystem(C_ASSERT 0x128):
// LoaderLock(ptr)@0x110 -> OSMajorVersion(ULONG)@0x118 -> OSMinorVersion@0x11C
// -> OSBuildNumber(USHORT)@0x120 -> OSCSDVersion(USHORT)@0x122 ->
// OSPlatformId(ULONG)@0x124 -> ImageSubsystem@0x128. Matches public Win64 offsets.
#define PEB_OSMAJORVERSION_OFF    0x118
#define PEB_OSMINORVERSION_OFF    0x11C
#define PEB_OSBUILDNUMBER_OFF     0x120
#define PEB_SIZE                  0x140

// ---- RTL_USER_PROCESS_PARAMETERS (rtltypes.h ~:1548) ----
// MaximumLength(4)+Length(4)+Flags(4)+DebugFlags(4)=0x10 -> ConsoleHandle
// (HANDLE, 8-align)@0x10 -> ConsoleFlags(ULONG)@0x18 -> StandardInput
// (HANDLE, 8-align, pad 4)@0x20 -> StandardOutput@0x28 -> StandardError@0x30.
// 0x28 matches SLICE2-SPEC A4's own receipt ("StdOutput @ +0x28").
#define PARAMS_STANDARDOUTPUT_OFF 0x28
#define PARAMS_SIZE               0x60

// ---- PEB_LDR_DATA (ldrtypes.h ~:119, WIN7+ branch: ShutdownThreadId present) ----
// Length(4)+Initialized(1,pad3)=8 -> SsHandle(8)@0x08 -> InLoadOrderModuleList
// (LIST_ENTRY,16B)@0x10 -> InMemoryOrderModuleList@0x20 ->
// InInitializationOrderModuleList@0x30 -> EntryInProgress@0x40 ->
// ShutdownInProgress(1,pad7)@0x48 -> ShutdownThreadId@0x50 -> size 0x58.
#define LDR_INLOADORDER_OFF        0x10
#define LDR_INMEMORYORDER_OFF      0x20
#define LDR_INEINITORDER_OFF       0x30
#define LDR_SIZE                   0x60

// ---- LDR_DATA_TABLE_ENTRY (ldrtypes.h ~:140) ----
// InLoadOrderLinks(16)@0 -> InMemoryOrderLinks(16)@0x10 ->
// InInitializationOrderLinks(16)@0x20 -> DllBase(8)@0x30 -> EntryPoint(8)@0x38
// -> SizeOfImage(4,pad4)@0x40 -> FullDllName(UNICODE_STRING,16B)@0x48 ->
// BaseDllName(16B)@0x58 -> Flags(4)@0x68 -> size 0xA0 (rounded, headroom for
// the trailing union/PVOID fields not needed by Atom A).
#define MOD_INLOADORDER_OFF        0x00
#define MOD_INMEMORYORDER_OFF      0x10
#define MOD_ININITORDER_OFF        0x20
#define MOD_DLLBASE_OFF            0x30
#define MOD_ENTRYPOINT_OFF         0x38
#define MOD_SIZEOFIMAGE_OFF        0x40
#define MOD_SIZE                   0xA0

void *nt_build_teb(void *image_base, void *entry, unsigned long image_size) {
    nt_service_tables_init();

    u8 *teb    = (u8 *)nt_zalloc(TEB_SIZE);
    u8 *peb    = (u8 *)nt_zalloc(PEB_SIZE);
    u8 *params = (u8 *)nt_zalloc(PARAMS_SIZE);
    u8 *ldr    = (u8 *)nt_zalloc(LDR_SIZE);
    u8 *mod    = (u8 *)nt_zalloc(MOD_SIZE);
    u8 *heap   = (u8 *)nt_zalloc(4096);   // dummy ProcessHeap
    if (!teb || !peb || !params || !ldr || !mod || !heap) return 0;

    // ---- trapdoor #1: TEB.NtTib.Self ----
    wr64(teb + TEB_NTTIB_SELF_OFF, (u64b)teb);
    // ---- trapdoor #2: TEB.ProcessEnvironmentBlock ----
    wr64(teb + TEB_PEB_OFF, (u64b)peb);
    // ---- A1 dispatch slots ----
    wr64(teb + NT_TEB_SERVICE_TABLE_OFF, (u64b)nt_service_tables);
    wr64(teb + NT_TEB_SYSCALL_FRAME_OFF, 0);
    wr64(teb + NT_TEB_DISPATCH_ENTRY_OFF, (u64b)nt_dispatch);

    // ---- trapdoor #3: PEB.Ldr ----
    wr64(peb + PEB_LDR_OFF, (u64b)ldr);
    // ---- trapdoor #4: PEB.ProcessParameters ----
    wr64(peb + PEB_PROCESSPARAMETERS_OFF, (u64b)params);
    wr64(peb + PEB_IMAGEBASEADDRESS_OFF, (u64b)image_base);
    wr64(peb + PEB_PROCESSHEAP_OFF, (u64b)heap);
    peb[PEB_BEINGDEBUGGED_OFF] = 0;
    wr32(peb + PEB_NUMBEROFPROCESSORS_OFF, 1);
    wr32(peb + PEB_OSMAJORVERSION_OFF, 10);
    wr32(peb + PEB_OSMINORVERSION_OFF, 0);
    wr16(peb + PEB_OSBUILDNUMBER_OFF, 19041);   // arbitrary Win10 build, not load-bearing

    // ---- ProcessParameters: only hStdOutput is load-bearing for Atom A ----
    wr64(params + PARAMS_STANDARDOUTPUT_OFF, (u64b)NT_STDOUT_HANDLE);

    // ---- PEB_LDR_DATA + one LDR_DATA_TABLE_ENTRY (the image), circular
    // doubly-linked lists of exactly one node (self-consistent). ----
    wr32(ldr + 0x00, LDR_SIZE);
    ldr[0x04] = 1;   // Initialized
    wr64(ldr + LDR_INLOADORDER_OFF + 0, (u64b)(mod + MOD_INLOADORDER_OFF));
    wr64(ldr + LDR_INLOADORDER_OFF + 8, (u64b)(mod + MOD_INLOADORDER_OFF));
    wr64(ldr + LDR_INMEMORYORDER_OFF + 0, (u64b)(mod + MOD_INMEMORYORDER_OFF));
    wr64(ldr + LDR_INMEMORYORDER_OFF + 8, (u64b)(mod + MOD_INMEMORYORDER_OFF));
    wr64(ldr + LDR_INEINITORDER_OFF + 0, (u64b)(mod + MOD_ININITORDER_OFF));
    wr64(ldr + LDR_INEINITORDER_OFF + 8, (u64b)(mod + MOD_ININITORDER_OFF));

    wr64(mod + MOD_INLOADORDER_OFF + 0, (u64b)(ldr + LDR_INLOADORDER_OFF));
    wr64(mod + MOD_INLOADORDER_OFF + 8, (u64b)(ldr + LDR_INLOADORDER_OFF));
    wr64(mod + MOD_INMEMORYORDER_OFF + 0, (u64b)(ldr + LDR_INMEMORYORDER_OFF));
    wr64(mod + MOD_INMEMORYORDER_OFF + 8, (u64b)(ldr + LDR_INMEMORYORDER_OFF));
    wr64(mod + MOD_ININITORDER_OFF + 0, (u64b)(ldr + LDR_INEINITORDER_OFF));
    wr64(mod + MOD_ININITORDER_OFF + 8, (u64b)(ldr + LDR_INEINITORDER_OFF));
    wr64(mod + MOD_DLLBASE_OFF, (u64b)image_base);
    wr64(mod + MOD_ENTRYPOINT_OFF, (u64b)entry);
    wr32(mod + MOD_SIZEOFIMAGE_OFF, (u32b)image_size);

    return teb;
}
