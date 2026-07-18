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
#include "ntfile.h"   // CHALICE C3a Atom 3A: loader-path services

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;

static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr16(u8 *p, u16 v)  { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }
static u32b rd32(const u8 *p) { u32b v = 0; for (int i = 0; i < 4; i++) v |= (u32b)p[i] << (8 * i); return v; }

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

long NtClose(void *Handle) {
    // CHALICE C3a Atom 3A: releases file/section handle-table entries too
    // (ntfile.c); values below its handle base (e.g. NT_STDOUT_HANDLE) are
    // simply ignored there, preserving the original no-op-success behavior.
    nt_file_handle_close(Handle);
    return STATUS_SUCCESS;
}

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

// CHALICE C3c-2k p1: NtRaiseException -- makes every future Wine internal
// abort/debug fault LEGIBLE instead of a silent kernel Data Abort. Before
// this, an out-of-range id (any real Wine syscall >= the old
// NT_SERVICE_COUNT=128 table cap, e.g. this one, real id 207) hit
// ntdisp.S's bad_service path and returned STATUS_INVALID_SYSTEM_SERVICE
// forever -- Wine's own "unimplemented function ... aborting" handler
// (stock ntdll) retries NtRaiseException in an unbroken loop on that
// answer (28233 consecutive hits observed, C3c-2j), degrading kernel state
// until a real ARM64 Data Abort. Growing the table (ntids.h,
// NT_SERVICE_COUNT 128->512) lets id 207 resolve to a REAL handler now:
// read EXCEPTION_RECORD.ExceptionCode(@0)/ExceptionAddress(@0x10) (winnt.h
// _EXCEPTION_RECORD layout), print one line naming the fault (resolved
// against the image registry, ntfile.c's nt_image_resolve, when the
// address falls inside a mapped image), then cleanly terminate the process
// via the same nt_setjmp/nt_longjmp unwind channel NtTerminateProcess uses
// (SLICE2-SPEC A3 mechanism, reused verbatim) -- turns an unrecoverable
// kernel crash into a one-line diagnosis + clean return to the shell.
long NtRaiseException(void *ExceptionRecord, void *Context, unsigned long FirstChance) {
    (void)Context; (void)FirstChance;
    u8 *er = (u8 *)ExceptionRecord;
    u32b code = 0;
    u64b addr = 0;
    if (er) {
        code = (u32b)er[0] | ((u32b)er[1] << 8) | ((u32b)er[2] << 16) | ((u32b)er[3] << 24);
        for (int i = 0; i < 8; i++) addr |= (u64b)er[0x10 + i] << (8 * i);
    }
    puts("CHALICE: NtRaiseException code="); puthex(code);
    puts(" addr="); puthex(addr);
    char name[64]; u64b off = 0;
    if (addr && nt_image_resolve(addr, name, sizeof(name), &off)) {
        puts(" ("); puts(name); puts("+"); puthex(off); puts(")");
    }
    puts("\n");
    nt_exit_code = (long)(0xC0000000UL | (code & 0xFFFF));
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    return STATUS_SUCCESS;   // unreachable
}

// CHALICE C3c-2k p1: NtRaiseHardError -- same clean-terminate treatment as
// NtRaiseException (optional per plan, cheap to add, same abort-legibility
// payoff for this sibling Wine fault-reporting path). Minimal: print
// ErrorStatus, terminate. Full response-option handling (Parameters array,
// Response out-param) not needed here -- nothing in this chain waits for a
// hard-error response, the process just needs to unwind cleanly instead of
// retry-looping into a bad_service crash the way NtRaiseException did.
long NtRaiseHardError(unsigned long ErrorStatus, unsigned long NumberOfParameters,
                       unsigned long UnicodeStringParameterMask, void *Parameters,
                       unsigned long ValidResponseOptions, void *Response) {
    (void)NumberOfParameters; (void)UnicodeStringParameterMask;
    (void)Parameters; (void)ValidResponseOptions; (void)Response;
    puts("CHALICE: NtRaiseHardError status="); puthex(ErrorStatus); puts("\n");
    nt_exit_code = (long)(ErrorStatus & 0xFFFFFFFFUL);
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    return STATUS_SUCCESS;   // unreachable
}

// =====================================================================
// Atom B — flat page-aligned VM arena (NtAllocateVirtualMemory /
// NtFreeVirtualMemory). HANDOFF: "flat arena OK, skip VAD tree".
// CHALICE C3c-2g: no longer backed by mem.c's 64 MB MAlloc heap (that
// starved the stock loader — ntdll's own image-load reservations alone
// exhausted it before kernel32.dll/kernelbase.dll could ever fit). Backed
// instead by ntfile.c's DLL-territory bump allocator (nt_territory_alloc,
// 0x45000000-0x7ff00000, ~0.75 GB of identity-mapped RAM under QEMU -m 1G,
// already the backing store for NtMapViewOfSection's image maps) — one
// shared territory bump pointer for VM-arena regions, FS-fallback file
// blobs (below), and mapped images, so none of the three can overlap.
// nt_territory_alloc zero-fills every page it hands back (ntdll assumes
// freshly committed VM reads as zero) and is page-rounded already, so no
// separate raw/aligned split is needed here (unlike the old MAlloc trick).
#define NT_PAGE_SIZE 4096UL
#define NT_VM_MAX_REGIONS 64

static struct {
    void   *raw;        // == aligned; territory is already page-aligned
    void   *aligned;     // page-aligned pointer handed to the PE side
    u64b    size;         // page-rounded size
    int     used;
} nt_vm_regions[NT_VM_MAX_REGIONS];

// CHALICE C4-8 (monad ruling #2): pool selection -- `high_first` asks for
// the HIGH (>=4GB, unconstrained) pool before the LOW (sub-4GB, 32-bit-
// visible) one, falling back to LOW if HIGH is exhausted; LOW is also the
// unconditional path when `high_first` is false (constrained/32-bit-
// ceiling requests never belong above 4GB). Caller (NtAllocateVirtualMemory
// below) derives `high_first` from ZeroBits==0 -- see the donor-grounded
// receipt at its call site.
static void *nt_vm_alloc_pool(u64b size, int high_first) {
    u8 *aligned = 0;
    if (high_first) aligned = nt_high_territory_alloc(size);   // ntfile.h
    if (!aligned) aligned = nt_territory_alloc(size);           // ntfile.h; LOW pool / fallback
    if (!aligned) return 0;
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (nt_vm_regions[i].used) continue;
        nt_vm_regions[i].raw = aligned;
        nt_vm_regions[i].aligned = aligned;
        nt_vm_regions[i].size = size;
        nt_vm_regions[i].used = 1;
        return aligned;
    }
    // no tracking slot left — the territory bump has no reclaim anyway, so
    // there's nothing to undo here; just fail this call cleanly.
    return 0;
}

// CHALICE C3a Atom 3A: does `addr` fall inside a live tracked VM region?
// Backs NtAllocateVirtualMemory's RESERVE-then-COMMIT-at-base growth path.
static int nt_vm_region_contains(void *addr) {
    u64b a = (u64b)addr;
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used) continue;
        u64b base = (u64b)nt_vm_regions[i].aligned;
        if (a >= base && a < base + nt_vm_regions[i].size) return 1;
    }
    return 0;
}

// Returns page-rounded size of the freed region via *out_size, or 0 if
// aligned wasn't a live region (unknown/already-freed base).
// CHALICE C3c-2g: the territory backing this region is bump-only (no
// reclaim) — matches HANDOFF's own "0.75 GB is ample for load-time, do NOT
// over-engineer a reclaiming allocator now". Free just marks the tracking
// slot unused (frees the *handle*, not the bump space); nt_vm_region_contains
// correctly stops seeing it as live either way.
static int nt_vm_free(void *aligned, u64b *out_size) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used || nt_vm_regions[i].aligned != aligned) continue;
        if (out_size) *out_size = nt_vm_regions[i].size;
        // CHALICE C-hello7 (continuation #3): nt_territory_free is now a
        // no-op accounting stub (ntfile.c receipt -- the free-list pool
        // this call used to feed was reverted per monad ruling). Kept as an
        // API-surface call for symmetry/documentation; the real reclaim for
        // this tracking slot is nt_vm_regions_release_since below.
        nt_territory_free((unsigned char *)nt_vm_regions[i].aligned, (unsigned long)nt_vm_regions[i].size);
        nt_vm_regions[i].used = 0;
        return 1;
    }
    return 0;
}

// CHALICE C-hello7 (continuation #3): process-epoch reclaim for the VM-arena
// tracking table, companion to ntfile.c's nt_territory_mark/
// _release_since. Root cause of a live pe8 in-single-boot exhaustion found
// AFTER the territory bump-pointer reset already landed: this table is a
// SEPARATE fixed-size (NT_VM_MAX_REGIONS=64) bookkeeping array from the
// territory bump pointer, and nothing was ever resetting IT across
// nt_boot_process calls -- every stock boot's own kernelbase heap-init
// negotiates many top-level VirtualAlloc reservations (observed: 15-20+
// live 0xFD0000 heaps kept per boot, matching real Wine/Windows kernelbase
// startup creating several genuinely-not-freed process heaps) and NONE of
// them are ever released by a real NtFreeVirtualMemory during this floor's
// single-process-at-a-time model, so their tracking slots stayed "used"
// forever, permanently consuming the 64-slot table across pe6+pe7+pe8 in
// one QEMU session -- nt_vm_alloc started failing (STATUS_NO_MEMORY) with
// HUNDREDS of MB of raw territory still free, because the search for a
// free TRACKING SLOT (not territory space) came up empty. Same fix shape
// as the territory bump reset: since a shell verb is a complete,
// non-overlapping process lifetime (SLICE3-SPEC), nothing after it returns
// needs its own heap regions to stay tracked -- so nt_boot_process drops
// every tracking slot this boot created (by high-water-mark address
// comparison, mirroring nt_territory_release_since's own mark test)
// alongside the bump-pointer reset it already does.
void nt_vm_regions_release_since(unsigned long mark) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (nt_vm_regions[i].used && (u64b)nt_vm_regions[i].aligned >= mark)
            nt_vm_regions[i].used = 0;
    }
}

// CHALICE C3c-2e p1: heap-init alloc I/O trace, gated on the same
// nt_dispatch_log_enabled flag pe6 already arms (shell.c) -- prints the
// full 64-bit in-value/out-value of the two in-out params (*BaseAddress,
// *RegionSize) plus AllocationType/Protect/status, so a truncated-width or
// unzeroed-page bug in the LLP64 in-out contract shows up directly in the
// serial trace rather than being inferred from the eventual fault.
static void nt_avm_log(const char *tag, void *base, u64b size, u64b atype,
                        u64b protect, long status) {
    if (!nt_dispatch_log_enabled) return;
    puts("CHALICE AVM "); puts(tag);
    puts(" base="); puthex((u64b)base);
    puts(" size="); puthex(size);
    puts(" atype="); puthex(atype);
    puts(" prot="); puthex(protect);
    puts(" status="); puthex((u64b)status);
    puts("\n");
}

// CHALICE C4-8 (monad ruling #3): loop-breaker hygiene -- N consecutive
// *failed* NtAllocateVirtualMemory requests for the SAME page-rounded size
// (both pools exhausted) means the caller is retrying with zero backoff --
// unbounded and unrecoverable at this floor's territory sizing (HANDOFF
// leg-7: Box64's own init settles into exactly this pattern, requesting a
// stuck-fixed 0x20000/128KB chunk forever once both pools fill). N default
// ~256 (parameterized, not hardcoded) -- one legible CHALICE FAULT line
// (size + resolved caller module+offset, via the real x9 caller LR stashed
// at the syscall frame's stub_lr slot, NT_FRAME_STUB_LR_OFF -- ntdisp.S/
// ntids.h receipt) then a clean process termination through the same
// nt_setjmp/nt_longjmp unwind channel NtTerminateProcess/NtRaiseException
// already use above -- never an infinite kernel-side spin again.
#define NT_AVM_LOOPBREAK_N 256

static u64b nt_avm_loopbreak_last_size = 0;
static unsigned long nt_avm_loopbreak_count = 0;

static void nt_avm_loopbreak_reset(void) {
    nt_avm_loopbreak_last_size = 0;
    nt_avm_loopbreak_count = 0;
}

static void nt_avm_loopbreak_hit(u64b pages) {
    if (pages == nt_avm_loopbreak_last_size) {
        nt_avm_loopbreak_count++;
    } else {
        nt_avm_loopbreak_last_size = pages;
        nt_avm_loopbreak_count = 1;
    }
    if (nt_avm_loopbreak_count < NT_AVM_LOOPBREAK_N) return;

    u64b teb; __asm__ volatile("mov %0, x18" : "=r"(teb));
    u64b frame = rd64((const u8 *)(teb + NT_TEB_SYSCALL_FRAME_OFF));
    u64b caller = frame ? rd64((const u8 *)(frame + NT_FRAME_STUB_LR_OFF)) : 0;
    char name[64]; u64b off = 0;
    puts("CHALICE FAULT: NtAllocateVirtualMemory loop-broken N=");
    puthex((u64b)nt_avm_loopbreak_count);
    puts(" size="); puthex(pages);
    puts(" caller="); puthex(caller);
    if (caller && nt_image_resolve(caller, name, sizeof(name), &off)) {
        puts(" ("); puts(name); puts("+"); puthex(off); puts(")");
    }
    puts(" -- terminating process cleanly\n");

    nt_exit_code = (long)STATUS_NO_MEMORY;
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    // unreachable if jmpbuf set (the normal case, a real guest process);
    // falls through to the ordinary STATUS_NO_MEMORY return otherwise,
    // counter left saturated so this line keeps firing rather than going
    // silent again.
}

long NtAllocateVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long ZeroBits,
                              unsigned long *RegionSize, unsigned long AllocationType,
                              unsigned long Protect) {
    (void)ProcessHandle;

    void   *in_base = BaseAddress ? *BaseAddress : 0;
    u64b    in_size = RegionSize  ? *RegionSize  : 0;
    nt_avm_log("IN ", in_base, in_size, AllocationType, Protect, 0);

    if (!BaseAddress || !RegionSize) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // exotic flags -> STATUS_INVALID_PARAMETER (SLICE2-SPEC Atom B); must
    // specify at least one of COMMIT/RESERVE, both together also accepted
    // ("MEM_COMMIT|MEM_RESERVE both = alloc").
    if (AllocationType == 0 || (AllocationType & ~(u64b)(MEM_COMMIT | MEM_RESERVE)) != 0) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    u64b size = *RegionSize;
    if (size == 0) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    u64b pages = (size + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);

    // DEVIATION (documented, not silent): a caller-supplied *BaseAddress
    // hint can't be honored by a flat bump/free-list arena (no VAD tree, no
    // control over where MAlloc's free list happens to have room) --
    // rejected explicitly rather than silently ignored, UNLESS it's the
    // Atom 3A growth path below. hello3's probe always passes NULL
    // (system-chosen base), so this path isn't exercised by the Atom B gate.
    if (*BaseAddress != 0) {
        // CHALICE C4-9 (monad ruling #3): commit-at-existing-base inside the
        // RESERVE/COMMIT arena -- donor virtual.c:5199 commit-only branch
        // (find_view must succeed, i.e. a live reservation must cover
        // base/size, else STATUS_NOT_MAPPED_VIEW). Checked first: arena VA
        // (>=8GB, ntfile.c NT_ARENA_BASE) never overlaps the LOW/HIGH pools'
        // tracked ranges (<5GB) the nt_vm_region_contains path below covers,
        // so this is an unambiguous routing split, not a priority guess.
        if (nt_arena_contains((u64b)*BaseAddress)) {
            if (!(AllocationType & MEM_COMMIT)) {
                nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
                return STATUS_INVALID_PARAMETER;
            }
            if (!nt_arena_commit((u64b)*BaseAddress, pages)) {
                // Donor: find_view fails (no covering reservation) ->
                // STATUS_NOT_MAPPED_VIEW (virtual.c:5199) -- this floor's
                // nt_arena_commit also returns 0 on HIGH-pool/L2-table-page
                // exhaustion; both are legitimate "can't commit" outcomes,
                // reported the same way (the wrong-base case is the common
                // real one; loop-breaker below still catches sustained
                // exhaustion-driven retries of THIS path via the caller's
                // own retry loop hitting the *BaseAddress==0 branch instead
                // in practice, per Wine's own heap-growth pattern).
                nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_NOT_MAPPED_VIEW);
                return STATUS_NOT_MAPPED_VIEW;
            }
            *RegionSize = pages;
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }

        // CHALICE C3a Atom 3A: RESERVE-then-COMMIT-at-given-base growth
        // (RtlCreateHeap prerequisite) -- *BaseAddress inside an
        // already-tracked region + MEM_COMMIT => bookkeeping no-op success
        // (flat arena: every tracked page is already committed). Untracked
        // base => keep rejecting (spec: "keep rejecting").
        if (!(AllocationType & MEM_COMMIT)) {
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        if (!nt_vm_region_contains(*BaseAddress)) {
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    // CHALICE C4-9 (monad ruling #1): ZeroBits==0 -> unconstrained ->
    // RESERVATION ARENA now, not the HIGH pool directly (that was the C4-8
    // interim design; superseded here -- HIGH pool still backs the arena's
    // COMMIT windows underneath, just no longer bump-allocates whole
    // regions up front regardless of RESERVE-vs-COMMIT). Donor-grounded
    // ZeroBits routing unchanged from C4-8 (receipt kept, still true):
    // wow64's own thunks ALWAYS pass a nonzero zero_bits (explicit or
    // wow64's own default_zero_bits=0x7fffffff, wine/dlls/wow64/
    // syscall.c:41) through get_zero_bits() (wine/dlls/wow64/
    // wow64_private.h:94-97) into the real 64-bit NtAllocateVirtualMemory;
    // ntdll's own get_zero_bits_limit() (wine/dlls/ntdll/unix/
    // unix_private.h:569-586) turns a nonzero zero_bits into a concrete
    // ceiling, while zero_bits==0 hits `limit=0` == no ceiling (wine/
    // dlls/ntdll/unix/virtual.c:5290-5297) -- genuinely unconstrained
    // native-ARM64 callers (wowbox64.dll's own dynarec heap/code-cache
    // growth) are exactly the ZeroBits==0 case, a 32-bit guest's own
    // VirtualAlloc never is.
    if (ZeroBits == 0) {
        unsigned char *base = nt_arena_reserve(pages);
        if (!base) {
            nt_avm_loopbreak_hit(pages);   // arena VA exhausted -- may terminate the process
            nt_avm_log("OUT", 0, pages, AllocationType, Protect, STATUS_NO_MEMORY);
            return STATUS_NO_MEMORY;
        }
        if (AllocationType & MEM_COMMIT) {
            // MEM_RESERVE|MEM_COMMIT together (or MEM_COMMIT alone with
            // base==0, same donor branch, virtual.c:5176-5180): commit the
            // whole freshly-reserved range up front.
            if (!nt_arena_commit((u64b)base, pages)) {
                nt_avm_loopbreak_hit(pages);   // HIGH-pool backing exhausted -- same loop-breaker as before
                nt_avm_log("OUT", base, pages, AllocationType, Protect, STATUS_NO_MEMORY);
                return STATUS_NO_MEMORY;
            }
        }
        nt_avm_loopbreak_reset();
        *BaseAddress = base;
        *RegionSize = pages;
        nt_avm_log("OUT", base, pages, AllocationType, Protect, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    // ZeroBits != 0: unchanged LOW-pool-only path (32-bit-ceiling requests
    // never belong in the arena, ruling #1).
    void *mem = nt_vm_alloc_pool(pages, 0);
    if (!mem) {
        nt_avm_loopbreak_hit(pages);   // CHALICE C4-8 ruling #3 -- may terminate the process
        nt_avm_log("OUT", 0, pages, AllocationType, Protect, STATUS_NO_MEMORY);
        return STATUS_NO_MEMORY;
    }
    nt_avm_loopbreak_reset();

    *BaseAddress = mem;
    *RegionSize = pages;
    nt_avm_log("OUT", mem, pages, AllocationType, Protect, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

// CHALICE C3c-2f p1: NtFreeVirtualMemory I/O trace, gated on the same
// nt_dispatch_log_enabled flag nt_avm_log above uses -- logs the in-out
// *BaseAddress/*RegionSize pair full 64-bit plus FreeType/status, AND
// whether nt_vm_free actually found the region (a partial/base-offset
// release from a live over-reservation would miss the exact-match lookup
// and surface here as "found=0" rather than silently doing nothing).
static void nt_fvm_log(const char *tag, void *base, u64b size, u64b ftype,
                        int found, long status) {
    if (!nt_dispatch_log_enabled) return;
    puts("CHALICE FVM "); puts(tag);
    puts(" base="); puthex((u64b)base);
    puts(" size="); puthex(size);
    puts(" ftype="); puthex(ftype);
    puts(" found="); puthex((u64b)found);
    puts(" status="); puthex((u64b)status);
    puts("\n");
}

long NtFreeVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                          unsigned long FreeType) {
    (void)ProcessHandle;

    void *in_base = BaseAddress ? *BaseAddress : 0;
    u64b  in_size = RegionSize  ? *RegionSize  : 0;
    nt_fvm_log("IN ", in_base, in_size, FreeType, -1, 0);

    if (!BaseAddress || !*BaseAddress) {
        nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (FreeType != MEM_RELEASE && FreeType != MEM_DECOMMIT) {
        nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // CHALICE C4-9 (monad ruling): arena addresses use their own
    // reservation table (nt_arena_release), not nt_vm_regions -- both
    // MEM_RELEASE and MEM_DECOMMIT are accounting-only here too (ruling:
    // "NtFreeVirtualMemory MEM_RELEASE/MEM_DECOMMIT = accounting-only OK"),
    // real reclaim is the per-boot wholesale reset (ntboot.c).
    if (nt_arena_contains((u64b)*BaseAddress)) {
        if (FreeType == MEM_RELEASE) {
            u64b freed = 0;
            int found = nt_arena_release((u64b)*BaseAddress, &freed);
            if (!found) {
                nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
                return STATUS_INVALID_PARAMETER;
            }
            if (RegionSize) *RegionSize = freed;
            nt_fvm_log("OUT", in_base, freed, FreeType, 1, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
        if (RegionSize) *RegionSize = (*RegionSize + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
        nt_fvm_log("OUT", in_base, RegionSize ? *RegionSize : 0, FreeType, 1, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    if (FreeType == MEM_RELEASE) {
        u64b freed = 0;
        int found = nt_vm_free(*BaseAddress, &freed);
        if (!found) {
            nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        if (RegionSize) *RegionSize = freed;
        nt_fvm_log("OUT", in_base, freed, FreeType, 1, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }
    // MEM_DECOMMIT: flat arena has no separate commit/decommit state (every
    // page is committed at alloc time) -- no-op success, matches HANDOFF's
    // "flat arena OK" simplification.
    if (RegionSize) *RegionSize = (*RegionSize + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    nt_fvm_log("OUT", in_base, RegionSize ? *RegionSize : 0, FreeType, 1, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-13: NtAllocateVirtualMemoryEx (svc 107, wine-arm64-syscalls.txt
// :103) -- the Ex form FEX's own heap-growth path calls where Box64 calls
// the plain NtAllocateVirtualMemory (measured live, leg-12: two "svc
// id=107 UNIMPLEMENTED" lines immediately before a genuine Data Abort
// INSIDE ntdll.dll's own heap code, far=0x7 -- a small-int deref off an
// uninitialized *ret/*size_ptr the real syscall never got to fill in).
// Donor-grounded (wine/dlls/ntdll/unix/virtual.c:5324 get_extended_params +
// :5384 NtAllocateVirtualMemoryEx, fetched this leg -- no local wine/
// donor clone present this session, HANDOFF's "main working copy has it"
// not true this leg either, same as leg-11 -- fetched the specific files
// needed via curl instead, same grounding standard): same core allocator
// as plain NtAllocateVirtualMemory (donor's allocate_virtual_memory), fed
// by a MEM_EXTENDED_PARAMETER array instead of a ZeroBits scalar -- this
// floor mirrors that by parsing MemExtendedParameterAddressRequirements
// (winnt.h _MEM_ADDRESS_REQUIREMENTS: LowestStartingAddress@0/
// HighestEndingAddress@8/Alignment@16, 24B; MEM_EXTENDED_PARAMETER itself:
// Type:8-bitfield QWORD@0 + union QWORD@8, 16B total, DECLSPEC_ALIGN(8))
// into the same LOW-vs-arena pool choice NtAllocateVirtualMemory's
// ZeroBits==0-vs-!=0 branch already makes: a HighestEndingAddress below
// the HIGH pool's own floor (4GB -- "keep me under 4GB", exactly what a
// WOW64-adjacent 32-bit-visible allocation needs, FEX's own likely use
// case per this leg's task) routes to the LOW pool; no constraint (no
// param, or a param with everything zero) is genuinely unconstrained,
// same as ZeroBits==0. MemExtendedParameterAttributeFlags/ImageMachine are
// parsed+logged only (no ARM64EC support on this floor, so their real
// donor-side effect -- commit_arm64ec_map/set_arm64ec_range, virtual.c
// :5218-5221 -- never applies here); MemExtendedParameterNumaNode/
// PartitionHandle/UserPhysicalHandle are logged+IGNORED, matching donor's
// own FIXME+continue exactly (get_extended_params never rejects these, so
// neither do we); any other/out-of-range Type -> STATUS_INVALID_PARAMETER,
// matching donor's own default case (donor is the ground truth here,
// followed exactly rather than inventing a distinct STATUS_NOT_SUPPORTED
// this floor's task text speculated about).
// =====================================================================
#define MEM_EXTENDED_PARAMETER_ADDRESSREQUIREMENTS 1UL
#define MEM_EXTENDED_PARAMETER_NUMANODE             2UL
#define MEM_EXTENDED_PARAMETER_PARTITIONHANDLE       3UL
#define MEM_EXTENDED_PARAMETER_USERPHYSICALHANDLE    4UL
#define MEM_EXTENDED_PARAMETER_ATTRIBUTEFLAGS        5UL
#define MEM_EXTENDED_PARAMETER_IMAGEMACHINE          6UL
// == ntfile.c's own NT_HIGH_TERRITORY_BASE (private #define there, no
// header exposes it -- duplicated value, not meaning, same convention
// ntids.h's NT_FRAME_STUB_LR_OFF receipt documents for this exact
// situation).
#define NT_C413_HIGH_POOL_FLOOR 0x100000000ULL

long NtAllocateVirtualMemoryEx(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                                unsigned long AllocationType, unsigned long Protect,
                                void *Parameters, unsigned long ParameterCount) {
    (void)ProcessHandle;

    void *in_base = BaseAddress ? *BaseAddress : 0;
    u64b  in_size = RegionSize ? *RegionSize : 0;
    nt_avm_log("EXIN ", in_base, in_size, AllocationType, Protect, 0);

    if (!BaseAddress || !RegionSize) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (ParameterCount && !Parameters) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    u64b limit_high = 0;      // 0 == unconstrained (donor's own default)
    int  have_addr_req = 0;

    const u8 *params = (const u8 *)Parameters;
    for (unsigned long i = 0; i < ParameterCount; i++) {
        const u8 *p = params + i * 16;   // MEM_EXTENDED_PARAMETER is 16B
        u64b type_field = rd64(p + 0);
        u32b type = (u32b)(type_field & 0xFFUL);   // Type:8 bits, LSB of the first QWORD
        u64b value = rd64(p + 8);

        if (type == MEM_EXTENDED_PARAMETER_ADDRESSREQUIREMENTS) {
            const u8 *req = (const u8 *)(u64b)value;   // Pointer -- MEM_ADDRESS_REQUIREMENTS*
            have_addr_req = 1;
            if (req) {
                u64b highest = rd64(req + 8);   // HighestEndingAddress
                if (highest != 0) limit_high = highest;
            }
        } else if (type == MEM_EXTENDED_PARAMETER_ATTRIBUTEFLAGS ||
                   type == MEM_EXTENDED_PARAMETER_IMAGEMACHINE) {
            // logged only, no ARM64EC support on this floor -- see receipt above.
        } else if (type == MEM_EXTENDED_PARAMETER_NUMANODE ||
                   type == MEM_EXTENDED_PARAMETER_PARTITIONHANDLE ||
                   type == MEM_EXTENDED_PARAMETER_USERPHYSICALHANDLE) {
            // donor FIXME+continue -- ignored, not rejected (get_extended_params ground truth).
        } else {
            if (nt_dispatch_log_enabled) {
                puts("CHALICE: NtAllocateVirtualMemoryEx invalid param type="); puthex(type); puts("\n");
            }
            nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtAllocateVirtualMemoryEx count="); puthex(ParameterCount);
        puts(" have_addr_req="); puthex((u64b)have_addr_req);
        puts(" limit_high="); puthex(limit_high); puts("\n");
    }

    // Donor: *ret set (growth-at-base) + any address-requirement param ->
    // STATUS_INVALID_PARAMETER (virtual.c:5384 "if (*ret && (align ||
    // limit_low || limit_high)) return STATUS_INVALID_PARAMETER", align/
    // limit_low not modeled separately here since this floor's flat arena
    // never honors a caller base hint anyway -- the have_addr_req flag
    // alone already captures "a requirements param was present").
    if (in_base != 0 && have_addr_req) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // Route: a HighestEndingAddress under the HIGH pool's own floor (4GB,
    // NT_C413_HIGH_POOL_FLOOR above) means "keep me under 4GB" -- exactly
    // this floor's existing LOW-pool contract (ZeroBits!=0's own routing,
    // NtAllocateVirtualMemory below) -- reuse it verbatim by delegating
    // with an equivalent nonzero ZeroBits value (any nonzero value routes
    // LOW-only there, same as any real WOW64 ZeroBits!=0 caller already
    // does -- the exact numeric value is never inspected past nonzero).
    unsigned long effective_zero_bits =
        (in_base == 0 && have_addr_req && limit_high != 0 && limit_high < NT_C413_HIGH_POOL_FLOOR) ? 1UL : 0UL;

    long status = NtAllocateVirtualMemory(ProcessHandle, BaseAddress, effective_zero_bits,
                                           RegionSize, AllocationType, Protect);
    nt_avm_log("EXOUT", *BaseAddress, *RegionSize, AllocationType, Protect, status);
    return status;
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

// CHALICE C4-6: single-process-kernel stash for ProcessWow64Information's
// answer -- the PEB32 address wow64.dll's own process_init reads back via
// NtQueryInformationProcess RIGHT AFTER RtlWow64GetProcessMachines
// succeeds (donor dlls/wow64/syscall.c:995 "NtQueryInformationProcess(
// GetCurrentProcess(), ProcessWow64Information, &peb32, sizeof(peb32),
// NULL); wow64info = (WOW64INFO *)(peb32 + 1);" -- confirmed live, pe10
// probe: an unanswered class 26 left peb32 as stack garbage, faulting on
// `wow64info->NativeSystemPageSize = ...` a few instructions later,
// wow64.dll+0x1d170). Set once by ntboot.c right after
// nt_build_wow64_teb32 succeeds (the SAME peb32 buffer that helper built).
static void *nt_current_wow64_peb32;
void nt_wow64_set_peb32(void *peb32) { nt_current_wow64_peb32 = peb32; }

long NtQueryInformationProcess(void *ProcessHandle, unsigned long ProcessInformationClass,
                                void *ProcessInformation, unsigned long ProcessInformationLength,
                                unsigned long *ReturnLength) {
    (void)ProcessHandle;
    if (ProcessInformationClass == 26 /* ProcessWow64Information */) {
        if (!ProcessInformation) return STATUS_INVALID_PARAMETER;
        if (ProcessInformationLength < 8) return STATUS_INFO_LENGTH_MISMATCH;
        wr64((u8 *)ProcessInformation, (u64b)nt_current_wow64_peb32);
        if (ReturnLength) *ReturnLength = 8;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtQueryInformationProcess ProcessWow64Information -> peb32=");
            puthex((u64b)nt_current_wow64_peb32); puts("\n");
        }
        return STATUS_SUCCESS;
    }
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

// =====================================================================
// CHALICE C4-9: NtQueryInformationThread, ThreadWow64Context (class 29)
// ONLY -- the exact next-required syscall pe10 measured this leg. Ground
// truth (disassembly, not guessed): wowbox64.dll's OWN BTCpuGetContext
// (stock/wow64-chain/wowbox64.dll+0x20e0) tail-calls straight into this
// real syscall (x1=w1=0x1d=29, x2=ContextPtr, x3=w3=0x2cc=sizeof(I386_
// CONTEXT), x4=xzr) -- confirmed live: our kernel's own "svc id=37
// UNIMPLEMENTED" line fires at exactly this call site (pe10 probe), and
// the caller (wine/dlls/wow64/syscall.c:1069 thread_init) then reads
// ctx.Esp (I386_CONTEXT offset 0xc4, wine/include/winnt.h _I386_CONTEXT)
// to compute `ctx_ptr = (I386_CONTEXT*)ctx.Esp - 1; *ctx_ptr = ctx;` --
// an unanswered call leaves ctx.Esp=0 (thread_init's own local `ctx =
// {CONTEXT_I386_FULL}` zero-initializer), so ctx_ptr underflows to
// 0-sizeof(ctx) and the very next store faults (live-confirmed: elr=
// ntdll.dll+0x68458, inside memcpy's own store loop, far=
// 0xFFFFFFFFFFFFFD34 == -0x2CC == -sizeof(I386_CONTEXT), exactly this).
// Fix: answer with the x86 guest thread's real initial Eip/Esp (stashed
// by ntboot.c right after building the wow64 TEB32, same per-boot-state
// pattern as nt_wow64_set_peb32 above) -- Esp points into a FRESH, LOW-
// territory (<4GB, so a 32-bit ESP can address it), zeroed, writable
// stack block just for this, distinct from the ARM64 boot thread's own
// stack (still live/in-use on the native call chain making THIS very
// syscall).
// CTX32_SIZE mirrors the WOW64_CPURESERVED section's own I386_CONTEXT_SIZE
// (0x2cc, defined later in this file) -- same value, distinct name here
// since that other #define lives below this point in the file (C needs it
// declared before use, and moving either section isn't worth the diff).
#define CTX32_SIZE       0x2CC
#define CTX32_EBP_OFF    0xB4
#define CTX32_EIP_OFF    0xB8
#define CTX32_SEGCS_OFF  0xBC
#define CTX32_EFLAGS_OFF 0xC0
#define CTX32_ESP_OFF    0xC4
#define CTX32_SEGSS_OFF  0xC8
// Real WOW64 flat usermode selectors (donor-known constants, x86 Windows
// ABI -- CS=0x1B, SS=0x23 under the flat 32-bit code/data/stack segments
// WOW64 sets up for every 32-bit thread); EFlags=0x202 (IF set + the
// always-1 reserved bit 1) is the standard initial-thread default.
#define WOW64_SEGCS 0x1BUL
#define WOW64_SEGSS 0x23UL
#define WOW64_EFLAGS 0x202UL

static u64b nt_wow64_ctx_eip, nt_wow64_ctx_esp;
static int  nt_wow64_ctx_valid;

// Called by ntboot.c once per wow64 boot, right after nt_build_wow64_teb32
// succeeds -- same call-site convention as nt_wow64_set_peb32 just above.
void nt_wow64_set_initial_context(unsigned long eip, unsigned long esp) {
    nt_wow64_ctx_eip = eip;
    nt_wow64_ctx_esp = esp;
    nt_wow64_ctx_valid = 1;
}

// CHALICE C4-11: see ntsvc.h receipt -- WOW64_CPURESERVED layout (this
// file's own WOW64_CPURESERVED_HDR_SIZE/CTX32_* offsets above): Flags(2)+
// Machine(2) header, then an inline I386_CONTEXT starting at +4. Same
// field values NtQueryInformationThread(ThreadWow64Context) already
// answers with (Ebp==Esp empty-frame convention, WOW64_SEGCS/SEGSS/
// EFLAGS constants above) -- one real write, not a guess: this is the
// exact struct native_pass0 dereferences directly (measured live, no
// intervening syscall between TEB build and the fault).
void nt_wow64_cpureserved_set_context(void *cpureserved_v, unsigned long eip, unsigned long esp) {
    u8 *cr = (u8 *)cpureserved_v;
    if (!cr) return;
    // literal 4 == WOW64_CPURESERVED_HDR_SIZE (Flags+Machine, defined later
    // in this file where nt_build_wow64_teb32 lives -- can't forward-
    // reference a #define, so the value is duplicated here, not the meaning).
    u8 *ctx = cr + 4;
    wr32(ctx + CTX32_EBP_OFF, (u32b)esp);
    wr32(ctx + CTX32_EIP_OFF, (u32b)eip);
    wr32(ctx + CTX32_SEGCS_OFF, (u32b)WOW64_SEGCS);
    wr32(ctx + CTX32_EFLAGS_OFF, (u32b)WOW64_EFLAGS);
    wr32(ctx + CTX32_ESP_OFF, (u32b)esp);
    wr32(ctx + CTX32_SEGSS_OFF, (u32b)WOW64_SEGSS);
}

long NtQueryInformationThread(void *ThreadHandle, unsigned long ThreadInformationClass,
                               void *ThreadInformation, unsigned long ThreadInformationLength,
                               unsigned long *ReturnLength) {
    (void)ThreadHandle;
    if (ThreadInformationClass != 29 /* ThreadWow64Context */) return STATUS_NOT_IMPLEMENTED;
    if (!ThreadInformation) return STATUS_INVALID_PARAMETER;
    if (ThreadInformationLength < CTX32_SIZE) return STATUS_INFO_LENGTH_MISMATCH;
    if (!nt_wow64_ctx_valid) return STATUS_NOT_IMPLEMENTED;   // non-wow64 boot -- nothing to answer with

    u8 *out = (u8 *)ThreadInformation;
    for (unsigned long i = 0; i < CTX32_SIZE; i++) out[i] = 0;
    wr32(out + CTX32_EBP_OFF, (u32b)nt_wow64_ctx_esp);   // Ebp==Esp, empty-frame convention (no caller frame yet)
    wr32(out + CTX32_EIP_OFF, (u32b)nt_wow64_ctx_eip);
    wr32(out + CTX32_SEGCS_OFF, (u32b)WOW64_SEGCS);
    wr32(out + CTX32_EFLAGS_OFF, (u32b)WOW64_EFLAGS);
    wr32(out + CTX32_ESP_OFF, (u32b)nt_wow64_ctx_esp);
    wr32(out + CTX32_SEGSS_OFF, (u32b)WOW64_SEGSS);

    if (ReturnLength) *ReturnLength = CTX32_SIZE;
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQueryInformationThread ThreadWow64Context -> eip=");
        puthex(nt_wow64_ctx_eip); puts(" esp="); puthex(nt_wow64_ctx_esp); puts("\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-11: NtSetInformationThread, ThreadWow64Context (class 29)
// ONLY -- measured live (pe10 probe): svc id=13, wine-arm64-syscalls.txt:
// 210, fires with class=0x1D(29)/len=0x2CC(=CTX32_SIZE) right after our
// existing NtQueryInformationThread(ThreadWow64Context) answers eip/esp --
// the documented real-Windows SET counterpart of that same class (Nt{Query,
// Set}InformationThread ThreadWow64Context get/set a thread's WOW64
// CONTEXT; this is a standard NT contract, not Wine/Hangover-specific --
// no donor grounding needed beyond the same I386_CONTEXT field offsets
// this file already uses for the GET side, CTX32_EIP_OFF/CTX32_ESP_OFF
// above). BTCpuThreadInit/BTCpuProcessInit calls this to install the
// (possibly backend-adjusted) x86 context as the thread's authoritative
// WOW64 context before continuing -- our previously-silent NtStub->
// STATUS_NOT_IMPLEMENTED left the caller believing the write failed/no-
// oped and it dereferenced forward assuming success (wowbox64.dll+0xF88BC
// Data Abort, far=0x2, immediately downstream of this exact call site).
// Fix: parse Eip/Esp back OUT of the incoming buffer into the same
// nt_wow64_ctx_eip/esp stash nt_wow64_set_initial_context populates, so a
// later GET reflects whatever the backend just SET (single-thread kernel,
// one stash, same simplification the rest of this WOW64 floor uses) --
// then STATUS_SUCCESS.
// =====================================================================
long NtSetInformationThread(void *ThreadHandle, unsigned long ThreadInformationClass,
                             const void *ThreadInformation, unsigned long ThreadInformationLength) {
    (void)ThreadHandle;
    if (ThreadInformationClass != 29 /* ThreadWow64Context */) return STATUS_NOT_IMPLEMENTED;
    if (!ThreadInformation) return STATUS_INVALID_PARAMETER;
    if (ThreadInformationLength < CTX32_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    const u8 *in = (const u8 *)ThreadInformation;
    nt_wow64_ctx_eip = rd32(in + CTX32_EIP_OFF);
    nt_wow64_ctx_esp = rd32(in + CTX32_ESP_OFF);
    nt_wow64_ctx_valid = 1;

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtSetInformationThread ThreadWow64Context <- eip=");
        puthex(nt_wow64_ctx_eip); puts(" esp="); puthex(nt_wow64_ctx_esp); puts("\n");
    }
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
// CHALICE C4-6: NtQuerySystemInformationEx, ONE class only --
// SystemSupportedProcessorArchitectures(181)/...Architectures2(230) --
// the call RtlWow64GetProcessMachines (donor wine/dlls/ntdll/process.c:
// 107-124) makes to learn current/native machine, which loader_init's
// init_wow64 path (via wow64.dll's own process_init, dlls/wow64/
// syscall.c:984) calls FIRST, before touching anything else -- observed
// live (pe10 probe) as the exact next required syscall (id 203, wine-
// arm64-syscalls.txt:205) once wow64.dll itself is mapped+relocated.
// Struct (winnt.h _SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION):
// ONE bitfield DWORD per entry -- Machine:16, KernelMode:1, UserMode:1,
// Native:1, Process:1, WoW64Container:1, ReservedZero0:11 -- array is
// Machine==0-terminated (donor system.c:4176 `for (i=0;machines[i].
// Machine;i++)`). This floor is single-arch-pair only (this C4 leg):
// entry0 = native (ARM64, 0xAA64), entry1 = the guest/current machine
// (0x14C i386, Process=1), entry2 = terminator. `query` is a HANDLE (the
// process to report Process=1 for) -- unused here (single-process kernel,
// same simplification NtQueryInformationProcess above already takes: the
// live x18/TEB IS "the current process", there is no other to compare
// against), so the guest/current entry is unconditionally Process=1.
// =====================================================================
#define SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES 181
#define SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES2 230

long NtQuerySystemInformationEx(unsigned long SystemInformationClass, void *Query, unsigned long QueryLength,
                                 void *SystemInformation, unsigned long SystemInformationLength,
                                 unsigned long *ReturnLength) {
    (void)Query; (void)QueryLength;
    if (SystemInformationClass != SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES &&
        SystemInformationClass != SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES2)
        return STATUS_NOT_IMPLEMENTED;

    u32b need = 3 * 4;   // 2 real entries + 1 Machine==0 terminator, 4B each
    if (ReturnLength) *ReturnLength = need;
    if (!SystemInformation) return STATUS_INVALID_PARAMETER;
    if (SystemInformationLength < need) return STATUS_INFO_LENGTH_MISMATCH;   // no STATUS_BUFFER_TOO_SMALL constant this floor, same-shape "too small" signal

    u8 *out = (u8 *)SystemInformation;
    // entry0: native == ARM64, this floor's own host arch.
    wr32(out + 0, 0xAA64UL | (1UL << 16) /* KernelMode */ | (1UL << 17) /* UserMode */ |
                  (1UL << 18) /* Native */);
    // entry1: current/guest == i386, always Process=1 (single-process kernel).
    wr32(out + 4, 0x14CUL | (1UL << 17) /* UserMode */ | (1UL << 19) /* Process */ |
                  (1UL << 20) /* WoW64Container */);
    wr32(out + 8, 0);   // terminator

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQuerySystemInformationEx class="); puthex((u64b)SystemInformationClass);
        puts(" -> native=0xAA64 current=0x14C\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-13: real ARM64 ID-register reads. This kernel runs at EL1
// (kmain.c mmu_on) -- ID_AA64* register reads are unconditionally legal
// there (ARM ARM D17.2: EL1 access is trapped to EL2 only if a hypervisor
// sets HCR_EL2.TID3, and this floor never installs an EL2 -- we ARE the
// only privilege level below EL1's own guest, same "we ARE the unix side"
// framing nt_kuser_init already uses). Direct mrs, not the Linux-side
// trap+emulate dance donor wine/dlls/ntdll/unix/system.c's
// get_core_id_regs_arm64 needs (that code reads via getauxval(HWCAP_CPUID)
// + an emulated mrs trap because IT runs at EL0 under a real Linux kernel;
// we have no such intermediary to trap through, so we skip that whole
// mechanism and read the real hardware register directly).
// =====================================================================
unsigned long nt_read_id_aa64isar0(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, id_aa64isar0_el1" : "=r"(v));
    return v;
}
unsigned long nt_read_id_aa64mmfr1(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(v));
    return v;
}
unsigned long nt_read_id_aa64mmfr2(void) {
    unsigned long v;
    __asm__ volatile("mrs %0, id_aa64mmfr2_el1" : "=r"(v));
    return v;
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

    // ProcessorFeatures[]: two tiers, per donor dlls/ntdll/unix/system.c
    // init_shared_data_cpuinfo (fetched this leg, non-__arm__/aarch64
    // branch) --
    // (1) unconditional-TRUE-on-any-AArch64-host facts (NOT host-specific,
    //     so no mrs read backs these -- matches donor exactly, every
    //     AArch64 CPU real Windows-on-ARM64 ever ran on has them):
    k[NT_KUSER_PROCESSORFEATURES_OFF + 18] = 1;   // PF_ARM_VFP_32_REGISTERS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 19] = 1;   // PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 25] = 1;   // PF_ARM_64BIT_LOADSTORE_ATOMIC
    k[NT_KUSER_PROCESSORFEATURES_OFF + 29] = 1;   // PF_ARM_V8_INSTRUCTIONS_AVAILABLE

    // (2) host-SPECIFIC facts -- CHALICE C4-13: derived from a real
    // ID_AA64ISAR0_EL1 mrs read (nt_read_id_aa64isar0 above), IRON law "no
    // hardcoded feature lists, parameterize nothing by hand" -- mapping
    // table is the ARM ARM's ID_AA64ISAR0_EL1 field layout (also exactly
    // the 3 PF_* box64-for-WOW64's DetectHostCpuFeatures actually queries
    // via IsProcessorFeaturePresent, box64/src/os/hostext_wine.c:8, v0.4.2
    // donor in-tree gitignored quarry -- SHA1/SHA2/PMULL/RDM/SHA3/SM3/SM4/
    // DP/FHM/TS/TLB/RNDR fields exist in the same register but box64 never
    // asks their PF_* counterparts, so this floor doesn't derive them
    // either -- exactly the surface consumed, no more, no less):
    //   AES    bits[7:4]   nonzero -> PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE(30)
    //   CRC32  bits[19:16] nonzero -> PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE(31)
    //   Atomic bits[23:20] nonzero -> PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE(34)
    {
        u64b isar0 = nt_read_id_aa64isar0();
        if (((isar0 >> 4)  & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 30] = 1; // AES
        if (((isar0 >> 16) & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 31] = 1; // CRC32
        if (((isar0 >> 20) & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 34] = 1; // Atomic/LSE
    }
}

// =====================================================================
// CHALICE C3c Atom 3C-1 — dispatcher call-log (removable-later debug aid,
// see ntsvc.h). Runtime-flag-gated (default OFF); shell.c's pe6 verb turns
// it on only around nt_boot_process so earlier gates (pe1-pe5, zc, aot,
// ...) stay quiet. Called from ntdisp.S's nt_dispatch on every service
// entry (`bl nt_dispatch_log`, both the resolved-handler path and the
// bad_service/out-of-range path).
// =====================================================================
int nt_dispatch_log_enabled;
static u64b nt_dispatch_log_count;
#define NT_DISPATCH_LOG_FIRST_N 64

static const char *nt_svc_name(u64b id) {
    switch (id) {
    case NT_SVC_NTALLOCATEVIRTUALMEMORY:    return "NtAllocateVirtualMemory";
    case NT_SVC_NTALLOCATEVIRTUALMEMORYEX:  return "NtAllocateVirtualMemoryEx";
    case NT_SVC_NTFREEVIRTUALMEMORY:        return "NtFreeVirtualMemory";
    case NT_SVC_NTPROTECTVIRTUALMEMORY:     return "NtProtectVirtualMemory";
    case NT_SVC_NTCREATESECTION:            return "NtCreateSection";
    case NT_SVC_NTMAPVIEWOFSECTION:         return "NtMapViewOfSection";
    case NT_SVC_NTCREATEFILE:               return "NtCreateFile";
    case NT_SVC_NTOPENFILE:                 return "NtOpenFile";
    case NT_SVC_NTREADFILE:                 return "NtReadFile";
    case NT_SVC_NTWRITEFILE:                return "NtWriteFile";
    case NT_SVC_NTCLOSE:                    return "NtClose";
    case NT_SVC_NTCREATETHREAD:             return "NtCreateThread";
    case NT_SVC_NTRESUMETHREAD:             return "NtResumeThread";
    case NT_SVC_NTTERMINATETHREAD:          return "NtTerminateThread";
    case NT_SVC_NTTERMINATEPROCESS:         return "NtTerminateProcess";
    case NT_SVC_NTWAITFORSINGLEOBJECT:      return "NtWaitForSingleObject";
    case NT_SVC_NTCREATEEVENT:              return "NtCreateEvent";
    case NT_SVC_NTQUERYINFORMATIONPROCESS:  return "NtQueryInformationProcess";
    case NT_SVC_NTQUERYINFORMATIONTHREAD:   return "NtQueryInformationThread";
    case NT_SVC_NTSETINFORMATIONTHREAD:      return "NtSetInformationThread";
    case NT_SVC_NTQUERYSYSTEMINFORMATION:   return "NtQuerySystemInformation";
    case NT_SVC_NTQUERYSYSTEMINFORMATIONEX: return "NtQuerySystemInformationEx";
    case NT_SVC_NTCONTINUE:                 return "NtContinue";
    case NT_SVC_NTCREATEKEY:                return "NtCreateKey";
    case NT_SVC_NTOPENKEY:                  return "NtOpenKey";
    case NT_SVC_NTQUERYVALUEKEY:            return "NtQueryValueKey";
    case NT_SVC_NTQUERYVIRTUALMEMORY:       return "NtQueryVirtualMemory";
    case NT_SVC_NTQUERYSECTION:              return "NtQuerySection";
    case NT_SVC_NTRAISEEXCEPTION:            return "NtRaiseException";
    case NT_SVC_NTRAISEHARDERROR:            return "NtRaiseHardError";
    case NT_SVC_NTINITIALIZENLSFILES:        return "NtInitializeNlsFiles";
    case NT_SVC_NTGETNLSSECTIONPTR:           return "NtGetNlsSectionPtr";
    case NT_SVC_NTSETVALUEKEY:                return "NtSetValueKey";
    case NT_SVC_NTQUERYDEFAULTLOCALE:          return "NtQueryDefaultLocale";
    default: return 0;
    }
}

static void nt_log_putdec(u64b v) {
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + (char)(v % 10); v /= 10; } while (v);
    puts(&b[i]);
}

void nt_dispatch_log_reset(void) { nt_dispatch_log_count = 0; }

void nt_dispatch_log(unsigned long id_arg, void *handler) {
    if (!nt_dispatch_log_enabled) return;
    u64b id = (u64b)id_arg;
    nt_dispatch_log_count++;
    int unknown = (handler == 0);
    int unimpl  = (!unknown && handler == (void *)NtStub);
    if (!unknown && !unimpl && nt_dispatch_log_count > NT_DISPATCH_LOG_FIRST_N) return;

    puts("CHALICE: svc id="); nt_log_putdec(id);
    const char *name = unknown ? 0 : nt_svc_name(id);
    if (name) { puts(" ("); puts(name); puts(")"); }
    if (unknown) puts(" UNKNOWN");
    else if (unimpl) puts(" UNIMPLEMENTED");
    puts("\n");
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
    // CHALICE C4-13: NtAllocateVirtualMemoryEx(ProcessHandle, BaseAddress,
    // RegionSize, AllocationType, Protect, Parameters, ParameterCount) -- 7 args.
    nt_svc_table0[NT_SVC_NTALLOCATEVIRTUALMEMORYEX] = (void *)NtAllocateVirtualMemoryEx;
    nt_svc_argtable0[NT_SVC_NTALLOCATEVIRTUALMEMORYEX] = 7 * 8;
    nt_svc_table0[NT_SVC_NTFREEVIRTUALMEMORY] = (void *)NtFreeVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTFREEVIRTUALMEMORY] = 4 * 8;
    nt_svc_table0[NT_SVC_NTQUERYINFORMATIONTHREAD] = (void *)NtQueryInformationThread;
    nt_svc_table0[NT_SVC_NTQUERYINFORMATIONPROCESS] = (void *)NtQueryInformationProcess;
    nt_svc_argtable0[NT_SVC_NTQUERYINFORMATIONPROCESS] = 5 * 8;
    nt_svc_argtable0[NT_SVC_NTQUERYINFORMATIONTHREAD] = 5 * 8;
    // CHALICE C4-11: NtSetInformationThread(ThreadHandle, ThreadInformationClass,
    // ThreadInformation, ThreadInformationLength) -- 4 args.
    nt_svc_table0[NT_SVC_NTSETINFORMATIONTHREAD] = (void *)NtSetInformationThread;
    nt_svc_argtable0[NT_SVC_NTSETINFORMATIONTHREAD] = 4 * 8;
    nt_svc_table0[NT_SVC_NTQUERYSYSTEMINFORMATION] = (void *)NtQuerySystemInformation;
    nt_svc_argtable0[NT_SVC_NTQUERYSYSTEMINFORMATION] = 4 * 8;
    // CHALICE C4-6: NtQuerySystemInformationEx(class, query, query_len,
    // info, size, ret_size) -- 6 args.
    nt_svc_table0[NT_SVC_NTQUERYSYSTEMINFORMATIONEX] = (void *)NtQuerySystemInformationEx;
    nt_svc_argtable0[NT_SVC_NTQUERYSYSTEMINFORMATIONEX] = 6 * 8;
    nt_svc_table0[NT_SVC_NTCONTINUE] = (void *)NtContinue;
    nt_svc_argtable0[NT_SVC_NTCONTINUE] = 2 * 8;

    // ---- CHALICE C3a Atom 3A: loader-path services (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTOPENFILE] = (void *)NtOpenFile;
    nt_svc_argtable0[NT_SVC_NTOPENFILE] = 6 * 8;
    nt_svc_table0[NT_SVC_NTREADFILE] = (void *)NtReadFile;
    nt_svc_argtable0[NT_SVC_NTREADFILE] = 9 * 8;
    nt_svc_table0[NT_SVC_NTCREATESECTION] = (void *)NtCreateSection;
    nt_svc_argtable0[NT_SVC_NTCREATESECTION] = 7 * 8;
    nt_svc_table0[NT_SVC_NTMAPVIEWOFSECTION] = (void *)NtMapViewOfSection;
    nt_svc_argtable0[NT_SVC_NTMAPVIEWOFSECTION] = 10 * 8;
    nt_svc_table0[NT_SVC_NTPROTECTVIRTUALMEMORY] = (void *)NtProtectVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTPROTECTVIRTUALMEMORY] = 5 * 8;
    nt_svc_table0[NT_SVC_NTQUERYVIRTUALMEMORY] = (void *)NtQueryVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTQUERYVIRTUALMEMORY] = 6 * 8;
    // CHALICE C3c-2i: NtQuerySection(SectionHandle, SectionInformationClass,
    // SectionInformation, SectionInformationLength, ReturnLength) -- 5 args.
    nt_svc_table0[NT_SVC_NTQUERYSECTION] = (void *)NtQuerySection;
    nt_svc_argtable0[NT_SVC_NTQUERYSECTION] = 5 * 8;

    // ---- CHALICE C3c-2k p1: legible-abort handlers ----
    nt_svc_table0[NT_SVC_NTRAISEEXCEPTION] = (void *)NtRaiseException;
    nt_svc_argtable0[NT_SVC_NTRAISEEXCEPTION] = 3 * 8;
    nt_svc_table0[NT_SVC_NTRAISEHARDERROR] = (void *)NtRaiseHardError;
    nt_svc_argtable0[NT_SVC_NTRAISEHARDERROR] = 6 * 8;

    // ---- CHALICE C3c-2n: NtInitializeNlsFiles(BaseAddress, DefaultLocaleId,
    // DefaultCasingTableSize) -- 3 args (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTINITIALIZENLSFILES] = (void *)NtInitializeNlsFiles;
    nt_svc_argtable0[NT_SVC_NTINITIALIZENLSFILES] = 3 * 8;

    // ---- CHALICE C3c-2n: NtGetNlsSectionPtr(Type, Id, Unknown, BaseAddress,
    // Size) -- 5 args (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTGETNLSSECTIONPTR] = (void *)NtGetNlsSectionPtr;
    nt_svc_argtable0[NT_SVC_NTGETNLSSECTIONPTR] = 5 * 8;
    // CHALICE C3c-2o: minimal in-memory registry floor -- see ntfile.c for
    // the per-handler NTSTATUS-contract receipts.
    nt_svc_table0[NT_SVC_NTCREATEKEY] = (void *)NtCreateKey;
    nt_svc_argtable0[NT_SVC_NTCREATEKEY] = 7 * 8;
    nt_svc_table0[NT_SVC_NTOPENKEY] = (void *)NtOpenKey;
    nt_svc_argtable0[NT_SVC_NTOPENKEY] = 3 * 8;
    nt_svc_table0[NT_SVC_NTQUERYVALUEKEY] = (void *)NtQueryValueKey;
    nt_svc_argtable0[NT_SVC_NTQUERYVALUEKEY] = 6 * 8;
    nt_svc_table0[NT_SVC_NTSETVALUEKEY] = (void *)NtSetValueKey;
    nt_svc_argtable0[NT_SVC_NTSETVALUEKEY] = 6 * 8;
    nt_svc_table0[NT_SVC_NTQUERYDEFAULTLOCALE] = (void *)NtQueryDefaultLocale;
    nt_svc_argtable0[NT_SVC_NTQUERYDEFAULTLOCALE] = 2 * 8;

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
// CHALICE C-hello7 fix (pe8 gate): TEB.StaticUnicodeString, offset
// confirmed by disassembling stock kernelbase.dll!GetModuleHandleA ->
// file_name_AtoW (AllocateDestinationString=FALSE path): `add x19, x18,
// #0x1258` builds the destination UNICODE_STRING* passed to
// RtlAnsiStringToUnicodeString/RtlOemStringToUnicodeString. Left zeroed
// (MaximumLength=0), that Rtl call returns STATUS_BUFFER_OVERFLOW for ANY
// non-empty ANSI name, which GetModuleHandleA turns into ERROR_MOD_NOT_FOUND
// (0x7E) -- root cause of GetModuleHandleA("kernel32.dll") always failing
// while GetModuleHandleA(NULL) (a different, PEB-based path) works fine.
#define TEB_STATICUNICODESTRING_OFF 0x1258
#define TEB_STATICUNICODEBUFFER_OFF 0x1268
#define TEB_STATICUNICODEBUFFER_LEN 0x2C0   // bytes; ballpark of real Windows' capacity, plenty for any single module/path name
// CHALICE C4-3: TEB_SIZE 0x1600->0x3000 -- our TEB layout tracks the REAL
// TEB64 (winternl.h _STRUCT64 TEB, C_ASSERT sizeof==0x1838: TlsSlots@0x1480,
// WowTebOffset@0x180c) closely enough that 0x1600 didn't even reach
// WowTebOffset, let alone the TEB32 view Wine places at teb+0x2000
// (wine/dlls/ntdll/unix/unix_private.h:176 `teb_offset = 0x2000`, virtual.c
// :4002 `TEB32 *teb32 = (TEB32*)((char*)ptr + teb_offset)`). 0x3000 covers
// the real TEB64 body (0x1838) AND a full TEB32 (0x1000, winternl.h:1061
// _TEB32 C_ASSERT sizeof==0x1000) at +0x2000..+0x3000, in ONE allocation --
// same block nt_build_teb already hands back, no parallel allocator. Purely
// additive (every existing offset below is unchanged; callers that never
// touch the WOW64 fields see only a bigger, still-zeroed buffer -- no
// regression risk, same growth pattern as scar #10's 0x400->0x1600 bump).
#define TEB_SIZE              0x3000
// ---- CHALICE C4-3: TEB64 WOW64 fields (winternl.h _STRUCT64 TEB, i.e. the
// SAME struct this file's other TEB_* offsets above already track) ----
#define TEB64_TLSSLOTS_OFF     0x1480   // winternl.h:1238 "TlsSlots[64]; /* 1480 */"
#define TEB64_WOWTEBOFFSET_OFF 0x180c   // winternl.h:1300 "WowTebOffset; /* 180c */"
#define TEB_WOW64_VIEW_OFF     0x2000   // unix_private.h:176 teb_offset
#define WOW64_TLS_CPURESERVED  1        // winternl.h:1327
#define WOW64_TLS_WOW64INFO    10       // winternl.h:1336

// ---- TEB32 (winternl.h:1061 _TEB32, C_ASSERT sizeof==0x1000) ----
#define TEB32_SIZE                     0x1000
#define TEB32_TIB_EXCEPTIONLIST_OFF    0x00
#define TEB32_TIB_FIBERDATA_OFF        0x10
#define TEB32_TIB_SELF_OFF             0x18   // winnt.h:2564 NT_TIB32.Self
#define TEB32_PEB_OFF                  0x30
#define TEB32_STATICUNICODESTRING_OFF  0xbf8
#define TEB32_STATICUNICODEBUFFER_OFF  0xc00
#define TEB32_STATICUNICODEBUFFER_LEN  0x20a  // 261 WCHAR * 2 (winternl.h:1113 StaticUnicodeBuffer[261])
#define TEB32_GDIBATCHCOUNT_OFF        0xf70
#define TEB32_WOWTEBOFFSET_OFF         0xfdc
#define US32_BUFFER_OFF                0x04   // UNICODE_STRING32 (winternl.h:673): Length(2)@0,MaximumLength(2)@2,Buffer(4)@4

// ---- PEB32 (winternl.h:833 _PEB32, C_ASSERT sizeof==0x480) ----
#define PEB32_SIZE                    0x480
#define PEB32_NUMBEROFPROCESSORS_OFF  0x64
#define PEB32_OSMAJORVERSION_OFF      0xa4
#define PEB32_OSMINORVERSION_OFF      0xa8
#define PEB32_OSBUILDNUMBER_OFF       0xac

// ---- WOW64INFO (winternl.h:4395, C_ASSERT sizeof==40); stored
// immediately after PEB32 (wine/dlls/wow64/syscall.c:1000 "wow64info =
// (WOW64INFO *)(peb32 + 1)") ----
#define WOW64INFO_SIZE                     0x28
#define WOW64INFO_NATIVESYSTEMPAGESIZE_OFF 0x00
#define WOW64INFO_CPUFLAGS_OFF             0x04
#define WOW64INFO_CROSSPROCESSWORKLIST_OFF 0x18
#define WOW64INFO_NATIVEMACHINETYPE_OFF    0x20
#define WOW64INFO_EMULATEDMACHINETYPE_OFF  0x22

// ---- CROSS_PROCESS_WORK_LIST (winternl.h:306-333). Real init sizes the
// shared section to 0x4000 (syscall.c:952 "map_size = 0x4000") -- matched
// exactly (not shrunk) so a later atom's real BTCpu-facing push/pop needs
// no resize. Free list built with the SAME prepend-every-entry algorithm
// as create_cross_process_work_list (syscall.c:972-973) +
// RtlWow64PushCrossProcessWorkOntoFreeList (ntdll/process.c:411-425: new
// entry's `next` = old first, `first` becomes byte-offset of new entry
// FROM THE LIST BASE, counter++) -- built directly here since no code
// executes yet to call the real Rtl function. ----
#define CPWL_HDR_SIZE      0x30   // free_list(8)+work_list(8)+unknown[4](32)
#define CPWL_ENTRY_SIZE    40     // next(4)+id(4)+addr(8)+size(8)+args[4](16), winternl.h:308-315
#define CPWL_MAP_SIZE      0x4000
#define CPWL_FREELIST_OFF  0x00
#define CPWL_ENTRIES_OFF   CPWL_HDR_SIZE

// ---- WOW64_CPURESERVED (winternl.h:4375): USHORT Flags + USHORT Machine,
// followed by an inline i386 CONTEXT (winnt.h:1177 I386_CONTEXT, sizeof
// 0xcc+0x200==0x2cc) + CONTEXT_EX (winnt.h:1596: 3 CONTEXT_CHUNK(8B each)
// + ULONG64 align under _WIN64 == 0x20) when Machine==i386 (our only guest
// arch this leg). Pure per-thread CPU-sim scratch -- BTCpuSimulate reads/
// writes it once wired (C4-4/5); this atom allocates+zeroes it and wires
// TlsSlots[WOW64_TLS_CPURESERVED] to it, no simulate call happens yet. ----
#define WOW64_CPURESERVED_HDR_SIZE    4
#define I386_CONTEXT_SIZE             0x2cc
#define CONTEXT_EX_SIZE                0x20
#define WOW64_CPURESERVED_SIZE (WOW64_CPURESERVED_HDR_SIZE + I386_CONTEXT_SIZE + CONTEXT_EX_SIZE)
#define WOW64_CPURESERVED_MACHINE_OFF 0x02

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
// CHALICE C3b Atom 3B: bumped 0x60 -> 0x90 -- headroom for ntboot.c's
// nt_boot_process to also populate StandardInput/StandardError (already
// laid out below Atom A's StandardOutput receipt) + ImagePathName/
// CommandLine UNICODE_STRINGs (well-known public RTL_USER_PROCESS_
// PARAMETERS offsets: CurrentDirectory@0x38, DllPath@0x50,
// ImagePathName@0x60, CommandLine@0x70, Environment ptr@0x80) -- purely a
// larger zeroed MAlloc, no change to Atom A/3A's existing byte usage
// (StandardOutput@0x28 untouched, PeRun/hello3/hello4 unaffected).
#define PARAMS_SIZE               0x90

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
#define MOD_BASEDLLNAME_OFF        0x58   // UNICODE_STRING, 16B (FullDllName@0x48 left unset)
#define MOD_SIZE                   0xA0
#define US_LENGTH_OFF              0x00
#define US_MAXLENGTH_OFF           0x02
#define US_BUFFER_OFF              0x08

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

    // ---- CHALICE C-hello7 fix: TEB.StaticUnicodeString -- an empty,
    // properly-capacitied UNICODE_STRING pointing at the in-TEB scratch
    // buffer right after it (see offset comment above). Must be valid
    // BEFORE any guest code resolves an ANSI module/path name (kernelbase's
    // GetModuleHandleA/GetModuleFileNameA etc all route through this slot
    // when AllocateDestinationString=FALSE). ----
    wr16(teb + TEB_STATICUNICODESTRING_OFF + US_LENGTH_OFF, 0);
    wr16(teb + TEB_STATICUNICODESTRING_OFF + US_MAXLENGTH_OFF, TEB_STATICUNICODEBUFFER_LEN);
    wr64(teb + TEB_STATICUNICODESTRING_OFF + US_BUFFER_OFF, (u64b)(teb + TEB_STATICUNICODEBUFFER_OFF));

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

// =====================================================================
// CHALICE C4-3 (C4-SPEC.md atom C4-3): 32-bit TEB32/PEB32 floor.
// Extends an already-built 64-bit TEB (nt_build_teb, above -- TEB_SIZE now
// 0x3000, grown this atom specifically to make room here) with the WOW64
// view Wine's own code expects: a TEB32 in-place at teb64+0x2000, a PEB32
// + trailing WOW64INFO, a CROSS_PROCESS_WORK_LIST, and a WOW64_CPURESERVED
// per-thread CPU-sim scratch block -- wired into TEB64.TlsSlots exactly the
// way dlls/wow64/syscall.c's process_init/BTCpuSimulate call sites read
// them (offset citations in the struct-offset block above). No guest code
// runs here (C4-2's own doctrine, unchanged) -- this only builds+wires the
// structures and hands back their addresses for a serial proof dump.
// =====================================================================
long nt_build_wow64_teb32(void *teb64_v, unsigned short native_machine,
                           unsigned short emulated_machine,
                           struct NtWow64Info *out) {
    u8 *teb64 = (u8 *)teb64_v;
    if (!teb64 || !out) return -1;

    // ---- TEB32 view, in-place at teb64+0x2000 (already zeroed -- part of
    // teb64's own nt_zalloc(TEB_SIZE) block). ----
    u8 *teb32 = teb64 + TEB_WOW64_VIEW_OFF;
    wr32(teb32 + TEB32_TIB_EXCEPTIONLIST_OFF, 0xFFFFFFFFUL);        // virtual.c:4009 Tib.ExceptionList = ~0u
    wr32(teb32 + TEB32_TIB_FIBERDATA_OFF, 0x1e00);                  // virtual.c:4010
    wr32(teb32 + TEB32_TIB_SELF_OFF, (u32b)(u64b)teb32);
    wr16(teb32 + TEB32_STATICUNICODESTRING_OFF + 0 /* Length */, 0);
    wr16(teb32 + TEB32_STATICUNICODESTRING_OFF + 2 /* MaximumLength */, TEB32_STATICUNICODEBUFFER_LEN);
    wr32(teb32 + TEB32_STATICUNICODESTRING_OFF + US32_BUFFER_OFF,
         (u32b)(u64b)(teb32 + TEB32_STATICUNICODEBUFFER_OFF));
    wr32(teb32 + TEB32_GDIBATCHCOUNT_OFF, (u32b)(u64b)teb64);       // virtual.c:4016 GdiBatchCount = PtrToUlong(teb64)
    wr32(teb32 + TEB32_WOWTEBOFFSET_OFF, (u32b)(0UL - TEB_WOW64_VIEW_OFF));   // virtual.c:4017 WowTebOffset = -teb_offset

    // ---- cross-link on the 64-bit TEB (virtual.c:4018 "teb64->WowTebOffset = teb_offset") ----
    wr32(teb64 + TEB64_WOWTEBOFFSET_OFF, TEB_WOW64_VIEW_OFF);

    // ---- PEB32 + trailing WOW64INFO (syscall.c:1000 "(peb32 + 1)"). PEB32's
    // own trapdoor fields (ImageBaseAddress/LdrData/ProcessParameters/
    // ApiSetMap) are left 0 this atom -- no 32-bit ntdll/exe image mapped
    // yet (that's C4-4/5 territory); documented gap, not silent (dumped as
    // 0 by the pe9 proof line, not hidden). ----
    u8 *peb32 = (u8 *)nt_zalloc(PEB32_SIZE + WOW64INFO_SIZE);
    if (!peb32) return -1;
    u8 *wow64info = peb32 + PEB32_SIZE;

    wr32(peb32 + PEB32_NUMBEROFPROCESSORS_OFF, 1);
    wr32(peb32 + PEB32_OSMAJORVERSION_OFF, 10);
    wr32(peb32 + PEB32_OSMINORVERSION_OFF, 0);
    wr32(peb32 + PEB32_OSBUILDNUMBER_OFF, 19041);
    wr32(teb32 + TEB32_PEB_OFF, (u32b)(u64b)peb32);

    wr32(wow64info + WOW64INFO_NATIVESYSTEMPAGESIZE_OFF, 0x1000);           // syscall.c:1001
    // WOW64INFO.CpuFlags: left 0 -- grep of the vanilla donor (dlls/wow64,
    // dlls/ntdll) shows NO assignment to ->CpuFlags anywhere; it's read
    // (syscall.c:252) but never written in-tree, presumably a backend-
    // specific (BTCpuProcessInit) job -- measured, not guessed at.
    wr16(wow64info + WOW64INFO_NATIVEMACHINETYPE_OFF, native_machine);      // syscall.c:1002
    wr16(wow64info + WOW64INFO_EMULATEDMACHINETYPE_OFF, emulated_machine);  // syscall.c:1003

    // ---- CROSS_PROCESS_WORK_LIST: exact 0x4000 size real Wow64 uses
    // (syscall.c:952), free list fully populated (prepend-all-entries,
    // matching syscall.c:972-973 + process.c:411-425). ----
    u8 *cpwl = (u8 *)nt_zalloc(CPWL_MAP_SIZE);
    if (!cpwl) return -1;
    unsigned int n_entries = (CPWL_MAP_SIZE - CPWL_ENTRIES_OFF) / CPWL_ENTRY_SIZE;
    u32b first_off = 0;
    for (unsigned int i = 0; i < n_entries; i++) {
        u8 *entry = cpwl + CPWL_ENTRIES_OFF + (u64b)i * CPWL_ENTRY_SIZE;
        wr32(entry + 0 /* next */, first_off);
        first_off = (u32b)(u64b)(entry - cpwl);
    }
    wr32(cpwl + CPWL_FREELIST_OFF + 0 /* .first */, first_off);
    wr32(cpwl + CPWL_FREELIST_OFF + 4 /* .counter */, n_entries);
    wr64(wow64info + WOW64INFO_CROSSPROCESSWORKLIST_OFF, (u64b)cpwl);       // syscall.c:975

    // ---- TlsSlots wiring (real TEB64 offset 0x1480, winternl.h:1238) ----
    wr64(teb64 + TEB64_TLSSLOTS_OFF + WOW64_TLS_WOW64INFO * 8, (u64b)wow64info);   // syscall.c:1004

    // ---- WOW64_CPURESERVED per-thread CPU-sim scratch; TlsSlots[WOW64_TLS_
    // CPURESERVED] is how BTCpuSimulate's caller locates it (syscall.c:1393
    // "WOW64_CPURESERVED *cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED]"). ----
    u8 *cpureserved = (u8 *)nt_zalloc(WOW64_CPURESERVED_SIZE);
    if (!cpureserved) return -1;
    wr16(cpureserved + WOW64_CPURESERVED_MACHINE_OFF, emulated_machine);
    wr64(teb64 + TEB64_TLSSLOTS_OFF + WOW64_TLS_CPURESERVED * 8, (u64b)cpureserved);

    out->teb32 = teb32;
    out->peb32 = peb32;
    out->wow64info = wow64info;
    out->crossproclist = cpwl;
    out->crossproclist_size = CPWL_MAP_SIZE;
    out->crossproclist_entries = n_entries;
    out->cpureserved = cpureserved;
    return 0;
}

// CHALICE C3c-2l Atom A/B — nt_ldr_add_module: insert a SECOND (or Nth)
// LDR_DATA_TABLE_ENTRY into an already-built TEB's PEB.Ldr module list.
// nt_build_teb (above) only ever registers ONE self-entry (the image it
// was called with, always the EXE for nt_boot_process) -- ntdll itself
// (mapped directly by ntboot.c's own C code via nt_territory_alloc,
// bypassing the guest's own NtCreateSection/NtMapViewOfSection dependency-
// load path that kernel32/kernelbase go through) never gets a module-list
// entry at all. Phase-A receipt (C3c-2l): the guest's own find_named_export
// module lookup for "ntdll.dll" during KERNEL32's __wine_dbg_header import
// fixup walks PEB.Ldr looking for a module named "ntdll.dll" -- with zero
// such entries present, the lookup can't succeed regardless of base
// alignment; this is the missing-registration gap, not a base-math bug.
// Inserts at the TAIL of all three circular doubly-linked lists (InLoad/
// InMemory/InInit order) -- same list-splice pattern nt_build_teb already
// uses for its single self-node, generalized to N nodes.
void nt_ldr_add_module(void *teb_v, void *image_base, void *entry, unsigned long image_size,
      const unsigned short *name_utf16, unsigned long name_chars) {
    u8 *teb = (u8 *)teb_v;
    u8 *peb = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *ldr = (u8 *)(u64b)rd64(peb + PEB_LDR_OFF);
    u8 *mod = (u8 *)nt_zalloc(MOD_SIZE);
    if (!ldr || !mod) return;

    // splice `mod` in before the list head (== insert at tail, since the
    // head's Blink is the current tail) for each of the 3 lists.
    for (int li = 0; li < 3; li++) {
        u64b off = li == 0 ? LDR_INLOADORDER_OFF : li == 1 ? LDR_INMEMORYORDER_OFF : LDR_INEINITORDER_OFF;
        u64b modoff = li == 0 ? MOD_INLOADORDER_OFF : li == 1 ? MOD_INMEMORYORDER_OFF : MOD_ININITORDER_OFF;
        u8 *head = ldr + off;
        u64b head_flink = rd64(head + 0);
        u64b old_tail = rd64(head + 8);   // head->Blink
        // new_node->Flink = head; new_node->Blink = old_tail
        wr64(mod + modoff + 0, (u64b)head);
        wr64(mod + modoff + 8, old_tail);
        // old_tail->Flink = new_node (old_tail's LIST_ENTRY is at old_tail
        // itself -- it's a raw LIST_ENTRY* into some node's modoff field)
        wr64((u8 *)old_tail + 0, (u64b)(mod + modoff));
        // head->Blink = new_node
        wr64(head + 8, (u64b)(mod + modoff));
        (void)head_flink;
    }

    wr64(mod + MOD_DLLBASE_OFF, (u64b)image_base);
    wr64(mod + MOD_ENTRYPOINT_OFF, (u64b)entry);
    wr32(mod + MOD_SIZEOFIMAGE_OFF, (u32b)image_size);
    if (name_utf16 && name_chars) {
        wr16(mod + MOD_BASEDLLNAME_OFF + US_LENGTH_OFF, (u16)(name_chars * 2));
        wr16(mod + MOD_BASEDLLNAME_OFF + US_MAXLENGTH_OFF, (u16)(name_chars * 2));
        wr64(mod + MOD_BASEDLLNAME_OFF + US_BUFFER_OFF, (u64b)name_utf16);
    }
}

// CHALICE C3c-2l Phase-A instrument: dump every node currently on
// PEB.Ldr.InLoadOrderModuleList (count + DllBase + SizeOfImage + raw
// BaseDllName char count) -- the direct receipt for "does the guest's own
// module list contain an ntdll.dll entry" before any fix is applied.
void nt_ldr_dump(void *teb_v) {
    u8 *teb = (u8 *)teb_v;
    u8 *peb = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *ldr = (u8 *)(u64b)rd64(peb + PEB_LDR_OFF);
    if (!ldr) { puts("CHALICE: ldr-dump: no Ldr\n"); return; }
    u8 *head = ldr + LDR_INLOADORDER_OFF;
    u64b cur = rd64(head + 0);
    int n = 0;
    puts("CHALICE: ldr-dump InLoadOrderModuleList:\n");
    while (cur != (u64b)head && n < 16) {
        u8 *mod = (u8 *)cur - MOD_INLOADORDER_OFF;
        u64b base = rd64(mod + MOD_DLLBASE_OFF);
        u64b sz   = rd64(mod + MOD_SIZEOFIMAGE_OFF) & 0xFFFFFFFFULL;
        u16 nlen = (u16)rd64(mod + MOD_BASEDLLNAME_OFF + US_LENGTH_OFF);
        u64b nbuf = rd64(mod + MOD_BASEDLLNAME_OFF + US_BUFFER_OFF);
        puts("  ["); nt_log_putdec((u64b)n); puts("] base="); puthex(base);
        puts(" size="); puthex(sz); puts(" namelen="); nt_log_putdec(nlen);
        if (nbuf && nlen) {
            puts(" name=");
            u16 *w = (u16 *)nbuf;
            for (int i = 0; i < nlen / 2 && i < 32; i++) putc((char)w[i]);
        }
        puts("\n");
        cur = rd64(mod + MOD_INLOADORDER_OFF + 0);
        n++;
    }
    puts("CHALICE: ldr-dump count="); nt_log_putdec((u64b)n); puts("\n");
}
