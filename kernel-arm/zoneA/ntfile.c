// CHALICE C3a Atom 3A — loader-path services + DLL machinery.
// Manifest-driven blob store (embedded PE images, build.sh-generated) +
// flat kernel-wide handle table {file, section} (console pseudo-handle
// stays ntsvc.c's existing NT_STDOUT_HANDLE special case) + NtOpenFile/
// NtReadFile + NtCreateSection(SEC_IMAGE)/NtMapViewOfSection (graduates
// pemod.c's PeMapImage onto a fresh DLL-territory bump allocator, per
// SLICE3-SPEC Atom 3A: 0x45000000+, NOT the 64MB heap) + image registry +
// NtProtectVirtualMemory (recorded no-op, flat arena) + NtQueryVirtualMemory
// (MemoryBasicInformation only).
// LLP64 note: this file is aarch64-none-elf (LP64, kernel side) -- every
// `unsigned long` here is 8 bytes, matching nt_dispatch's fixed 8-byte-
// per-argument marshal regardless of the PE-side (LLP64) caller's declared
// width. PE-side callers (chalice/hello4.c) must use `unsigned long long`
// for any pointer-target integer per the established SCARS law.
#include "kernel.h"
#include "ntfile.h"
#include "ntsvc.h"
#include "pemod.h"   // CHALICE C4-57: pe_find_named_export (i386 IAT binder)

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;
typedef unsigned short u16b;

static void wr16(u8 *p, u16b v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }
static u32b rd32(const u8 *p) { u32b v = 0; for (int i = 0; i < 4; i++) v |= (u32b)p[i] << (8 * i); return v; }

// IO_STATUS_BLOCK: {NTSTATUS/PVOID Status; ULONG_PTR Information;} 16B, same
// shape ntsvc.c's NtWriteFile uses (receipted there).
typedef struct { u64b status; u64b information; } NtIoStatusBlock;

// Moved ahead of first use (nt_blob_lookup_fs, below) -- originally
// introduced further down alongside NtOpenFile's OBJECT_ATTRIBUTES/
// UNICODE_STRING offsets; same constant, single definition.
#define NT_PATH_MAX 128

static int nt_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

// =====================================================================
// Blob manifest lookup (table itself lives in build.sh-generated
// pehello_bin.c: nt_blob_manifest[]/nt_blob_manifest_count, declared in
// pemod.h). nt_blob_manifest_init() is idempotent-by-caller; called once
// here, lazily, on first file-service use.
// =====================================================================
static int nt_blob_inited;

// CHALICE C3b Atom 3B: no longer static -- ntboot.c calls this directly
// before its own blob-manifest lookups (nt_boot_process doesn't go through
// NtOpenFile/handle-table machinery, it wants the blob bytes straight).
void nt_blob_ensure_init(void) {
    if (nt_blob_inited) return;
    nt_blob_inited = 1;
    nt_blob_manifest_init();
}

static int nt_blob_lookup(const char *basename, const unsigned char **out_bytes, u64b *out_len,
                           const char **out_name) {
    for (u64b i = 0; i < nt_blob_manifest_count; i++) {
        if (!nt_streq_ci(basename, nt_blob_manifest[i].name)) continue;
        *out_bytes = nt_blob_manifest[i].bytes;
        *out_len   = nt_blob_manifest[i].len;
        *out_name  = nt_blob_manifest[i].name;
        return 1;
    }
    return 0;
}

// =====================================================================
// CHALICE C3c Atom 3C-1: blob-store miss -> FS fallback. Stock DLLs are too
// big to embed as xxd byte arrays in the kernel image (~20MB combined,
// SLICE3-SPEC Atom 3C) -- they instead live on the RedSea-spirit disk
// (kernel-arm/chalice/mkdisk.py bakes them in at build time, lowercase
// names). On a manifest miss, do a whole-file FsRead into a buffer sized
// generously above any real stock DLL (largest observed: kernelbase.dll
// ~9MB, wine 11.9 aarch64-windows) then store the SAME way a manifest hit
// would (bytes pointer + exact length FsRead reports, name = a lowercase
// copy so nt_handle_alloc's `name` field stays valid for the handle's
// lifetime, matching the manifest path's static-lifetime-string contract).
// CHALICE C3c-2g: this buffer (+ the small namebuf) used to come from
// mem.c's 64MB MAlloc heap -- one NT_FS_FALLBACK_MAX (16MB) allocation PER
// open, and the stock loader opens kernelbase.dll+kernel32.dll+more in one
// run, so 2-4 opens alone exhausted the *entire* 64MB heap on their own
// (independent of the VM-arena sink in ntsvc.c). Routed to the same
// DLL-territory bump allocator (nt_territory_alloc, below) the VM arena and
// image maps now share -- 0.75GB of headroom easily absorbs load-time's
// few dozen MB of raw file bytes, no reclaim needed (same bump-only
// simplification already accepted for the VM arena and PeMapImage's maps).
// Case-insensitive: fs.c's own on-disk names are lowercase (mkdisk.py
// convention); this fallback lowercases the query basename before FsRead
// so any-case PE-side path components (e.g. "C:\...\NTDLL.DLL") still hit.
// =====================================================================
#define NT_FS_FALLBACK_MAX (16UL * 1024 * 1024)   // headroom over largest stock DLL (~9MB)

static int nt_blob_lookup_fs(const char *basename, const unsigned char **out_bytes, u64b *out_len,
                              const char **out_name) {
    char lower[NT_PATH_MAX];
    int n = 0;
    for (; basename[n] && n < NT_PATH_MAX - 1; n++) {
        char c = basename[n];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        lower[n] = c;
    }
    lower[n] = 0;

    unsigned char *buf = nt_territory_alloc(NT_FS_FALLBACK_MAX);
    if (!buf) return 0;
    long got = FsRead(lower, buf, NT_FS_FALLBACK_MAX);
    // CHALICE C-hello7 (continuation #3): nt_territory_free is now a no-op
    // (receipt at its definition, ntfile.c below -- the C3c-2r free-list
    // reuse pool was reverted per monad ruling). These two calls (miss +
    // unused-tail trim) are kept as harmless dead-weight API usage, not
    // functioning reclaim -- the per-fetch waste they used to shrink (up to
    // NT_FS_FALLBACK_MAX=16MB per FS fetch regardless of real file size,
    // ~7-9 fetches/boot) no longer needs trimming: it's well under the
    // ~890MB territory for a SINGLE boot's own use, and cross-boot
    // accumulation (the actual exhaustion this atom chased) is now handled
    // by nt_territory_mark/_release_since bracketing nt_boot_process
    // (ntboot.c), which wipes this boot's ENTIRE territory footprint --
    // fetch waste included -- once the boot's PE has run to completion.
    if (got <= 0) { nt_territory_free(buf, NT_FS_FALLBACK_MAX); return 0; }
    {
        u64b used_pages = ((u64b)got + (4096ULL - 1)) & ~(4096ULL - 1);
        u64b total_pages = (NT_FS_FALLBACK_MAX + (4096ULL - 1)) & ~(4096ULL - 1);
        if (used_pages < total_pages)
            nt_territory_free(buf + used_pages, total_pages - used_pages);
    }

    char *namebuf = (char *)nt_territory_alloc((u64b)n + 1);
    if (!namebuf) return 0;
    for (int i = 0; i < n; i++) namebuf[i] = lower[i];
    namebuf[n] = 0;

    *out_bytes = buf;
    *out_len   = (u64b)got;
    *out_name  = namebuf;
    return 1;
}

// Combined lookup: manifest first (cheap, embedded test artifacts), then FS
// fallback (stock DLLs). Used by both NtOpenFile (below) and ntboot.c's
// nt_boot_process (pe6: stock ntdll.dll comes from here).
int nt_blob_lookup_or_fs(const char *basename, const unsigned char **out_bytes, u64b *out_len,
                          const char **out_name) {
    if (nt_blob_lookup(basename, out_bytes, out_len, out_name)) return 1;
    return nt_blob_lookup_fs(basename, out_bytes, out_len, out_name);
}

// =====================================================================
// Handle table: flat array, kernel-wide. Kinds {file, section} (console is
// the pre-existing NT_STDOUT_HANDLE pseudo-handle, ntsvc.c -- a singleton
// constant, not worth a table slot). Handle VALUES are small integers
// (0x100 + index*4, real-Windows-style 4-increment) so they never collide
// with NT_STDOUT_HANDLE (0x14) or the -1/-2 pseudo process/thread handles
// PE-side callers pass by convention.
// =====================================================================
#define NT_HANDLE_BASE 0x100UL
#define NT_HANDLE_MAX  32

typedef enum { NTH_FREE = 0, NTH_FILE = 1, NTH_SECTION = 2 } NtHandleKind;

typedef struct {
    NtHandleKind kind;
    const unsigned char *blob;
    u64b bloblen;
    const char *name;   // manifest entry name (static lifetime), for the
                         // image registry when a section gets mapped.
} NtHandleEntry;

static NtHandleEntry nt_handles[NT_HANDLE_MAX];

static void *nt_handle_alloc(NtHandleKind kind, const unsigned char *blob, u64b bloblen, const char *name) {
    for (int i = 0; i < NT_HANDLE_MAX; i++) {
        if (nt_handles[i].kind != NTH_FREE) continue;
        nt_handles[i].kind = kind;
        nt_handles[i].blob = blob;
        nt_handles[i].bloblen = bloblen;
        nt_handles[i].name = name;
        return (void *)(NT_HANDLE_BASE + (u64b)i * 4);
    }
    return 0;
}

static NtHandleEntry *nt_handle_get(void *h, NtHandleKind kind) {
    u64b v = (u64b)h;
    if (v < NT_HANDLE_BASE) return 0;
    u64b idx = (v - NT_HANDLE_BASE) / 4;
    if (idx >= NT_HANDLE_MAX) return 0;
    if (nt_handles[idx].kind != kind) return 0;
    return &nt_handles[idx];
}

int nt_file_handle_close(void *h) {
    u64b v = (u64b)h;
    if (v < NT_HANDLE_BASE) return 0;   // pseudo-handle territory -- not ours
    u64b idx = (v - NT_HANDLE_BASE) / 4;
    if (idx >= NT_HANDLE_MAX || nt_handles[idx].kind == NTH_FREE) return 0;
    nt_handles[idx].kind = NTH_FREE;
    return 1;
}

// =====================================================================
// Image registry: {name, base, size, entry, is_dll}. Populated by
// NtMapViewOfSection. Answers NtQueryVirtualMemory's MemoryBasicInformation.
// =====================================================================
#define NT_IMG_MAX 16

typedef struct {
    char name[64];
    u64b base, size, entry;
    int  is_dll;
    int  used;
} NtImageEntry;

static NtImageEntry nt_images[NT_IMG_MAX];

// CHALICE C3b Atom 3B: no longer static -- ntboot.c registers the Phase-0
// ntdll + exe maps here too, same registry NtQueryVirtualMemory answers from.
void nt_image_register(const char *name, u64b base, u64b size, u64b entry, int is_dll) {
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (nt_images[i].used) continue;
        int j = 0;
        if (name) for (; name[j] && j < 63; j++) nt_images[i].name[j] = name[j];
        nt_images[i].name[j] = 0;
        nt_images[i].base = base; nt_images[i].size = size; nt_images[i].entry = entry;
        nt_images[i].is_dll = is_dll; nt_images[i].used = 1;
        // C4-20 trace receipt: module bases must be this boot's values, not
        // addresses copied from a prior run.  The raw-RSP harness consumes it.
        puts("CHALICE IMAGE: name="); puts(nt_images[i].name);
        puts(" base="); puthex(base); puts(" size="); puthex(size); puts("\n");
        return;
    }
}

static NtImageEntry *nt_image_find_containing(u64b addr) {
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (!nt_images[i].used) continue;
        if (addr >= nt_images[i].base && addr < nt_images[i].base + nt_images[i].size) return &nt_images[i];
    }
    return 0;
}

static u64b nt_image_next_base(u64b addr) {
    u64b next = 0;
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (!nt_images[i].used) continue;
        if (nt_images[i].base > addr && (!next || nt_images[i].base < next)) next = nt_images[i].base;
    }
    return next;
}

// CHALICE C3c-2k p2 diagnostic: exact-name lookup (case-sensitive, matches
// nt_image_register's stored basename verbatim) -- used to re-verify a
// specific registered image's export directory is still intact LATER in
// the boot sequence (not just at map time), by calling pe_find_named_export
// against a fresh base/size snapshot from the registry.
static NtImageEntry *nt_image_find_by_name(const char *name) {
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (!nt_images[i].used) continue;
        int j = 0;
        for (; name[j] && nt_images[i].name[j] == name[j]; j++) {}
        if (name[j] == 0 && nt_images[i].name[j] == 0) return &nt_images[i];
    }
    return 0;
}

// CHALICE C3c Atom 3C-2a: exported so kmain.c's fault dump (boot.S
// sync_entry -> sync_handler) can resolve ELR/FAR against the same
// registry NtQueryVirtualMemory already answers from -- no parallel
// lookup. Returns 1 + name_out/off_out filled if addr falls inside a
// mapped image, 0 (untouched outputs) otherwise.
int nt_image_resolve(u64b addr, char *name_out, u64b namelen, u64b *off_out) {
    NtImageEntry *img = nt_image_find_containing(addr);
    if (!img) return 0;
    u64b i = 0;
    for (; img->name[i] && i + 1 < namelen; i++) name_out[i] = img->name[i];
    name_out[i] = 0;
    if (off_out) *off_out = addr - img->base;
    return 1;
}

// CHALICE C3c-2p: see ntfile.h -- thin public wrapper over the existing
// exact-name lookup, for the fault dump's targeted table spot-checks.
int nt_image_base_by_name(const char *name, u64b *base_out, u64b *size_out) {
    NtImageEntry *img = nt_image_find_by_name(name);
    if (!img) return 0;
    if (base_out) *base_out = img->base;
    if (size_out) *size_out = img->size;
    return 1;
}

// =====================================================================
// DLL-territory bump allocator (SLICE3-SPEC Atom 3A): 0x45000000 upward --
// right past mem.c's 64MB heap ceiling (HEAP_BASE 0x41000000 + 64MB =
// 0x45000000, verified in mem.c) -- toward the 0x7ffe0000 KUSER floor.
// Bump-only (no free): DLL images live for the kernel's lifetime here,
// same simplification HANDOFF already applies to the VM arena ("flat
// arena OK, skip VAD tree"). Whole 1GB RAM block is identity-mapped
// (kmain.c mmu_on, receipted there) so this is ordinary writable memory,
// no MMU work needed.
// =====================================================================
#define NT_DLL_TERRITORY_BASE  0x45000000ULL
#define NT_DLL_TERRITORY_LIMIT 0x7ff00000ULL   // safety margin below KUSER

// CHALICE C4-8 (monad ruling #2): HIGH territory pool -- unconstrained
// (ZeroBits==0) NtAllocateVirtualMemory requests (ntsvc.c NtAllocateVirtual-
// Memory) land here FIRST, falling back to the LOW pool above only if HIGH
// is exhausted. Selection grounded in donor wow64/ntdll zero_bits semantics,
// not guessed: wow64's own thunks ALWAYS pass a nonzero zero_bits (either an
// explicit 32-bit-visible caller value or wow64's own default_zero_bits =
// 0x7fffffff, wine/dlls/wow64/syscall.c:41) through get_zero_bits()
// (wine/dlls/wow64/wow64_private.h:94-97) into the real 64-bit
// NtAllocateVirtualMemory -- ntdll's own get_zero_bits_limit(zero_bits)
// (wine/dlls/ntdll/unix/unix_private.h:569-586) turns THAT into a concrete
// address ceiling, while zero_bits==0 (native ARM64 code with no 32-bit
// guest to stay visible to -- e.g. wowbox64.dll's OWN dynarec heap/code-
// cache growth) hits NtAllocateVirtualMemory's own `limit = 0` == no ceiling
// (wine/dlls/ntdll/unix/virtual.c:5290-5297: `if (!*ret) limit =
// get_zero_bits_limit(zero_bits); else limit = 0;` then
// allocate_virtual_memory(...,limit,...) -- zero_bits==0 => limit==0 =>
// unconstrained). NT_HIGH_TERRITORY_BASE sits at the 4GB mark -- above any
// PE32 image's <4GB requirement (pemod.c assert) and above wow64's own
// ~2GB default_zero_bits ceiling, so LOW-pool callers can never collide
// with it. Range must track kmain.c mmu_on's identity-mapped RAM extent
// and build.sh's GAIA_QEMU_MEM (default 4G: RAM spans 0x40000000..
// 0x140000000) -- bump both together if GAIA_QEMU_MEM grows.
#define NT_HIGH_TERRITORY_BASE  0x100000000ULL
#define NT_HIGH_TERRITORY_LIMIT 0x140000000ULL
#define NT_PAGE_SIZE 4096ULL
// CHALICE C3c-2l Phase B: Windows allocation granularity (SectionAlignment/
// image-map granularity every real PE loader assumes) -- 64KB, NOT the 4KB
// page size. NT_DLL_TERRITORY_BASE is already 0x10000-aligned so the FIRST
// call in a fresh boot lands correctly, but every call after the first only
// page-rounds its SIZE (not its START), so repeated small/odd-sized
// allocations (thread stacks, FS read buffers) drift the running bump
// pointer off the 64KB grid -- confirmed live (C3c-2l Phase A pe7 receipt):
// ntdll mapped at 0x46001000 (page-aligned, NOT 64KB-aligned) after pe1-pe6
// had already consumed territory earlier in the same boot.
#define NT_DLL_TERRITORY_ALIGN 0x10000ULL

static u64b nt_dll_territory_next = NT_DLL_TERRITORY_BASE;
// CHALICE C4-8: HIGH pool bump pointer -- declared here (not beside its
// alloc/mark/release functions further down) so nt_territory_debug_stats
// right below can report both pools in one line; same shape as the LOW
// pointer above (bump-only, per-boot wholesale reset via
// nt_high_territory_mark/_release_since, mirroring nt_territory_mark/
// _release_since exactly).
static u64b nt_dll_high_territory_next = NT_HIGH_TERRITORY_BASE;

void nt_territory_debug_stats(const char *tag) {
    puts("CHALICE DEBUG: territory["); puts(tag); puts("] next=");
    puthex(nt_dll_territory_next);
    puts(" used_bytes="); puthex(nt_dll_territory_next - NT_DLL_TERRITORY_BASE);
    puts(" high_next="); puthex(nt_dll_high_territory_next);
    puts(" high_used_bytes="); puthex(nt_dll_high_territory_next - NT_HIGH_TERRITORY_BASE);
    puts("\n");
}

// CHALICE C-hello7 (continuation #3, monad ruling): the C3c-2r general
// free-list reuse pool is REVERTED here -- proven fragile live: reusing a
// smaller-than-freed slot without splitting left a stale tail from the
// PRIOR occupant sitting beside a live NEW allocation (a size-matched
// zero-only-`pages` attempt still didn't remove the bug CLASS, just one
// instance of it -- root cause of a live pe7 regression, kernelbase.dll+
// 0x3aaa0 NULL-ish deref reading a stale ntdll .data byte through a reused
// slot's dropped tail). Ruling: the floor runs ONE guest process at a
// time -- general reuse is over-engineering for that model. The real fix
// is the wholesale per-process reset below (nt_territory_mark/
// _release_since). nt_territory_free is kept as an API surface for its
// existing callers (ntsvc.c's NtFreeVirtualMemory, this file's
// nt_blob_lookup_fs shrink-to-fit trim) but is now pure accounting/no-op --
// no pool, no reuse, so no possible stale-tail exposure.
void nt_territory_free(unsigned char *addr, unsigned long size) {
    (void)addr; (void)size;   // no-op by design -- see receipt above.
}

// CHALICE C-hello7: process-epoch reclaim (the actual fix, monad ruling).
// Root cause of the live 3-boot-chain exhaustion (pe6 then pe7 then pe8 in
// one QEMU session, each a fresh "process" per HANDOFF's own framing):
// stock kernelbase's heap-init legitimately creates/keeps several 0xFD0000
// (13MB, Wine's default heap reserve) growable heap reservations per
// process -- real, not-a-bug per-process memory use -- but this kernel has
// no process teardown, so every nt_boot_process invocation's OWN heaps/
// DLL-image maps/FS-blob buffers pile up FOREVER on top of the previous
// verb's (observed live: ~450MB kept per stock boot, exhausting the
// ~890MB territory on the 3rd consecutive stock boot). Each shell verb
// (pe6/pe7/pe8) is an independent, non-overlapping process lifetime by
// SLICE3-SPEC design (one PE runs to completion, shell regains control,
// only THEN does the next verb run) -- nothing after a verb returns needs
// ITS OWN private allocations to remain live, so a wholesale bump-pointer
// reset back to the pre-boot mark at nt_boot_process's exit (ntboot.c) is
// exactly the semantic real Windows process teardown gives for free, and
// (unlike the reverted free-list) carries no reuse hazard: every byte
// between mark and this boot's own high-water mark is freshly zeroed by
// the very alloc that claims it (plain bump path below), so no in-boot gap
// can expose a prior boot's stale bytes beside a live allocation.
// KUSER_SHARED_DATA (0x7ffe0000) is fixed-address and OUTSIDE this
// territory range (NT_DLL_TERRITORY_LIMIT 0x7ff00000, checked against the
// ruling's "verify what must persist" instruction) so the reset never
// touches it; nothing else in territory is designed to survive past its
// owning boot (pe1-pe6/shell verbs each map their own DLLs fresh, none
// reads back into a prior verb's territory once that verb has returned).
u64b nt_territory_mark(void) { return nt_dll_territory_next; }

void nt_territory_release_since(u64b mark) {
    if (nt_dll_territory_next > mark) nt_dll_territory_next = mark;
}

// CHALICE C3b Atom 3B: no longer static -- ntboot.c's nt_boot_process
// allocates the Phase-0 thread stack (64K) from the same DLL territory.
unsigned char *nt_territory_alloc(unsigned long size_of_image) {
    u64b pages = (size_of_image + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    if (pages == 0) pages = NT_PAGE_SIZE;

    // CHALICE C-hello7 (continuation #3): no free-list consultation here --
    // reverted per monad ruling (receipt above nt_territory_free). Pure bump
    // allocation only; the wholesale per-boot reset (nt_territory_mark/
    // _release_since) is what keeps this from exhausting across pe-runs.

    // CHALICE C3c-2l Phase B: round the START up to 64KB granularity before
    // handing out this allocation -- every DLL image map (and, for
    // simplicity/uniformity, every other territory consumer: thread stacks,
    // FS buffers) gets a Windows-allocation-granularity-aligned base, same
    // as a real Windows VirtualAlloc/MapViewOfFile guarantee. Territory is
    // ~890MB (0x45000000..0x7ff00000) so the extra rounding waste is cheap.
    nt_dll_territory_next = (nt_dll_territory_next + (NT_DLL_TERRITORY_ALIGN - 1)) & ~(NT_DLL_TERRITORY_ALIGN - 1);
    if (nt_dll_territory_next + pages > NT_DLL_TERRITORY_LIMIT) {
        puts("CHALICE DEBUG: territory EXHAUSTED next="); puthex(nt_dll_territory_next);
        puts(" want_pages="); puthex(pages); puts(" limit="); puthex(NT_DLL_TERRITORY_LIMIT);
        puts("\n");
        return 0;
    }
    u8 *base = (u8 *)nt_dll_territory_next;
    nt_dll_territory_next += pages;
    for (u64b i = 0; i < pages; i++) base[i] = 0;
    return base;
}

u64b nt_high_territory_mark(void) { return nt_dll_high_territory_next; }

void nt_high_territory_release_since(u64b mark) {
    if (nt_dll_high_territory_next > mark) nt_dll_high_territory_next = mark;
}

// CHALICE C4-8: HIGH pool alloc -- identical bump/align/zero-fill shape to
// nt_territory_alloc (LOW pool) above, just against the HIGH base/limit.
// Returns 0 on HIGH exhaustion (caller, ntsvc.c nt_vm_alloc_pool, falls
// back to the LOW pool -- monad ruling #2's "HIGH first, LOW fallback").
unsigned char *nt_high_territory_alloc(unsigned long size_of_image) {
    u64b pages = (size_of_image + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    if (pages == 0) pages = NT_PAGE_SIZE;

    nt_dll_high_territory_next = (nt_dll_high_territory_next + (NT_DLL_TERRITORY_ALIGN - 1)) & ~(NT_DLL_TERRITORY_ALIGN - 1);
    if (nt_dll_high_territory_next + pages > NT_HIGH_TERRITORY_LIMIT) {
        puts("CHALICE DEBUG: HIGH territory EXHAUSTED next="); puthex(nt_dll_high_territory_next);
        puts(" want_pages="); puthex(pages); puts(" limit="); puthex(NT_HIGH_TERRITORY_LIMIT);
        puts("\n");
        return 0;
    }
    u8 *base = (u8 *)nt_dll_high_territory_next;
    nt_dll_high_territory_next += pages;
    for (u64b i = 0; i < pages; i++) base[i] = 0;
    return base;
}

// =====================================================================
// CHALICE C4-9: RESERVE/COMMIT split (monad ruling, HANDOFF leg-8 waste
// analysis -> leg-9 fix). Root cause fixed here: the LOW/HIGH territory
// pools above always bumped+zeroed the FULL requested size regardless of
// AllocationType, so a MEM_RESERVE-only call (no physical touch intended,
// ever) cost exactly as much real territory as a MEM_COMMIT -- ~20x waste
// measured live last leg (114 x 13MB Wine default-heap RESERVEs backing
// only ~7.5MB of actual MEM_COMMIT touches).
//
// Design (every value parameterized w/ default, per IRON law):
//   - RESERVATION ARENA: pure VA carve, zero physical cost. Lives ABOVE all
//     physical RAM (NT_RAM_TOP tops out at 5GB under the 4G default +
//     NT_HIGH_TERRITORY_LIMIT 0x140000000) at the 8GB mark, well inside the
//     T0SZ=25 512GB VA ceiling (kmain.c mmu_on, C4-8) -- room to grow the
//     default without ever colliding with physical-backed pools.
//   - COMMIT: maps 2MB L2 block windows (4KB-granule level-2 leaf size) on
//     demand, backed by bump-allocated HIGH-pool physical pages (reuses
//     nt_high_territory_alloc above -- "physical backing bump-allocated
//     from HIGH pool", ruling #2) -- so HIGH pool now serves two roles:
//     legacy direct HIGH-pool callers (unchanged) AND arena commit backing.
//   - Only the arena's OWN 1GB L1 slots get new L2 table descriptors
//     (kmain.c nt_l1_install_table) -- RAM/device 1GB block descriptors
//     mmu_on already installed are never touched.
//
// Semantics grounded in donor (wine/dlls/ntdll/unix/virtual.c, receipted
// per-callsite below):
//   - MEM_RESERVE alone (virtual.c:5176 `if ((type & MEM_RESERVE) || !base)`
//     branch, no MEM_COMMIT bit): carve VA, vprot has no VPROT_COMMITTED --
//     zero physical cost here too (nt_arena_reserve, no window touched).
//   - MEM_RESERVE|MEM_COMMIT together: same branch, `if (type & MEM_COMMIT)
//     vprot |= VPROT_COMMITTED` -- reserve AND commit the whole range.
//   - MEM_COMMIT alone at a *BaseAddress inside an existing reservation
//     (virtual.c:5199 commit-only branch): `find_view` must succeed (else
//     STATUS_NOT_MAPPED_VIEW) -- commits just the requested sub-range.
//   - MEM_COMMIT on an address with NO covering reservation: donor's
//     find_view fails -> STATUS_NOT_MAPPED_VIEW (virtual.c:5199).
//   - Re-COMMIT of an already-committed window: idempotent SUCCESS (donor
//     set_protection path, virtual.c:5203-5204, does not re-fault/re-map an
//     already-committed range) -- nt_arena_commit_window checks the
//     committed bitmap first and returns success without re-touching HIGH
//     pool or the L2 entry.
//   - NtFreeVirtualMemory MEM_RELEASE/MEM_DECOMMIT: accounting-only is fine
//     per this floor's existing doctrine (LOW/HIGH pools already do this) --
//     the real reclaim is the per-boot wholesale reset (nt_arena_release_
//     since below), extended to cover the arena bump pointer, reservation
//     table, and committed-window bitmap together, same one call site
//     ntboot.c already uses for the other two pools.
// =====================================================================
#define NT_ARENA_BASE   0x200000000ULL           // 8GB mark (param)
#define NT_ARENA_SIZE   0x200000000ULL           // 8GB default (param)
#define NT_ARENA_LIMIT  (NT_ARENA_BASE + NT_ARENA_SIZE)
#define NT_ARENA_WINDOW 0x200000ULL              // 2MB commit granularity (L2 block size)
#define NT_ARENA_ALIGN  0x10000ULL               // Windows allocation granularity (same as LOW/HIGH pools)
#define NT_ARENA_L1_COUNT (NT_ARENA_SIZE / 0x40000000ULL)      // one L2 table per 1GB arena slot
#define NT_ARENA_WINDOWS  (NT_ARENA_SIZE / NT_ARENA_WINDOW)     // total 2MB windows across the arena

// Lazily-allocated L2 table per 1GB arena slot -- NULL until this boot's
// first commit inside that slot (kernel-lifetime pointer cache; the L2
// table page itself lives in the LOW territory pool, ordinary identity-
// mapped RAM, so no separate physical-tracking struct is needed for it).
static u64b *nt_arena_l2[NT_ARENA_L1_COUNT];

// Committed-window accounting: one bit per 2MB window across the WHOLE
// arena (absolute VA-indexed, not per-reservation) -- 4096 windows @ 8GB
// default = 512 bytes total, cheap, and stable across reservation-table
// churn (a window's committed state doesn't move if reservation slots get
// reused, since VA index is all that's ever looked up).
static unsigned long nt_arena_committed_bitmap[(NT_ARENA_WINDOWS + 63) / 64];

static int nt_arena_window_committed(u64b win_idx) {
    return (int)((nt_arena_committed_bitmap[win_idx / 64] >> (win_idx % 64)) & 1UL);
}
static void nt_arena_window_mark_committed(u64b win_idx) {
    nt_arena_committed_bitmap[win_idx / 64] |= (1UL << (win_idx % 64));
}

// Reservation table -- tracks each live [base,size) VA carve so COMMIT can
// validate "address falls inside a reservation" (donor's find_view, ruling
// #4) and NtQueryVirtualMemory can report State honestly (ruling #5).
#define NT_ARENA_MAX_RESERVATIONS 128
typedef struct { u64b base; u64b size; int used; } NtArenaRes;
static NtArenaRes nt_arena_res[NT_ARENA_MAX_RESERVATIONS];
static u64b nt_arena_next = NT_ARENA_BASE;

static NtArenaRes *nt_arena_find_containing(u64b addr) {
    for (int i = 0; i < NT_ARENA_MAX_RESERVATIONS; i++) {
        if (!nt_arena_res[i].used) continue;
        if (addr >= nt_arena_res[i].base && addr < nt_arena_res[i].base + nt_arena_res[i].size)
            return &nt_arena_res[i];
    }
    return 0;
}

// Is `addr` inside the arena's VA range at all (reserved or not)? Lets
// NtQueryVirtualMemory/NtAllocateVirtualMemory tell "unreserved arena VA"
// (MEM_FREE, donor-consistent) apart from "outside the arena entirely"
// (falls through to this floor's existing LOW/HIGH/image-registry answers).
int nt_arena_contains(unsigned long addr) {
    return addr >= NT_ARENA_BASE && addr < NT_ARENA_LIMIT;
}

// Fault-path classifier → allocator ownership + reservation/commit state.
void nt_fault_region(const char *label, unsigned long addr) {
    char name[64]; u64b off, base = 0, size = 0; int committed = -2;
    puts("CHALICE REGION "); puts(label); puts("="); puthex(addr); puts(" -> ");
    if (nt_image_resolve(addr, name, sizeof(name), &off)) {
        puts("image "); puts(name); puts("+"); puthex(off);
    } else if (nt_arena_query(addr, &base, &size, &committed)) {
        puts("reserve-commit-arena ");
        if (committed < 0) puts("unreserved");
        else if (committed) puts("committed");
        else puts("reserved");
        puts(" base="); puthex(base); puts(" size="); puthex(size);
    } else if (nt_vm_region_query(addr, &base, &size)) {
        puts("vm-region base="); puthex(base); puts(" size="); puthex(size);
    } else if (addr >= NT_DLL_TERRITORY_BASE && addr < NT_DLL_TERRITORY_LIMIT) {
        puts(addr < nt_dll_territory_next ? "territory-live" : "territory-unallocated");
    } else if (addr >= NT_HIGH_TERRITORY_BASE && addr < NT_HIGH_TERRITORY_LIMIT) {
        puts(addr < nt_dll_high_territory_next ? "high-pool-live" : "high-pool-unallocated");
    } else puts("outside-known-allocators");
    puts("\n");
}

// Fault instruction fetch → only known mapped/live allocator classes.
int nt_fault_read32(unsigned long addr, unsigned int *word_out) {
    char name[64]; u64b off, base = 0, size = 0; int committed = -2;
    int mapped = nt_image_resolve(addr, name, sizeof(name), &off);
    if (!mapped && nt_arena_query(addr, &base, &size, &committed)) mapped = committed > 0;
    if (!mapped && nt_vm_region_query(addr, &base, &size)) mapped = 1;
    if (!mapped && addr >= NT_DLL_TERRITORY_BASE && addr < nt_dll_territory_next) mapped = 1;
    if (!mapped && addr >= NT_HIGH_TERRITORY_BASE && addr < nt_dll_high_territory_next) mapped = 1;
    if (!mapped) return 0;
    *word_out = *(volatile unsigned int *)addr;
    return 1;
}

// CHALICE C4-59: shared cycle counter, see ntfile.h for the full receipt.
unsigned long nt_c459_cycle;

// CHALICE C4-59: shared live-State.rip reader, see ntfile.h for the full
// donor-derived offset receipt (TEB -> TlsSlots[16] -> CurrentFrame -> State
// -> rip, offsets independently re-derived AND compile-checked against the
// real FEX/FEXCore/include/FEXCore/Core/CoreState.h +
// FEXCore/include/FEXCore/Debug/InternalThreadState.h headers this atom).
// Reuses nt_fault_read32's arena/territory/vm-region-aware safe-probe --
// this pointer chain lives in the reserve-commit ARENA (>=4GB), never the
// plain NT_RAM_BASE..NT_RAM_TOP identity window (same reasoning C4-31's own
// SynchronousFaultData reader already established for this exact chain).
int nt_wow64_live_state_rip(unsigned long teb, unsigned long *rip_out) {
    // teb (x18) is the kernel's own territory-allocated TEB once a guest
    // thread is running (ntsvc.c's TlsSlots wiring writes it unchecked the
    // same way, ~line 2375) -- non-null is the only precondition needed;
    // the pointer chain PAST this raw TlsSlots read goes through
    // nt_fault_read32's arena/territory-aware safe probe below.
    if (!teb) return 0;
    unsigned long thread_state = *(volatile unsigned long *)(teb + 0x1480 + 16 * 8);
    if (!thread_state) return 0;
    unsigned int cf_lo = 0, cf_hi = 0;
    if (!nt_fault_read32(thread_state + 0, &cf_lo) ||
        !nt_fault_read32(thread_state + 4, &cf_hi)) return 0;
    unsigned long current_frame = ((unsigned long)cf_hi << 32) | cf_lo;
    if (!current_frame) return 0;
    unsigned int r_lo = 0, r_hi = 0;
    if (!nt_fault_read32(current_frame + 0x18, &r_lo) ||
        !nt_fault_read32(current_frame + 0x1c, &r_hi)) return 0;
    *rip_out = ((unsigned long)r_hi << 32) | r_lo;
    return 1;
}

// CHALICE C4-64: shared live State.gregs[REG_RSP]/[guest-stack] probe, see
// ntfile.h for the full gregs[]-offset derivation receipt (CoreState.h/
// X86Enums.h direct field reads, CurrentFrame+0x40). Same TEB->TlsSlots[16]
// ->CurrentFrame chain as nt_wow64_live_state_rip above (deliberately NOT
// factored to share code with it -- this floor's established convention,
// same reasoning nt_wow64_live_state_rip's own header comment gives, is one
// self-contained guarded-probe function per reader, no shared internal
// helper to keep each one auditable in isolation). Returns CurrentFrame (0
// if not yet known -- caller's own "CurrentFrame known" gate). Each of
// rip/rsp/stackword is validated independently; stackword additionally
// requires rsp itself to be nonzero and mapped (GUARDED -- nt_fault_read32's
// arena/territory/vm-region-aware check -- never derefs an unmapped guest
// address, never faults the floor).
unsigned long nt_wow64_live_state_probe(unsigned long teb,
                                         unsigned long *rip_out, int *rip_valid,
                                         unsigned long *rsp_out, int *rsp_valid,
                                         unsigned int *stackword_out, int *stackword_valid) {
    *rip_valid = 0; *rsp_valid = 0; *stackword_valid = 0;
    if (!teb) return 0;
    unsigned long thread_state = *(volatile unsigned long *)(teb + 0x1480 + 16 * 8);
    if (!thread_state) return 0;
    unsigned int cf_lo = 0, cf_hi = 0;
    if (!nt_fault_read32(thread_state + 0, &cf_lo) ||
        !nt_fault_read32(thread_state + 4, &cf_hi)) return 0;
    unsigned long current_frame = ((unsigned long)cf_hi << 32) | cf_lo;
    if (!current_frame) return 0;

    unsigned int r_lo = 0, r_hi = 0;
    if (nt_fault_read32(current_frame + 0x18, &r_lo) &&
        nt_fault_read32(current_frame + 0x1c, &r_hi)) {
        *rip_out = ((unsigned long)r_hi << 32) | r_lo;
        *rip_valid = 1;
    }

    unsigned int sp_lo = 0, sp_hi = 0;
    if (nt_fault_read32(current_frame + 0x40, &sp_lo) &&
        nt_fault_read32(current_frame + 0x44, &sp_hi)) {
        unsigned long rsp = ((unsigned long)sp_hi << 32) | sp_lo;
        *rsp_out = rsp;
        *rsp_valid = 1;
        unsigned int word = 0;
        if (rsp && nt_fault_read32(rsp, &word)) {
            *stackword_out = word;
            *stackword_valid = 1;
        }
    }
    return current_frame;
}

// RESERVE: carve `size` bytes of VA from the arena -- bump-only, zero
// physical cost (no window touched, no L2 entry written). NULL on arena
// VA exhaustion (bump-only per-boot; wholesale reset below is the reclaim,
// same doctrine as the LOW/HIGH pools).
unsigned char *nt_arena_reserve(unsigned long size) {
    u64b pages = (size + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    if (pages == 0) pages = NT_PAGE_SIZE;

    nt_arena_next = (nt_arena_next + (NT_ARENA_ALIGN - 1)) & ~(NT_ARENA_ALIGN - 1);
    if (nt_arena_next + pages > NT_ARENA_LIMIT) {
        puts("CHALICE DEBUG: ARENA reserve EXHAUSTED next="); puthex(nt_arena_next);
        puts(" want_pages="); puthex(pages); puts(" limit="); puthex(NT_ARENA_LIMIT);
        puts("\n");
        return 0;
    }
    u64b base = nt_arena_next;
    nt_arena_next += pages;

    for (int i = 0; i < NT_ARENA_MAX_RESERVATIONS; i++) {
        if (nt_arena_res[i].used) continue;
        nt_arena_res[i].base = base;
        nt_arena_res[i].size = pages;
        nt_arena_res[i].used = 1;
        return (unsigned char *)base;
    }
    // No tracking slot left -- the VA bump already advanced; nothing to
    // undo (same accepted-waste convention nt_vm_alloc_pool's own comment
    // documents for its tracking table). A later COMMIT at this base would
    // fail nt_arena_find_containing -> STATUS_NOT_MAPPED_VIEW, legible.
    return 0;
}

// Commit exactly one 2MB window (win_base_va must already be window-
// aligned). Idempotent: an already-committed window is a no-op SUCCESS
// (donor: re-committing doesn't re-map, virtual.c:5203-5204). Returns 0
// only on HIGH-pool-backing or L2-table-page exhaustion.
static int nt_arena_commit_window(u64b win_base_va) {
    u64b win_idx = (win_base_va - NT_ARENA_BASE) / NT_ARENA_WINDOW;
    if (nt_arena_window_committed(win_idx)) return 1;

    unsigned char *phys = nt_high_territory_alloc((unsigned long)NT_ARENA_WINDOW);
    if (!phys) return 0;   // HIGH pool exhausted -- caller surfaces STATUS_NO_MEMORY

    u64b slot   = (win_base_va - NT_ARENA_BASE) / 0x40000000ULL;   // which 1GB arena L1 entry
    u64b l1_idx = (win_base_va >> 30) & 0x1FFULL;                   // absolute l1_table index
    if (!nt_arena_l2[slot]) {
        unsigned char *l2page = nt_territory_alloc(4096);   // LOW pool: identity-mapped, zeroed
        if (!l2page) return 0;
        nt_arena_l2[slot] = (u64b *)l2page;
        nt_l1_install_table(l1_idx, (u64b)l2page);          // kmain.c -- table descriptor, this slot only
    }
    u64b l2_idx = (win_base_va >> 21) & 0x1FFULL;
    // L2 block descriptor (4KB-granule level-2 leaf = 2MB block), same
    // normal-memory WBWA/inner-shareable/AF attribute bits mmu_on's own L1
    // RAM block descriptors use (kmain.c: `(1ULL<<2)|(3ULL<<8)|(1ULL<<10)`),
    // bits[1:0]=0b01 (valid+block, vs 0b11 table) since this IS the leaf.
    nt_arena_l2[slot][l2_idx] = (u64b)phys | (1ULL << 2) | (3ULL << 8) | (1ULL << 10) | 1ULL;
    nt_arena_window_mark_committed(win_idx);
    nt_tlbi_vmalle1();   // kmain.c -- conservative vmalle1 per ruling #2
    return 1;
}

// COMMIT: base/size must fall entirely within ONE live reservation (donor's
// find_view, ruling #4) -- returns 0 if not (caller maps to STATUS_NOT_
// MAPPED_VIEW), 1 on success (every covering window now committed).
int nt_arena_commit(unsigned long base, unsigned long size) {
    NtArenaRes *r = nt_arena_find_containing(base);
    if (!r) return 0;
    u64b end = base + size;
    if (end > r->base + r->size) return 0;   // extends past the reservation -- reject

    u64b win_start = base & ~(NT_ARENA_WINDOW - 1);
    u64b win_end   = (end + NT_ARENA_WINDOW - 1) & ~(NT_ARENA_WINDOW - 1);
    for (u64b w = win_start; w < win_end; w += NT_ARENA_WINDOW) {
        if (!nt_arena_commit_window(w)) return 0;   // partial commit left in place -- no undo, matches this floor's existing convention elsewhere
    }
    return 1;
}

// Query support for NtQueryVirtualMemory (ruling #5: report State honestly
// per accounting). Returns 0 if `addr` isn't inside the arena's VA range at
// all (caller falls through to the pre-existing free-region answer).
// On a hit: *base_out/*size_out are the covering reservation's [base,size)
// (or, if no reservation covers it, a single page at `addr` -- MEM_FREE
// inside the arena, same page-sized-unknown-region shape the existing
// fallback branch already uses outside it); *committed_out is 1 if the
// containing 2MB window is backed, 0 if reserved-but-uncommitted, or -1 as
// a sentinel meaning "no reservation at all here" (MEM_FREE).
int nt_arena_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out, int *committed_out) {
    if (!nt_arena_contains(addr)) return 0;
    NtArenaRes *r = nt_arena_find_containing(addr);
    if (!r) {
        // CHALICE C4-15: one coalesced MEM_FREE gap, not one 4KB page.
        // FEX InvalidationTracker's constructor walks `Address = BaseAddress
        // + RegionSize` until VirtualQuery fails (pinned donor Source/Windows/
        // Common/InvalidationTracker.cpp:16-28). Page-sized free answers turn
        // that finite region walk into millions of syscalls. Stop at the next
        // reservation (or arena limit), matching VirtualQuery's region shape.
        u64b base = addr & ~(NT_PAGE_SIZE - 1);
        u64b next = NT_ARENA_LIMIT;
        for (int i = 0; i < NT_ARENA_MAX_RESERVATIONS; i++) {
            if (nt_arena_res[i].used && nt_arena_res[i].base > base && nt_arena_res[i].base < next)
                next = nt_arena_res[i].base;
        }
        if (base_out) *base_out = base;
        if (size_out) *size_out = next - base;
        if (committed_out) *committed_out = -1;
        return 1;
    }
    u64b win_idx = (addr - NT_ARENA_BASE) / NT_ARENA_WINDOW;
    if (base_out) *base_out = r->base;
    if (size_out) *size_out = r->size;
    if (committed_out) *committed_out = nt_arena_window_committed(win_idx);
    return 1;
}

// MEM_RELEASE: `addr` must be an exact reservation base (real Windows rule
// too -- VirtualFree(MEM_RELEASE) only accepts the original base). Fills
// *size_out with the released reservation's size. Accounting-only (ruling:
// "NtFreeVirtualMemory MEM_RELEASE/MEM_DECOMMIT = accounting-only OK") --
// the committed-window bits for this range are left set until the next
// wholesale per-boot reset; a real Windows re-VirtualAlloc at a DIFFERENT
// base (this floor never reuses arena VA within one boot -- bump-only,
// same doctrine as LOW/HIGH) never observes the stale bits, so this is
// safe without extra bitmap-clearing work here.
int nt_arena_release(unsigned long addr, unsigned long *size_out) {
    for (int i = 0; i < NT_ARENA_MAX_RESERVATIONS; i++) {
        if (!nt_arena_res[i].used || nt_arena_res[i].base != addr) continue;
        if (size_out) *size_out = nt_arena_res[i].size;
        nt_arena_res[i].used = 0;
        return 1;
    }
    return 0;
}

// Per-boot wholesale reset (ntboot.c calls this alongside nt_territory_
// release_since/nt_high_territory_release_since/nt_vm_regions_release_
// since -- the arena's mark is captured/restored the same way). Clears the
// reservation table, the committed-window bitmap from the mark's window
// index onward, and the L2-table pointer cache (a NEW boot's first commit
// into a given 1GB slot re-allocates a fresh L2 page from the freshly-
// reset LOW pool and re-installs the L1 table entry -- any STALE L1 entry
// left pointing at a since-reclaimed L2 page is simply never touched again
// unless a NEW commit lands in that same slot, in which case it's
// immediately overwritten by nt_l1_install_table before any translation
// through it could occur -- same "wholesale reset, no reuse hazard" shape
// the LOW/HIGH pools already rely on).
unsigned long nt_arena_mark(void) { return nt_arena_next; }

void nt_arena_release_since(unsigned long mark) {
    if (nt_arena_next <= mark) return;
    u64b win_mark = (mark - NT_ARENA_BASE) / NT_ARENA_WINDOW;
    for (u64b w = win_mark; w < NT_ARENA_WINDOWS; w++)
        nt_arena_committed_bitmap[w / 64] &= ~(1UL << (w % 64));
    for (int i = 0; i < NT_ARENA_MAX_RESERVATIONS; i++)
        if (nt_arena_res[i].used && nt_arena_res[i].base >= mark) nt_arena_res[i].used = 0;
    for (u64b i = 0; i < NT_ARENA_L1_COUNT; i++) nt_arena_l2[i] = 0;
    nt_arena_next = mark;
}

// =====================================================================
// NtOpenFile / NtReadFile: name (UNICODE_STRING via OBJECT_ATTRIBUTES) ->
// blob store handle; offset+length copy for reads.
// OBJECT_ATTRIBUTES (winternl.h, LP64): Length(4,pad4)@0,
// RootDirectory@0x08, ObjectName@0x10 (PUNICODE_STRING), Attributes(4,
// pad4)@0x18, SecurityDescriptor@0x20, SecurityQualityOfService@0x28 (size
// 0x30). UNICODE_STRING: Length(u16)@0, MaximumLength(u16)@2, Buffer(ptr,
// 8-aligned)@8 (size 0x10). Both offsets receipted by natural C struct
// layout under AAPCS64/LLP64 alignment rules -- same status as ntsvc.c's
// TEB/PEB offsets.
// =====================================================================
#define OA_ROOTDIRECTORY_OFF 0x08
#define OA_OBJECTNAME_OFF 0x10
#define US_LENGTH_OFF     0x00
#define US_BUFFER_OFF     0x08

// =====================================================================
// CHALICE C4-4: two-root FS. Windows keeps ONE on-disk system32\ tree
// (native arch -- ARM64 here, our pre-existing flat blob/FS names
// unchanged) plus a SEPARATE syswow64\ tree (the OTHER, 32-bit/i386,
// arch) and a per-process WOW64 flag + 2 fixed path rewrites pick which
// tree a `...\system32\X` open actually reads. Donor-grounded (wine
// mirror, fetched read-only this session, gitignored per HANDOFF):
//   - dlls/wow64/file.c:181 replace_path(attr,prefix_len,"sysnative",
//     "system32",NULL) -- "sysnative" ALWAYS rewrites to "system32"
//     (native-arch escape hatch), even with redirection disabled.
//   - dlls/wow64/file.c:188-189 (get_machine_wow64_dir(current_machine)
//     computes "syswow64" for the i386-on-arm64 case, donor
//     dlls/ntdll/unix/loader.c:1375-1383) replace_path(attr,prefix_len,
//     "system32",syswow64dir,NULL) -- ONLY when running as a WOW64
//     process (this rewrite lives in wow64.dll's OWN file.c, which is
//     only ever loaded/active inside a WOW64 process to begin with).
//   - dlls/ntdll/loader.c:4369 quotes the literal opened path form:
//     "\??\C:\windows\sysnative\wow64cpu.dll" -- confirms the exact
//     3-backslash-segment shape ("windows", the redirect root, the leaf)
//     our dirname parse below matches against.
// Our floor has no real subdirectories (fs.c: flat 64-entry directory,
// SLICE3-SPEC) -- "two roots" is realized as two DISTINCT on-disk name
// prefixes on the SAME flat namespace: existing bare names ("kernel32.dll")
// stay the system32/ARM64 root UNCHANGED (zero regression risk, pe1-pe9
// keep working untouched); a NEW "syswow64/" prefix names the i386-windows
// guest tree (build.sh populate, mkdisk.py). No "system32/" alias entries
// are created on disk -- system32 IS the pre-existing bare-name set, so
// aliasing would only double disk usage for zero behavioral gain.
// =====================================================================
static int nt_wow64_process_active;

void nt_wow64_set_active(int active) { nt_wow64_process_active = active; }
int  nt_wow64_is_active(void) { return nt_wow64_process_active; }

// CHALICE C4-12 leg-12: CPU-backend A/B seam (see ntfile.h receipt). Bare
// static string pointer, same per-boot-scoped-state discipline as the
// wow64-active flag above -- caller (an isolated probe only) sets it
// before nt_boot_process, must clear it after.
static const char *nt_wow64_backend_override_name;
void nt_wow64_set_backend_override(const char *name) { nt_wow64_backend_override_name = name; }

// Case-insensitive length-bounded segment compare (nt_streq_ci needs
// NUL-terminated strings on both sides; this file's path[] IS NUL-terminated
// mid-buffer per-segment via the caller slicing below, so nt_streq_ci
// reapplies cleanly).
static int nt_dirname_is(const char *dirname, const char *want) {
    return nt_streq_ci(dirname, want);
}

long NtOpenFile(void **FileHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                 void *IoStatusBlock, unsigned long ShareAccess, unsigned long OpenOptions) {
    (void)DesiredAccess; (void)ShareAccess; (void)OpenOptions;
    nt_blob_ensure_init();
    if (!FileHandle || !ObjectAttributes) return STATUS_INVALID_PARAMETER;

    u8 *oa  = (u8 *)ObjectAttributes;
    u8 *uni = *(u8 **)(oa + OA_OBJECTNAME_OFF);
    if (!uni) return STATUS_OBJECT_NAME_INVALID;

    u16b wlen_bytes = *(u16b *)(uni + US_LENGTH_OFF);
    u16b *wbuf = *(u16b **)(uni + US_BUFFER_OFF);
    if (!wbuf || wlen_bytes == 0) return STATUS_OBJECT_NAME_INVALID;

    u32b nchars = wlen_bytes / 2;
    if (nchars > NT_PATH_MAX - 1) nchars = NT_PATH_MAX - 1;

    char path[NT_PATH_MAX];
    for (u32b i = 0; i < nchars; i++) path[i] = (char)wbuf[i];
    path[nchars] = 0;

    // strip to basename: text after the last backslash (spec example:
    // "\??\C:\windows\system32\kernel32.dll" -> "kernel32.dll").
    const char *base = path;
    int last_bs = -1, prev_bs = -1;
    for (u32b i = 0; i < nchars; i++) {
        if (path[i] == '\\') { prev_bs = last_bs; last_bs = (int)i; base = &path[i + 1]; }
    }

    // CHALICE C4-4: dirname = the path segment immediately before the
    // basename (e.g. "system32", "sysnative", "syswow64") -- only ever
    // present when the path has at least 2 backslashes (bare names like
    // manifest-only test blobs have none, prev_bs stays -1, no redirect).
    char dirname[NT_PATH_MAX];
    dirname[0] = 0;
    if (prev_bs >= 0 && last_bs > prev_bs) {
        int dn = 0;
        for (int i = prev_bs + 1; i < last_bs && dn < NT_PATH_MAX - 1; i++) dirname[dn++] = path[i];
        dirname[dn] = 0;
    }

    // Resolve which on-disk name this open actually reads. Default (no
    // recognized dirname, OR dirname=="system32" while NOT a WOW64
    // process, OR dirname=="sysnative" -- native-arch escape, ALWAYS
    // system32 regardless of WOW64 state per donor file.c:179-181): bare
    // basename, UNCHANGED behavior (this is the only path pe1-pe9 ever
    // exercised, zero regression). Only dirname=="system32" while WOW64-
    // active, or an explicit dirname=="syswow64", redirect to the
    // "syswow64/" root -- and that redirect does NOT fall back to the
    // bare name on a miss (a missing 32-bit DLL must fail as such, not
    // silently serve the wrong-arch 64-bit one).
    char rooted[NT_PATH_MAX];
    const char *lookup_name = base;
    int is_wow64_redirect = 0;
    if (dirname[0] && !nt_dirname_is(dirname, "sysnative")) {
        if (nt_dirname_is(dirname, "syswow64") ||
            (nt_dirname_is(dirname, "system32") && nt_wow64_process_active)) {
            int rn = 0;
            const char *pfx = "syswow64/";
            for (; pfx[rn] && rn < NT_PATH_MAX - 1; rn++) rooted[rn] = pfx[rn];
            for (int i = 0; base[i] && rn < NT_PATH_MAX - 1; i++, rn++) rooted[rn] = base[i];
            rooted[rn] = 0;
            lookup_name = rooted;
            is_wow64_redirect = 1;
        }
    }

    // CHALICE C3c-2k p2 diagnostic: unconditional (not gated behind the
    // 64-count dispatch-log cap) so a SECOND open of an already-mapped
    // module (e.g. "ntdll.dll" self-open during its own module-list
    // self-registration lookup) is visible even late in a long boot trace.
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtOpenFile "); puts(base);
        if (dirname[0]) { puts(" dir="); puts(dirname); }
        if (is_wow64_redirect) { puts(" -> "); puts(lookup_name); }
        puts("\n");
        // re-verify ntdll's export directory is STILL intact at THIS point
        // in the boot sequence (not just at ntdll-map time) -- rules out
        // any later corruption (relocation passes, other DLL maps) vs a
        // pure Wine-guest-side loader-state bug.
        NtImageEntry *ndl = nt_image_find_by_name("ntdll.dll");
        if (ndl) {
            u64b a = 0;
            long rc = pe_find_named_export(ndl->base, ndl->size, "__wine_dbg_header", &a);
            puts("CHALICE: recheck __wine_dbg_header rc="); puthex((u64b)rc);
            puts(" addr="); puthex(a); puts("\n");
        }
    }

    // CHALICE C4-12 leg-12: backend A/B override -- redirect ONLY the
    // literal "wowbox64.dll" open (the real wow64.dll init's hardcoded
    // default backend name) to whatever nt_wow64_set_backend_override was
    // given, e.g. "libwow64fex.dll". Never touches any other name/path
    // (dirname redirect above already resolved lookup_name; this compares
    // the ORIGINAL basename so "syswow64\wowbox64.dll" etc are unaffected
    // -- there is no such file anyway, this is strictly the system32-root
    // CPU-backend open the real boot path performs).
    if (nt_wow64_backend_override_name && nt_streq_ci(base, "wowbox64.dll")) {
        lookup_name = nt_wow64_backend_override_name;
    }

    const unsigned char *bytes; u64b bloblen; const char *manifest_name;
    if (!nt_blob_lookup_or_fs(lookup_name, &bytes, &bloblen, &manifest_name)) return STATUS_OBJECT_NAME_NOT_FOUND;

    void *h = nt_handle_alloc(NTH_FILE, bytes, bloblen, manifest_name);
    if (!h) return STATUS_NO_MEMORY;
    *FileHandle = h;

    if (IoStatusBlock) {
        NtIoStatusBlock *iosb = (NtIoStatusBlock *)IoStatusBlock;
        iosb->status = STATUS_SUCCESS;
        iosb->information = 1;   // FILE_OPENED
    }
    return STATUS_SUCCESS;
}

long NtReadFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                void *IoStatusBlock, void *Buffer, unsigned long Length,
                void *ByteOffset, unsigned long *Key) {
    (void)Event; (void)ApcRoutine; (void)ApcContext; (void)Key;
    NtHandleEntry *fh = nt_handle_get(FileHandle, NTH_FILE);
    if (!fh) return STATUS_INVALID_HANDLE;
    if (!Buffer) return STATUS_INVALID_PARAMETER;

    u64b offset = ByteOffset ? *(u64b *)ByteOffset : 0;
    u64b avail = offset < fh->bloblen ? fh->bloblen - offset : 0;
    u64b copy = (u64b)Length < avail ? (u64b)Length : avail;

    u8 *dst = (u8 *)Buffer;
    for (u64b i = 0; i < copy; i++) dst[i] = fh->blob[offset + i];

    if (IoStatusBlock) {
        NtIoStatusBlock *iosb = (NtIoStatusBlock *)IoStatusBlock;
        iosb->status = STATUS_SUCCESS;
        iosb->information = copy;
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// NtCreateSection(SEC_IMAGE) -- over a file handle only; anything else ->
// STATUS_INVALID_PARAMETER.
// =====================================================================
long NtCreateSection(void **SectionHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                      void *MaximumSize, unsigned long SectionPageProtection,
                      unsigned long AllocationAttributes, void *FileHandle) {
    (void)DesiredAccess; (void)ObjectAttributes; (void)MaximumSize; (void)SectionPageProtection;
    if (!SectionHandle || !FileHandle) return STATUS_INVALID_PARAMETER;
    if ((AllocationAttributes & SEC_IMAGE) == 0) return STATUS_INVALID_PARAMETER;

    NtHandleEntry *fh = nt_handle_get(FileHandle, NTH_FILE);
    if (!fh) return STATUS_INVALID_HANDLE;

    void *h = nt_handle_alloc(NTH_SECTION, fh->blob, fh->bloblen, fh->name);
    if (!h) return STATUS_NO_MEMORY;
    *SectionHandle = h;
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-26 -- guest-content frontier fix (root cause of C4-24/C4-25's
// STATUS_ILLEGAL_INSTRUCTION: FEXCore Thread->CurrentFrame->State.rip==0
// execute-fault at the very first BTCpuSimulate, PROVEN by C4-25's live
// receipt -- WOW64_CPURESERVED's I386_CONTEXT.Eip correctly held our
// seeded entry 0x47661000 the entire time, so the seed was never wrong;
// wow64.dll's own thread_init just never installed it into the CPU
// backend's real register state).
//
// Donor-grounded: on real Wine, SYSTEM_DLL_INIT_BLOCK (the struct the
// NATIVE 64-bit ntdll.dll exports as "LdrSystemDllInitBlock", wine
// include/winternl.h:4422-4438) is populated ENTIRELY by the Unix-HOST-
// SIDE process loader (dlls/ntdll/unix/loader.c:1598-1633
// load_ntdll_wow64_functions -- a pure static PE-export-table walk
// against the just-loaded 32-bit ntdll.dll, find_named_export, ZERO guest
// execution needed) -- long BEFORE any guest CPU code runs. This floor
// has no "unix side" (ntboot.c/ntsvc.c together fill that role, C-side,
// for everything else already), and nothing here ever did this
// population -- the struct stayed at its raw DLL static-data zero-init
// (loader.c:77 "SYSTEM_DLL_INIT_BLOCK LdrSystemDllInitBlock = {0xf0};",
// only `version` nonzero). wine dlls/wow64/syscall.c:1069-1097
// thread_init() then ran `ctx.Eip = pLdrSystemDllInitBlock->
// pLdrInitializeThunk; pBTCpuSetContext(...)` and installed Eip=0 as the
// WOW64 guest thread's REAL starting register state -- FEXCore's
// ExecuteThread immediately took an execute-fault at guest RIP 0,
// reported as the RAISE#1 STATUS_ILLEGAL_INSTRUCTION C4-24 diagnosed and
// C4-25 traced to source.
//
// Fix: replicate load_ntdll_wow64_functions here, C-side, triggered at
// the exact moment THIS syscall finishes mapping a 32-bit (i386, Machine
// 0x14C) module literally named "ntdll.dll" -- this floor's equivalent of
// "the 32-bit ntdll.dll just got loaded". Patches the NATIVE ntdll's own
// LdrSystemDllInitBlock export (registered during nt_boot_process's
// Phase-0, long before any wow64 rig exists -- nt_image_find_by_name
// always returns that FIRST "ntdll.dll" registry entry here, never the
// one this very call is about to register) with the 32-bit ntdll's own
// LdrInitializeThunk/RtlUserThreadStart/KiUser*Dispatcher exports +
// ntdll_handle + the self-pointer to its OWN (32-bit-side)
// LdrSystemDllInitBlock export, then mirrors the whole struct into that
// 32-bit copy too (loader.c:1631-1633's own "also set the 32-bit
// LdrSystemDllInitBlock" step). SYSTEM_DLL_INIT_BLOCK field offsets
// (LP64 natural layout, matches the donor struct field order/sizes):
// version@0(4) unknown1[3]@4(12) unknown2@0x10(8) pLdrInitializeThunk@
// 0x18(8) pKiUserExceptionDispatcher@0x20(8) pKiUserApcDispatcher@0x28(8)
// pKiUserCallbackDispatcher@0x30(8) pRtlUserThreadStart@0x38(8)
// pRtlpQueryProcessDebugInformationRemote@0x40(8) ntdll_handle@0x48(8)
// pLdrSystemDllInitBlock@0x50(8) pRtlpFreezeTimeBias@0x58(8) -- total
// 0x60 (96B).
// =====================================================================
// CHALICE C4-29: no longer static -- ntboot.c's nt_boot_process now calls
// this directly (Phase-0, C-side, matching Wine's own unix-side
// load_ntdll_wow64_functions timing) instead of relying solely on the
// NtMapViewOfSection hook below, which C4-29's own fresh measurement
// confirmed still never fires on pe10's boot path (no 32-bit ntdll is
// ever guest-mapped via that syscall -- see FEX-MAP.md C4-29). Both call
// sites coexist: whichever fires first wins, the other is a harmless
// (Machine-gated, name-gated) no-op re-check.
void nt_wow64_populate_init_block_if_ntdll32(const char *name, u64b base32, u64b size32) {
    if (!name || !nt_streq_ci(name, "ntdll.dll")) return;

    // Machine check straight off the just-mapped image header (base32 is a
    // live, readable kernel pointer -- PeMapImage already validated/copied
    // it; same DOS_E_LFANEW=0x3C / COFF Machine@pe+4 layout pemod.c uses).
    u32b e_lfanew = rd32((const u8 *)(u64b)base32 + 0x3C);
    u16b machine = (u16b)rd32((const u8 *)(u64b)base32 + e_lfanew + 4);
    if (machine != 0x014C) return;   // native (ARM64) ntdll's own open -- not the 32-bit one

    NtImageEntry *native = nt_image_find_by_name("ntdll.dll");
    if (!native || native->base == base32) return;   // no native entry yet, or somehow self

    u64b initblock = 0;
    if (pe_find_named_export(native->base, native->size, "LdrSystemDllInitBlock", &initblock) != 0) {
        puts("CHALICE C4-26: native ntdll missing LdrSystemDllInitBlock export -- skip\n");
        return;
    }

    u64b thunk = 0, rtlstart = 0, kuxd = 0, kuad = 0, kucd = 0, owninit = 0;
    pe_find_named_export(base32, size32, "LdrInitializeThunk", &thunk);
    pe_find_named_export(base32, size32, "RtlUserThreadStart", &rtlstart);
    pe_find_named_export(base32, size32, "KiUserExceptionDispatcher", &kuxd);
    pe_find_named_export(base32, size32, "KiUserApcDispatcher", &kuad);
    pe_find_named_export(base32, size32, "KiUserCallbackDispatcher", &kucd);
    pe_find_named_export(base32, size32, "LdrSystemDllInitBlock", &owninit);

    u8 *ib = (u8 *)(u64b)initblock;
    wr64(ib + 0x18, thunk);
    wr64(ib + 0x20, kuxd);
    wr64(ib + 0x28, kuad);
    wr64(ib + 0x30, kucd);
    wr64(ib + 0x38, rtlstart);
    wr64(ib + 0x48, base32);
    wr64(ib + 0x50, owninit);
    if (owninit) {
        u8 *ib32 = (u8 *)(u64b)owninit;
        for (u64b i = 0; i < 0x60; i++) ib32[i] = ib[i];
    }
    puts("CHALICE C4-26: LdrSystemDllInitBlock populated thunk="); puthex(thunk);
    puts(" rtlstart="); puthex(rtlstart); puts(" kuxd="); puthex(kuxd);
    puts(" kuad="); puthex(kuad); puts(" kucd="); puthex(kucd);
    puts(" ntdll32_base="); puthex(base32); puts(" owninit="); puthex(owninit); puts("\n");
}

// =====================================================================
// CHALICE C4-46 -- FEX InvalidationTracker notification for DYNAMICALLY
// (post-BTCpuProcessInit) mapped guest x86 images.
//
// Root cause chain (pe10 fex postfix receipt, scratch/c4-45_pe10_fex_
// receipt_postfix.log + /tmp/gaia-pe10-probe.log): once C4-45's DllPath
// fix let kernel32.dll+kernelbase.dll genuinely load, the guest's very
// first instruction fetch into kernelbase.dll -- its own PE entry point,
// disasm-confirmed byte-for-byte against stock/i386-windows/kernelbase.dll
// as `_DllMainCRTStartup@12` at RVA 0x68BD0 (objdump: `nop;push ebp;...`,
// exactly matching the donor file, NOT a section-boundary/truncation
// issue -- .text spans RVA [0x1000,0x69F15), entry sits comfortably
// inside it) -- second-chance-terminates with `NtRaiseException
// code=0xC0000005 addr=kernelbase.dll+0x68BD0`, `ExceptionInformation[0]=8`
// (EXECUTE-type AV). LIVE-confirmed (not a stale nt_wow64_cpureserved_dbg
// read, C4-45's own flagged staleness class -- ruled out here via TWO
// independent fresh sources: the guest's own `NtSetInformationThread/
// NtQueryInformationThread ThreadWow64Context` round-trips reporting
// `eip=0x5E228BD0` moments before the raise, AND the ExceptionRecord's own
// `ExceptionAddress` field, both agreeing byte-for-byte). The underlying
// ARM64-side signature (kmain.c's C4-31 SynchronousFaultData dump:
// `Signal=SIGSEGV(11) TrapNo=PF(14) si_code=2(SEGV_ACCERR)
// err_code=0x15(Present|InstrFetch)`) byte-matches FEX's own documented
// `NoExecOp` self-fault (FEX-MAP.md C4-31/32) -- FEX's frontend decoder
// believes the fetch address is not executable.
//
// Donor-traced why (FEX-MAP.md C4-31/32, `FEX/Source/Windows/WOW64/
// Module.cpp`): `InvalidationTracker::XIntervals` (the executable-range
// interval tree `FEXCore::Frontend::Decoder::CheckRangeExecutable`
// consults) is populated exactly THREE ways: (1) the tracker's own ctor
// `VirtualQuery` sweep of whatever is ALREADY mapped/committed at
// `BTCpuProcessInit` time (Module.cpp:520ish, one-shot, early); (2)
// `BTCpuProcessInit`'s own two explicit `HandleImageMap` calls, for
// `PEB.ImageBaseAddress` (the main EXE) and `LdrSystemDllInitBlock-
// >ntdll_handle` (the 32-bit ntdll) -- both already wired+fixed on this
// floor (C4-29); (3) the `BTCpuNotifyMapViewOfSection` export
// (`Module.cpp:1009-1013`, real signature quoted FEX-MAP.md C4-32),
// which on REAL Wine fires from `dlls/wow64/syscall.c`'s own
// `NtMapViewOfSection` THUNK, immediately after the real 32-on-64
// syscall returns for any image SUBSEQUENTLY mapped (i.e. anything
// loaded after process-start via the guest's own `LdrLoadDll` --
// kernel32.dll/kernelbase.dll here). This floor's `NtMapViewOfSection`
// (below) is a DIRECT kernel-mode syscall implementation with no such
// wow64-thunk layer in between (confirmed: grepped this whole tree for
// any `BTCpuNotify*`/`HandleImageMap` call site outside doc/*.md files --
// zero hits before this fix) -- every guest x86 DLL loaded via a REAL
// runtime `NtMapViewOfSection` call (as opposed to this floor's own
// Phase-0 C-direct exe/ntdll32 maps, which precede path (1)/(2) above)
// NEVER reaches FEX's `InvalidationTracker` at all. wow64.dll/the CPU
// backend itself are unaffected (native ARM64 code, called directly via
// `BTCpu*` function pointers, never decoded by FEX's x86 frontend --
// no `XIntervals` membership needed).
//
// Fix: replicate the (bypassed) real wow64.dll thunk's own post-map
// notify call directly, from this floor's own `NtMapViewOfSection`, for
// every DYNAMICALLY mapped GUEST X86 (Machine 0x14C) image other than
// ntdll.dll (which already has its own dedicated, working path (2) above
// -- avoiding a redundant/overlapping XIntervals insert, C4-31's own
// flagged-but-unchased worry about duplicate-insert interval-tree
// correctness). `BTCpuNotifyMapViewOfSection`'s address is resolved
// lazily, the FIRST time `pe_find_named_export` finds it on ANY mapped
// image (a cheap, harmless probe on every other image -- only the CPU
// backend DLL itself, mapped earlier in boot under whichever name C4-12's
// A/B override redirected it to, actually has this export) -- same
// resolve-by-export-name idiom this floor already uses everywhere else
// (C4-4/C4-26/C4-27/C4-29), no new mechanism invented. Per FEX-MAP.md's
// own quoted decompile, the function only locks a mutex then calls
// `HandleImageMap(Address)` (`MainImage` defaults false) -- a pure
// `RtlImageNtHeader(Address)`-driven section walk needing no other
// argument; `Unk1`/`Unk2`/`AllocType`/`Prot` are passed as harmless
// placeholders, never read by the donor body quoted.
// =====================================================================
typedef long (*BTCpuNotifyMapViewOfSectionFn)(void *Unk1, void *Address, void *Unk2,
                                               u64b Size, unsigned long AllocType,
                                               unsigned long Prot);
static u64b nt_btcpu_notify_map_addr;
int nt_wow64_ready;   // CHALICE C4-53 -- see set-site in nt_wow64_maybe_notify_map_view.
                      // WAS file-static; widened to extern (ntfile.h) this
                      // atom (C4-64) so ntsvc.c's svc-entry dispatch log can
                      // gate its own probe on it -- no other file touches it.

// CHALICE C4-48 bug1 fix: `teb` is the guest TEB captured by the CALLER
// (NtMapViewOfSection, below) as its OWN first statement -- i.e. the SAME
// x18 nt_dispatch installed for this syscall, read via the `mov %0,x18`
// idiom already used elsewhere on this floor (ntsvc.c:589/1221/1331)
// BEFORE any intervening kernel C (this function's own callee chain --
// pe_find_named_export/nt_streq_ci/rd32 etc, all plain aarch64-elf C, free
// to clobber x18 as scratch, no -ffixed-x18) can touch the register. It is
// threaded down explicitly as a parameter (not re-read here) because BY
// THE TIME this function runs, x18 itself may already be garbage (C4-47's
// pinned root cause). Reinstalled right at the native-call boundary via
// pe_enter6 (ntdisp.S) -- the same stash/restore bracket every other
// native/PE crossing on this floor already uses (nt_dispatch x22/x24,
// pe_enter/pe_enter1 x9), just extended to this 6-arg native callee.
static void nt_wow64_maybe_notify_map_view(const char *name, u64b base, u64b size, u64b teb) {
    // Lazily resolve BTCpuNotifyMapViewOfSection's address off whichever
    // image is actually the CPU backend -- harmless no-op probe on every
    // other image (export not found -> pe_find_named_export fails fast).
    if (!nt_btcpu_notify_map_addr) {
        u64b addr = 0;
        if (pe_find_named_export(base, size, "BTCpuNotifyMapViewOfSection", &addr) == 0 && addr) {
            nt_btcpu_notify_map_addr = addr;
            puts("CHALICE C4-46: BTCpuNotifyMapViewOfSection resolved backend="); puts(name ? name : "?");
            puts(" addr="); puthex(addr); puts("\n");
        }
    }
    if (!nt_btcpu_notify_map_addr || !name) return;

    // Only guest x86 (Machine 0x14C) images -- native ARM64 images
    // (wow64.dll/the backend/win32u.dll/wow64win.dll) are never decoded by
    // FEX's x86 frontend and don't need XIntervals membership. Skip
    // ntdll.dll -- already covered by BTCpuProcessInit's own dedicated
    // LdrSystemDllInitBlock-driven HandleImageMap call (C4-29).
    u32b e_lfanew = rd32((const u8 *)(u64b)base + 0x3C);
    u16b machine = (u16b)rd32((const u8 *)(u64b)base + e_lfanew + 4);
    if (machine != 0x014C) return;
    if (nt_streq_ci(name, "ntdll.dll")) return;

    BTCpuNotifyMapViewOfSectionFn fn = (BTCpuNotifyMapViewOfSectionFn)(u64b)nt_btcpu_notify_map_addr;
    // CHALICE C4-48: call through pe_enter6, not a raw C fn() call -- installs
    // the caller-captured guest TEB into x18 right at this native boundary,
    // regardless of whatever x18 currently holds (C4-47's pinned bug1: this
    // raw call site had NO such bracket, so nested native code (e.g.
    // GetSectionFilePath's own NtQueryVirtualMemory stub) read garbage TEB).
    long rc = pe_enter6((long (*)(void *, void *, void *, unsigned long, unsigned long, unsigned long))fn,
                         (void *)teb, 0, (void *)base, 0, size, 0, PAGE_EXECUTE_READWRITE);
    puts("CHALICE C4-46: BTCpuNotifyMapViewOfSection name="); puts(name);
    puts(" base="); puthex(base); puts(" size="); puthex(size);
    puts(" rc="); puthex((u64b)rc); puts("\n");

    // CHALICE C4-53: mark FEX's WOW64 runtime (InvalidationTracker et al.)
    // as constructed/ready -- we only ever reach this line for a REAL guest
    // x86 (Machine 0x14C) image other than ntdll.dll, which can only happen
    // AFTER BTCpuProcessInit has already run (the guest process must already
    // be executing under FEX, via BTCpuSimulate, to have issued the LdrLoadDll
    // that led here) -- i.e. InvalidationTracker.emplace(...) (FEX-MAP.md
    // C4-32) is guaranteed to have already happened. Gates nt_wow64_maybe_
    // notify_protect below from firing during the EARLIER native-ARM64-module
    // (wow64.dll/libwow64fex.dll/wow64win.dll/win32u.dll) load phase, where
    // this floor's own NtProtectVirtualMemory already fires repeatedly
    // (confirmed live, C4-53 receipt) but InvalidationTracker does not exist
    // yet -- calling into it that early faults (uninitialized shared_mutex).
    nt_wow64_ready = 1;

    // CHALICE C4-54: retroactively register this floor's OWN C4-27
    // syscall-hinge stub page (nt_territory_alloc(16), ntboot.c) with FEX's
    // InvalidationTracker, the FIRST moment nt_wow64_ready allows it. That
    // stub page is NOT allocated via any guest-visible NtAllocateVirtual-
    // Memory call at all -- it's this floor's own internal bookkeeping
    // buffer, written by nt_wow64_stub_write_if_ready (ntsvc.c) during
    // BTCpuProcessInit itself (well before this function -- reached only via
    // a REAL dynamic LdrLoadDll -- can ever run), so it crosses NEITHER of
    // the two syscalls this floor forwards to FEX's notify chain and
    // XIntervals never learns about it. It is, by construction, always
    // already fully written (or never allocated at all, on a non-wow64
    // boot) by the time control can reach here, so a plain one-shot
    // registration exactly here (paired with the SAME nt_wow64_ready gate
    // this whole notify chain already uses) is sufficient -- no separate
    // guest syscall exists to hook for this floor-internal buffer, unlike a
    // genuine guest EXEC NtAllocateVirtualMemory call (see ntsvc.c's own
    // NtAllocateVirtualMemory, wired separately, same atom).
    {
        u64b c454_stub_base = 0, c454_stub_size = 0;
        if (nt_wow64_stub_range(&c454_stub_base, &c454_stub_size))
            nt_wow64_maybe_notify_protect(c454_stub_base, c454_stub_size, PAGE_EXECUTE_READWRITE, teb);
    }

    // CHALICE C4-53: the C4-52 experimental direct HandleImageMap bypass
    // call (kept as a diagnostic-only crutch through C4-52) is REMOVED here
    // -- the proper donor contract now holds without it: C4-53's own
    // protect-notify wiring (nt_wow64_maybe_notify_protect, below) plus the
    // NtProtectVirtualMemory OldProtect fix make the guest loader's OWN
    // save/restore VirtualProtect idiom around kernelbase.dll's `.text`
    // section correctly re-insert the executable range via the REAL
    // BTCpuNotifyMemoryProtect-equivalent path -- proven live this atom
    // (`scratch/c4-53_pe10_fex_receipt_full.log`: kernelbase_entry
    // enclosed=1 at the (new, later) fault time, with the C4-52 crutch
    // already removed from this build). No bypass of any wrapper needed
    // anymore for this range.
}

// =====================================================================
// NtMapViewOfSection -- runs PeMapImage (pemod.c) over the section's blob
// at a fresh DLL-territory base, NO kernel-side DIR64 relocs (CHALICE
// C3c-2q -- proven live: the stock Wine-built guest ntdll.dll's own
// PE-side loader (build_module -> perform_relocations) unconditionally
// self-relocates any module not at its own preferred ImageBase, regardless
// of NtMapViewOfSection's return status; real Wine's Unix-side
// NtMapViewOfSection never relocates SEC_IMAGE maps either -- applying
// relocations here duplicated the guest's own pass, landing pointer tables
// like kernelbase.dll's root_key_names[] at file+2*delta instead of
// file+delta), NO import resolution. Always STATUS_SUCCESS (never
// IMAGE_NOT_AT_BASE, per spec -- the guest doesn't need that signal, it
// relocates unconditionally on address mismatch either way). Records an
// image-registry entry.
// =====================================================================
// =====================================================================
// CHALICE C4-57: kernel-side i386 IAT binder -- Road B (HANDOFF/FEX-MAP
// C4-56/C4-57). MEASURED root cause (C4-57 receipt): wine's OWN
// guest-executed loader chain (ntdll32 LdrInitializeThunk -> loader_init
// -> build_main_module(LDR_DONT_RESOLVE_REFS) -> explicit
// `fixup_imports(wm, NULL)` on the MAIN module, donor `dlls/ntdll/
// loader.c:4507-4513`) is real and DOES run for `kernel32.dll`/
// `kernelbase.dll` themselves (C4-42 through C4-45's own proven,
// guest-executed, WOW64-thunked NtOpenFile/NtCreateSection/
// NtMapViewOfSection chain, correct `syswow64/` i386 resolution, correct
// DLL-to-DLL `fixup_imports` for kernelbase's OWN imports) -- but that
// chain's entry point is Hangover/FEX's OWN per-process container
// bootstrap (`Source/Windows/WOW64/Module.cpp`, native ARM64,
// `BTCpuProcessInit`-driven), which loads SYSTEM DLLs for FEX's own
// bookkeeping and NEVER reaches wine's PE-side USER EXE loader at all --
// this floor's own Phase-0 (`nt_boot_process`, `ntboot.c`) maps the boot
// EXE directly (`do_imports=0`, `nt_wow64_set_initial_context` seeds the
// guest's FIRST Eip straight at the EXE's own entry point, never at any
// ntdll32 `LdrInitializeThunk`), so `build_main_module`/`fixup_imports`
// never run for the boot EXE's OWN Import Directory -- its IAT slots stay
// at their unbound, on-disk (zero) values (`hello9_x86.exe`'s
// `KERNEL32.dll` import of `GetStdHandle`/`WriteFile`).
//
// Fix: once BOTH `syswow64/kernel32.dll` and `syswow64/kernelbase.dll`
// are guest-mapped (real addresses, real export tables, proven live by
// the SAME chain above), walk the boot EXE's OWN PE32 Import Directory
// (`IMAGE_IMPORT_DESCRIPTOR` array, `IMAGE_THUNK_DATA32`/`IMAGE_IMPORT_
// BY_NAME` shapes -- 4-byte thunks, NOT `pemod.c`'s existing `do_imports`
// code, which hardcodes 8-byte `IMAGE_THUNK_DATA64` reads/writes and
// resolves via `zc_resolve()`, the kernel-native-stub table -- the wrong
// tool for a guest i386 IAT against real `KERNELBASE`/`KERNEL32` exports,
// HANDOFF's own C4-56 verdict) and resolve each imported name via the
// ALREADY is_pe32-aware `pe_find_named_export` (C4-36) against those two
// real, live-mapped images -- writing the resolved 32-bit VA straight
// into the IAT slot. One-shot (`nt_iat_bind_done`), fires from
// `NtMapViewOfSection` the moment the second of the two DLLs registers
// (same call-site-driven-notify idiom C4-46/48/53/54's own
// InvalidationTracker hooks already use), always strictly BEFORE the
// guest's OWN Eip (seeded at the EXE's entry point) ever executes --
// `kernel32.dll`/`kernelbase.dll` are always mapped well before `PeMain`
// runs (both DLL loads are driven by FEX's OWN container bootstrap,
// which completes long before this floor's synthetic guest Eip/Esp seed
// is ever read by `BTCpuSimulate`).
// =====================================================================
#define C57_IMPDESC_ENTRY_SIZE 20
#define C57_IMPDESC_OFT   0    // OriginalFirstThunk
#define C57_IMPDESC_NAME  12
#define C57_IMPDESC_FT    16   // FirstThunk
#define C57_DOS_E_LFANEW  0x3C
#define C57_OPT_MAGIC_OFF (4 + 20)          // after "PE\0\0" + 20B COFF header
#define C57_PE32_MAGIC    0x10B
#define C57_OPT_NUMDIRS32 0x5C
#define C57_OPT_DATADIRS32 0x60
#define C57_IMAGE_DIR_IMPORT 1

static int nt_iat_bind_done;

// CHALICE C4-66 (Step 2b): case-insensitive BASENAME search of the live
// image registry -- an import descriptor's own DLL name ("KERNEL32.dll",
// possibly mixed-case per the real PE file) is matched against each
// registry entry's own trailing path component (registry names carry a
// synthetic "syswow64/" prefix, C4-49's own convention; bare-mapped
// images like the boot EXE have no prefix at all) -- so "KERNEL32.dll"
// matches a registered "syswow64/kernel32.dll" entry without either side
// needing to know about the other's naming convention. No fixed name
// list -- resolves against WHATEVER happens to be mapped right now.
static int nt_image_find_by_basename(const char *dllname, u64b *base_out, u64b *size_out) {
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (!nt_images[i].used) continue;
        const char *rn = nt_images[i].name;
        const char *rbase = rn;
        for (const char *p = rn; *p; p++) if (*p == '/') rbase = p + 1;
        if (nt_streq_ci(rbase, dllname)) {
            *base_out = nt_images[i].base;
            *size_out = nt_images[i].size;
            return 1;
        }
    }
    return 0;
}

// CHALICE C4-57 (generalized, C4-66 Step 2b): walks ONE already-mapped
// PE32 (i386) image's own Import Directory (IMAGE_IMPORT_DESCRIPTOR
// array, IMAGE_THUNK_DATA32/IMAGE_IMPORT_BY_NAME shapes -- 4-byte
// thunks) and binds every thunk it can resolve. Originally hardcoded to
// the boot EXE + a kernel32-then-kernelbase guess (C4-57); C4-66
// generalizes both axes per its own task: (1) ANY mapped image, not just
// the boot EXE -- caller loops the registry, this function only cares
// about the one image handed to it; (2) the import's target DLL is
// resolved EXACTLY, by the descriptor's own name against the live
// registry (nt_image_find_by_basename, above), not guessed; (3) ordinal
// imports (raw thunk bit31 set) are now resolved too, via the new
// pe_find_export_by_ordinal (pemod.c) sibling of the existing
// pe_find_named_export. Non-PE32 images (native ARM64 wow64-support
// DLLs -- wow64.dll/wow64win.dll/win32u.dll/the CPU backend, all real on
// this floor) are silently skipped by the magic check, same as before --
// no separate arch flag needed, no hardcoded name list to exclude them.
static void nt_iat_bind_image(u64b img_base, u64b img_size) {
    const u8 *base = (const u8 *)img_base;
    u32b e_lfanew = rd32(base + C57_DOS_E_LFANEW);
    const u8 *opt = base + e_lfanew + C57_OPT_MAGIC_OFF;
    u16b magic = (u16b)(opt[0] | (opt[1] << 8));
    if (magic != C57_PE32_MAGIC) return;   // not i386/PE32 -- e.g. a native ARM64 wow64-support DLL
    u32b num_dirs = rd32(opt + C57_OPT_NUMDIRS32);
    if (num_dirs <= C57_IMAGE_DIR_IMPORT) return;
    const u8 *dirs = opt + C57_OPT_DATADIRS32;
    u32b irva = rd32(dirs + C57_IMAGE_DIR_IMPORT * 8);
    u32b isz  = rd32(dirs + C57_IMAGE_DIR_IMPORT * 8 + 4);
    if (!irva || !isz) return;   // no Import Directory -- nothing to bind

    puts("CHALICE C4-66: binding IAT for image base="); puthex(img_base); puts("\n");
    int bound = 0, failed = 0;
    const u8 *desc = base + irva;
    for (;;) {
        if ((u64b)(desc - base) + C57_IMPDESC_ENTRY_SIZE > img_size) break;
        u32b oft   = rd32(desc + C57_IMPDESC_OFT);
        u32b nrva  = rd32(desc + C57_IMPDESC_NAME);
        u32b ft    = rd32(desc + C57_IMPDESC_FT);
        if (!oft && !nrva && !ft) break;   // null terminator

        const char *dllname = (const char *)(base + nrva);
        u64b tgt_base = 0, tgt_size = 0;
        int have_target = nt_image_find_by_basename(dllname, &tgt_base, &tgt_size);
        puts("CHALICE C4-66: import DLL="); puts(dllname);
        puts(have_target ? " (mapped)\n" : " (NOT mapped -- skipped)\n");

        u32b thunk_rva = oft ? oft : ft;
        const u8 *thunk = base + thunk_rva;
        u8 *iat = (u8 *)(base + ft);
        for (u32b j = 0; ; j++) {
            u32b tval = rd32(thunk + j * 4);
            if (!tval) break;
            u64b addr = 0;
            long rc = -1;
            if (have_target) {
                if (tval & 0x80000000UL) {
                    rc = pe_find_export_by_ordinal(tgt_base, tgt_size, tval & 0xFFFFUL, (unsigned long *)&addr);
                } else {
                    const char *fname = (const char *)(base + tval + 2);   // skip Hint(u16)
                    rc = pe_find_named_export(tgt_base, tgt_size, fname, (unsigned long *)&addr);
                }
            }
            if (rc == 0) {
                wr32(iat + j * 4, (u32b)addr);
                if (!(tval & 0x80000000UL)) {
                    puts("CHALICE C4-66: bound "); puts((const char *)(base + tval + 2));
                    puts(" -> "); puthex(addr); puts("\n");
                } else {
                    puts("CHALICE C4-66: bound ordinal "); puthex((u64b)(tval & 0xFFFFUL));
                    puts(" -> "); puthex(addr); puts("\n");
                }
                bound++;
            } else {
                failed++;
            }
        }
        desc += C57_IMPDESC_ENTRY_SIZE;
    }
    puts("CHALICE C4-66: IAT bind done base="); puthex(img_base);
    puts(" bound="); puthex((u64b)bound);
    puts(" failed="); puthex((u64b)failed); puts("\n");
}

// Finds the sole is_dll==0 entry in the image registry -- the boot EXE
// itself (nt_boot_process's own nt_image_register(exe_name,...,0) call,
// ntboot.c) -- the ONLY registry entry ever registered with is_dll==0 on
// a wow64 boot (every DLL, incl. ntdll/ntdll32/kernel32/kernelbase/wow64.
// dll/the CPU backend, registers with is_dll==1).
// CHALICE C4-65: was static (C4-57's own private helper); exposed so
// ntsvc.c's per-syscall diagnostic (C4-64 probe extension) can locate the
// SAME boot-exe registry entry the IAT binder below reads from, to watch
// the live IAT-slot content across the whole boot rather than one snapshot.
int nt_find_boot_exe(u64b *base_out, u64b *size_out) {
    for (int i = 0; i < NT_IMG_MAX; i++) {
        if (nt_images[i].used && !nt_images[i].is_dll) {
            *base_out = nt_images[i].base;
            *size_out = nt_images[i].size;
            return 1;
        }
    }
    return 0;
}

// =====================================================================
// CHALICE C4-66: DIAGNOSTIC ONLY (measurement, no functional change) --
// C4-65 already proved hello9's OWN IAT slot (the one C4-57's binder
// above writes) is correctly bound and durably stable through the fatal
// call. This atom's task names a DIFFERENT, still-unchecked slot: real
// wine kernel32.dll's own exported GetStdHandle/WriteFile are THEMSELVES
// thin thunks (`8bff 55 8bec 5d ff25 <imm32>` -- hotpatch prologue then
// `jmp dword ptr [imm32]`, monad-disasm-confirmed this atom against
// chalice/stock/i386-windows/kernel32.dll, rva=0xd7ac/0xf9bc, byte-
// identical) that tail-jump through kernel32.dll's OWN Import Directory
// slot for kernelbase.dll's real GetStdHandle -- a slot NOTHING on this
// floor's own C code binds (nt_iat_bind_walk above only ever walks the
// BOOT EXE's Import Directory; grep-confirmed, this file, no second call
// site). If that slot is unbound, a guest call that correctly reaches
// kernel32's thunk (decodes fine, real code) would still branch to
// 0/garbage at the SECOND indirection -- same fault signature as C4-58
// through C4-65's own already-documented zero-rip crash, undetectable by
// C4-65's probe (which only ever watched the boot exe's OWN slot).
// Fires once, at the SAME bind event C4-57's binder already gates on
// (both syswow64 kernel32/kernelbase mapped) -- plain volatile reads,
// DLL territory, known-mapped, no fault risk, no control-flow change.
// =====================================================================
static void nt_c466_diag(u64b k32base, u64b k32size, u64b kbbase) {
    const u8 *thunk = (const u8 *)(u64b)(k32base + 0xd7ac);
    puts("CHALICE C4-66: k32_thunk_bytes@k32base+0xd7ac=");
    for (int w = 0; w < 16; w += 4) { puts(" "); puthex((u64b)rd32(thunk + w)); }
    puts("\n");

    // `ff25 <imm32>` operand sits at byte offset 8 of the 12-byte thunk
    // (8bff=2, 55=1, 8bec=2, 5d=1, ff25=2 -> 8), holding the ABSOLUTE VA
    // of kernel32.dll's own IAT slot for kernelbase!GetStdHandle.
    u32b slot_addr = rd32(thunk + 8);
    puts("CHALICE C4-66: k32_own_iat_slot_addr="); puthex((u64b)slot_addr);

    // CHALICE C4-66 HARDENING: kernel32's own IAT necessarily lives INSIDE
    // its own mapped image (the PE loader places the Import Directory's
    // Thunk array as part of the image's own sections) -- an operand that
    // reads OUTSIDE [k32base, k32base+k32size) means this address was never
    // relocated (still the raw file's preferred-base value, 0x7b800000 by
    // convention) and MUST NOT be dereferenced: this floor's device-memory
    // mapping guards only a narrow identity range (see C4-65's own
    // l2_dev_table analysis), so an unrelocated slot address can land on
    // genuinely unmapped/faulting territory -- skip the deref, don't guess.
    if (slot_addr < k32base || (u64b)slot_addr >= k32base + k32size) {
        puts(" k32_own_iat_slot_addr OUTSIDE k32 image (relocs NOT applied?) addr=");
        puthex((u64b)slot_addr); puts("\n");
        return;
    }

    const u8 *slotp = (const u8 *)(u64b)slot_addr;
    u32b slot_val = rd32(slotp);
    puts(" k32_own_iat_slot_live_value="); puthex((u64b)slot_val); puts("\n");

    const u8 *kbcode = (const u8 *)(u64b)(kbbase + 0x4acb0);
    puts("CHALICE C4-66: kernelbase_GetStdHandle_code@kbbase+0x4acb0=");
    for (int w = 0; w < 16; w += 4) { puts(" "); puthex((u64b)rd32(kbcode + w)); }
    puts("\n");
}

static void nt_iat_bind_maybe(void) {
    if (nt_iat_bind_done) return;
    u64b k32base = 0, k32size = 0, kbbase = 0, kbsize = 0, exebase = 0, exesize = 0;
    if (!nt_image_base_by_name("syswow64/kernel32.dll", &k32base, &k32size)) return;
    if (!nt_image_base_by_name("syswow64/kernelbase.dll", &kbbase, &kbsize)) return;
    if (!nt_find_boot_exe(&exebase, &exesize)) return;
    nt_iat_bind_done = 1;   // one-shot regardless of outcome below
    puts("CHALICE C4-57: both syswow64 kernel32/kernelbase mapped -- binding boot exe IAT\n");
    nt_iat_bind_image(exebase, exesize);
    nt_c466_diag(k32base, k32size, kbbase);   // CHALICE C4-66: diagnostic, see above

    // CHALICE C4-66 FIX (Step 2b, gated on the diagnostic above measuring
    // kernel32.dll's OWN internal IAT slot for kernelbase!GetStdHandle as
    // genuinely unbound -- not a correct kernelbase code VA, so the STOP
    // LAW at C4-65's own stop condition does NOT apply here): C4-57's
    // binder only ever bound the BOOT EXE's own Import Directory --
    // nothing on this floor ever bound kernel32.dll's or kernelbase.dll's
    // OWN imports from each other/ntdll, real gaps a genuine Windows
    // loader would always close. Generalize to every OTHER
    // currently-mapped image at this SAME one-shot bind event -- no fixed
    // name list, no fixed count: loops the live registry, nt_iat_bind_
    // image's own PE32 magic check silently skips anything that isn't a
    // real i386 image (native ARM64 wow64-support DLLs), and its own
    // Import-Directory-empty check silently skips anything with nothing
    // to bind (e.g. ntdll.dll typically has none).
    for (int ci = 0; ci < NT_IMG_MAX; ci++) {
        if (!nt_images[ci].used) continue;
        if (nt_images[ci].base == exebase) continue;   // boot exe already bound above
        nt_iat_bind_image(nt_images[ci].base, nt_images[ci].size);
    }
}

long NtMapViewOfSection(void *SectionHandle, void *ProcessHandle, void **BaseAddress,
                         unsigned long ZeroBits, unsigned long CommitSize, void *SectionOffset,
                         unsigned long *ViewSize, unsigned long InheritDisposition,
                         unsigned long AllocationType, unsigned long Win32Protect) {
    // CHALICE C4-48: capture the guest TEB (x18, installed by nt_dispatch
    // for this very syscall) as the ABSOLUTE FIRST statement of this
    // function -- before any intervening kernel C below (PeMapImage/
    // nt_image_register/etc, all plain aarch64-elf C, free to clobber x18,
    // no -ffixed-x18) gets a chance to. Threaded down to
    // nt_wow64_maybe_notify_map_view's native call site (pe_enter6) instead
    // of re-reading x18 there, where it may already be garbage (C4-47's
    // pinned root cause).
    u64b nt_c4_48_teb; __asm__ volatile("mov %0, x18" : "=r"(nt_c4_48_teb));

    (void)ProcessHandle; (void)ZeroBits; (void)CommitSize; (void)SectionOffset;
    (void)InheritDisposition; (void)AllocationType; (void)Win32Protect;
    if (!BaseAddress || !ViewSize) return STATUS_INVALID_PARAMETER;

    NtHandleEntry *sh = nt_handle_get(SectionHandle, NTH_SECTION);
    if (!sh) return STATUS_INVALID_HANDLE;

    u64b base = 0, size = 0, entry = 0;
    const char *tag = sh->name ? sh->name : "dll";
    long rc = PeMapImage(sh->blob, (unsigned long)sh->bloblen, tag, nt_territory_alloc, 0, 0,
                          &base, &size, &entry);
    if (rc != 0) return STATUS_INVALID_IMAGE_FORMAT;

    *BaseAddress = (void *)base;
    *ViewSize = (unsigned long)size;
    nt_image_register(sh->name, base, size, entry, 1);
    nt_wow64_populate_init_block_if_ntdll32(sh->name, base, size);   // CHALICE C4-26
    nt_wow64_maybe_notify_map_view(sh->name, base, size, nt_c4_48_teb);  // C4-46/C4-48
    nt_iat_bind_maybe();   // CHALICE C4-57: bind the boot exe's own IAT once kernel32+kernelbase are up
    return STATUS_SUCCESS;
}

// =====================================================================
// NtQuerySection(SectionImageInformation) -- parses the section's own PE
// blob (already held by the NTH_SECTION handle NtCreateSection above
// allocated -- sh->blob/bloblen, the same struct NtMapViewOfSection reads)
// via pemod.c's pe_query_image_info, and fills a SECTION_IMAGE_INFORMATION
// out-buffer. This is the gate wine/dlls/ntdll/loader.c's open_dll_file
// runs BEFORE ever calling NtMapViewOfSection: is_valid_binary() rejects
// any candidate whose info->Machine != current_machine (0xAA64 on ARM64),
// so an unwired/zeroed answer here fails EVERY real candidate and the
// search exhausts to STATUS_INVALID_IMAGE_FORMAT.
//
// SECTION_IMAGE_INFORMATION layout (donor wine/include/winternl.h struct,
// LP64 natural C alignment -- offsets computed from the struct's own field
// order/sizes, not assumed): TransferAddress@0x00(8) ZeroBits@0x08(4)
// MaximumStackSize@0x10(8) CommittedStackSize@0x18(8) SubSystemType@0x20(4)
// MinorSubsystemVersion@0x24(2) MajorSubsystemVersion@0x26(2)
// MajorOperatingSystemVersion@0x28(2) MinorOperatingSystemVersion@0x2A(2)
// ImageCharacteristics@0x2C(2) DllCharacteristics@0x2E(2) Machine@0x30(2)
// ImageContainsCode@0x32(1) ImageFlags@0x33(1) LoaderFlags@0x34(4)
// ImageFileSize@0x38(4) CheckSum@0x3C(4) -- total 0x40 (64B), matches the
// well-known Win64 sizeof(SECTION_IMAGE_INFORMATION).
//
// Field semantics cross-checked against wine/dlls/ntdll/unix/virtual.c's
// virtual_fill_image_information() + wine/server/mapping.c's create_mapping
// (server-side PE-header -> pe_image_info fill, the struct this mirrors):
// TransferAddress = PREFERRED ImageBase + AddressOfEntryPoint, NOT a
// runtime-mapped address -- confirmed against loader.c:2735-2743
// (open_dll_file), NtQuerySection is always called on the section BEFORE
// NtMapViewOfSection ever runs, so no mapped address exists yet.
// CheckSum is left 0 (PE checksum field, not read by is_valid_binary or
// the rest of the boot-critical loader path -- SLICE3-SPEC's own
// no-registry-needed-to-boot precedent for low-value optional fields).
// =====================================================================
// CHALICE C4-44: SII_* offsets moved to ntfile.h (shared with ntsvc.c's
// NtQueryInformationProcess ProcessImageInformation, class 37 -- same
// struct shape, one definition).
#define SECTION_IMAGE_INFORMATION_CLASS 1   // SECTION_INFORMATION_CLASS enum index

long NtQuerySection(void *SectionHandle, unsigned long SectionInformationClass,
                     void *SectionInformation, unsigned long SectionInformationLength,
                     unsigned long *ReturnLength) {
    if (SectionInformationClass != SECTION_IMAGE_INFORMATION_CLASS) return STATUS_INVALID_INFO_CLASS;
    if (!SectionInformation) return STATUS_INVALID_PARAMETER;
    if (SectionInformationLength < SII_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    NtHandleEntry *sh = nt_handle_get(SectionHandle, NTH_SECTION);
    if (!sh) return STATUS_INVALID_HANDLE;

    PeImageInfo info;
    if (pe_query_image_info(sh->blob, (unsigned long)sh->bloblen, &info) != 0)
        return STATUS_INVALID_IMAGE_FORMAT;

    u8 *out = (u8 *)SectionInformation;
    for (int i = 0; i < SII_SIZE; i++) out[i] = 0;

    u64b transfer = (u64b)info.image_base + (u64b)info.entry_point_rva;
    wr64(out + SII_TRANSFERADDRESS_OFF, transfer);
    wr64(out + SII_MAXSTACKSIZE_OFF, (u64b)info.stack_reserve);
    wr64(out + SII_COMMITSTACKSIZE_OFF, (u64b)info.stack_commit);
    wr32(out + SII_SUBSYSTEMTYPE_OFF, (u32b)info.subsystem);
    wr16(out + SII_MINORSUBSYS_OFF, info.subsystem_minor);
    wr16(out + SII_MAJORSUBSYS_OFF, info.subsystem_major);
    wr16(out + SII_MAJOROS_OFF, info.os_major);
    wr16(out + SII_MINOROS_OFF, info.os_minor);
    wr16(out + SII_IMAGECHAR_OFF, info.image_characteristics);
    wr16(out + SII_DLLCHAR_OFF, info.dll_characteristics);
    wr16(out + SII_MACHINE_OFF, info.machine);
    out[SII_CONTAINSCODE_OFF] = info.contains_code;
    wr32(out + SII_LOADERFLAGS_OFF, (u32b)info.loader_flags);
    wr32(out + SII_IMAGEFILESIZE_OFF, (u32b)info.file_size);

    if (ReturnLength) *ReturnLength = SII_SIZE;
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-53: complete the protect-notify contract.
//
// C4-53 disasm (objdump -d, real blob, NOT vendored source -- vendored tree
// has no BTCpuNotifyMemoryProtect at all, C4-50's finding): the REAL
// remover is `InvalidationTracker::HandleMemoryProtectionNotification`
// (VMA 0x1801696d4, called by the export `BTCpuNotifyMemoryProtect`,
// VMA 0x1800043e8). Full body traced end-to-end this atom:
//   HandleMemoryProtectionNotification(this, Address, Size, Prot):
//     AlignedBase = Address & ~0xFFF; AlignedEnd = ((Address&0xFFF)+Size+0xFFF) & ~0xFFF + AlignedBase
//     lock(this->SharedMutex @ this+0x30)
//     if (Prot & 0xF0)                      // PAGE_EXECUTE|_READ|_READWRITE|_WRITECOPY (bits 4-7)
//         XIntervals.Insert({AlignedBase, AlignedEnd})       // executable -> INSERT
//     else {
//         // binary-search this->XIntervals (this+0x0) for an overlapping
//         // interval; if found: XIntervals.Remove({AlignedBase,AlignedEnd})
//         // AND a second Remove() on the this+0x18 SMC-disabled list too
//     }
//     if (Prot & 0xC0) LogMan::Msg(Warn,"...RWX..."); XIntervals.Insert(...) again (this+0x18 list)
//     unlock(SharedMutex)
//     if (did-work) InvalidationTracker::InvalidateIntervalInternal(this, AlignedBase, AlignedSize)
//
// i.e. a NewProtect WITHOUT any PAGE_EXECUTE* bit REMOVES the range from
// the SAME `XIntervals` C4-33/49/50/51/52 all read -- exactly the
// mechanism that could explain a later, non-exception-based interval
// removal. Symmetric to C4-46/48's own finding for NtMapViewOfSection:
// this floor's NtProtectVirtualMemory is a DIRECT kernel-mode syscall
// implementation with NO wow64.dll thunk layer calling BTCpuNotifyMemory-
// Protect afterward (grepped this whole tree pre-this-atom: zero call
// sites) -- every guest VirtualProtect (kernel32.dll's own loader-init
// reprotects included) never reached FEX's InvalidationTracker at all,
// same donor-gap class C4-46 already fixed for NtMapViewOfSection.
//
// Wired call site: per C4-52's OWN precedent (`nt_wow64_maybe_notify_map_
// view`'s direct HandleImageMap call, proven safe/correct -- "the WRAPPER
// is the swallower, not HandleImageMap itself"), this calls
// HandleMemoryProtectionNotification DIRECTLY, bypassing
// BTCpuNotifyMemoryProtect's own wrapper -- whose own w3/w4 two-phase
// lock-only/unlock-only/do-work-then-unlock branch protocol (disasm-
// confirmed this atom, `kernel-arm/chalice/FEX-MAP.md` C4-53) is real
// donor ABI we cannot fully pin down without the caller-side donor source
// (not vendored, per C4-50), so calling it with a guessed flag combo would
// risk leaving `libwow64fex.dll`'s own outer mutex @ImageBase+0x34c398
// PERMANENTLY LOCKED (the w3==0 branch takes the lock and returns without
// ever unlocking) -- a genuine deadlock risk this floor cannot recover
// from. The inner function's OWN lock (this+0x30 shared_mutex, disasm-
// confirmed self-contained lock()/unlock() pair inside its own body) is
// safe to call directly, same reasoning as C4-52's own HandleImageMap
// bypass. `this` = fex_base+0x34c430, the SAME fixed InvalidationTracker
// global every prior atom (C4-33/49/50/51/52) has read/written.
//
// Applies unconditionally to every NtProtectVirtualMemory call (no guest-
// x86-image filter, unlike NtMapViewOfSection's notify -- donor semantics:
// BTCpuNotifyMemoryProtect fires for ANY VirtualProtect, arena or image,
// per the SAME function's use inside InvalidationTracker's own ctor
// VirtualQuery sweep, FEX-MAP.md C4-32).
// =====================================================================
static u64b nt_c453_fex_base, nt_c453_fex_size;

// CHALICE C4-54: no longer static -- ntsvc.c's NtAllocateVirtualMemory
// (guest EXEC-protect allocations) and this file's own retroactive
// stub-page registration (nt_wow64_maybe_notify_map_view, below) both call
// this directly. Declared in ntfile.h.
void nt_wow64_maybe_notify_protect(u64b base, u64b size, unsigned long new_protect, u64b teb) {
    // CHALICE C4-53: do NOT resolve/call before FEX's WOW64 runtime is known
    // constructed -- see nt_wow64_ready's own set-site/receipt (this floor's
    // NtProtectVirtualMemory fires ~13 times on native ARM64 modules, e.g.
    // wow64.dll's own relocation/import-protect calls, BEFORE BTCpuProcessInit
    // ever runs -- calling into InvalidationTracker that early faults, live-
    // confirmed this atom: alignment fault inside __shared_mutex_base::lock
    // on the not-yet-constructed global).
    if (!nt_wow64_ready) return;
    if (!nt_c453_fex_base) {
        nt_image_base_by_name("libwow64fex.dll", &nt_c453_fex_base, &nt_c453_fex_size);
    }
    if (!nt_c453_fex_base || !base || !size) return;

    u64b ivt_this = nt_c453_fex_base + 0x34c430;          // InvalidationTracker global (C4-33)
    u64b handle_prot_notify = nt_c453_fex_base + 0x1696d4; // HandleMemoryProtectionNotification, disasm-verified this atom

    long rc = pe_enter6(
        (long (*)(void *, void *, void *, unsigned long, unsigned long, unsigned long))handle_prot_notify,
        (void *)teb, (void *)ivt_this, (void *)base, (void *)size, (unsigned long)new_protect, 0, 0);
    (void)rc;
    puts("CHALICE C4-53: HandleMemoryProtectionNotification base="); puthex(base);
    puts(" size="); puthex(size); puts(" prot="); puthex((u64b)new_protect); puts("\n");
}

// =====================================================================
// NtProtectVirtualMemory -- flat arena = no REAL page protection (our own
// bookkeeping is unchanged, documented deviation per spec); C4-53 ADDS the
// FEX-side notify call above so the CPU-simulation module's OWN
// executable-range tracker (XIntervals) stays correct across guest
// VirtualProtect calls.
//
// C4-53 OldProtect fix: the OLD hardcoded `*OldProtect = 0x04` (PAGE_
// READWRITE) is a REAL, floor-caused correctness bug, named live this
// atom -- not just a "missing notify" gap. Guest PE loaders universally
// use the save/restore idiom around relocation-fixup writes:
//   NtProtectVirtualMemory(Section, PAGE_READWRITE, &OldProtect);  // save
//   <patch relocations>
//   NtProtectVirtualMemory(Section, OldProtect, &discard);         // restore
// Live-confirmed this atom (`scratch/c4-53_pe10_fex_receipt.log`): the
// guest loader does EXACTLY this for kernelbase.dll's own `.text` section
// (base=0x5E1C1000 size=0x69000) -- but because this floor always answered
// `OldProtect=0x04` regardless of the section's TRUE protection (which
// should reflect its `IMAGE_SCN_MEM_EXECUTE` PE characteristic), the
// "restore" call ALSO requests `PAGE_READWRITE` (the wrong value we handed
// back) -- the section's executable range, correctly REMOVED from FEX's
// `XIntervals` by the FIRST (genuinely-non-executable) protect call via
// C4-53's new notify wiring, is NEVER RE-INSERTED, because the "restore"
// call the loader believes is restoring EXECUTE permission is actually
// just ANOTHER no-exec-bit PAGE_READWRITE call. Fix: derive OldProtect
// from the image registry (same img-membership check/convention this
// floor's own NtQueryVirtualMemory already uses for its `AllocProtect`
// answer, FEX-MAP.md C4-32 Measure 2: "unconditionally reports AllocProtect
// =PAGE_EXECUTE_READWRITE for the WHOLE registered image span") -- inside
// a registered image: PAGE_EXECUTE_READWRITE (0x40, matches donor: this
// floor's flat-arena model has no real W^X, every image page already
// answers EXECUTE_READWRITE everywhere else); outside any image (plain
// arena/heap): PAGE_READWRITE (0x04), unchanged.
// =====================================================================
long NtProtectVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                             unsigned long NewProtect, unsigned long *OldProtect) {
    // CHALICE C4-53: capture the guest TEB as the ABSOLUTE FIRST statement,
    // same x18-clobber-safety idiom C4-48 established for NtMapViewOfSection
    // (every callee below is plain aarch64-elf C, free to clobber x18).
    u64b nt_c453_teb; __asm__ volatile("mov %0, x18" : "=r"(nt_c453_teb));

    // CHALICE C4-60: unconditional raw-argument receipt, printed BEFORE the
    // null/zero-size early return below and BEFORE nt_wow64_maybe_notify_protect
    // is ever reached -- unlike that function's own "HandleMemoryProtectionNotification"
    // print (which only fires once base/size are both proven nonzero), this
    // fires on EVERY NtProtectVirtualMemory call, so a call that early-returns
    // STATUS_INVALID_PARAMETER (null BaseAddress/RegionSize or zero *RegionSize)
    // is still visible in the transcript. Measurement-only, no control-flow change.
    puts("CHALICE C4-60: NtProtectVirtualMemory BaseAddress_ptr="); puthex((u64b)BaseAddress);
    puts(" *BaseAddress="); puthex(BaseAddress ? (u64b)*BaseAddress : 0xFFFFFFFFFFFFFFFFULL);
    puts(" RegionSize_ptr="); puthex((u64b)RegionSize);
    puts(" *RegionSize="); puthex(RegionSize ? (u64b)*RegionSize : 0xFFFFFFFFFFFFFFFFULL);
    puts(" NewProtect="); puthex((u64b)NewProtect); puts("\n");

    (void)ProcessHandle;
    if (!BaseAddress || !*BaseAddress || !RegionSize || *RegionSize == 0) return STATUS_INVALID_PARAMETER;
    if (OldProtect) {
        NtImageEntry *old_img = nt_image_find_containing((u64b)*BaseAddress);
        *OldProtect = old_img ? 0x40 /* PAGE_EXECUTE_READWRITE */ : 0x04 /* PAGE_READWRITE */;
    }

    nt_wow64_maybe_notify_protect((u64b)*BaseAddress, (u64b)*RegionSize, NewProtect, nt_c453_teb);
    return STATUS_SUCCESS;
}

// =====================================================================
// NtQueryVirtualMemory -- MemoryBasicInformation only. Answers from the
// image registry (mapped DLL/exe views) + one sane free region for unknown
// addresses.
//
// CHALICE C3c-2m fix: the offsets below were WRONG -- padded a spurious
// 4B gap after BOTH Protect and Type as if each DWORD field were followed
// by its own alignment pad. Real Win64 MEMORY_BASIC_INFORMATION
// (winternl.h) packs State/Protect/Type as three CONSECUTIVE DWORDs (only
// ONE pad exists in the whole struct, between AllocationProtect and
// RegionSize, to 8-align RegionSize/SIZE_T): BaseAddress@0(8),
// AllocationBase@8(8), AllocationProtect@0x10(4), <4B pad>,
// RegionSize@0x18(8), State@0x20(4), Protect@0x24(4), Type@0x28(4), <4B
// trailing pad to 8-align the struct> -- total size 0x30 (48), NOT 0x38.
// Proved live this atom: the real Wine ntdll guest's OWN build_ntdll_module
// bootstrap call (donor wine/dlls/ntdll/loader.c) passes
// MemoryInformationLength=0x30 for exactly this call -- our old `< 0x38`
// check rejected it with STATUS_INFO_LENGTH_MISMATCH on EVERY boot, so
// meminfo.AllocationBase (the guest's own ntdll base) was NEVER filled in,
// derailing every self-referential export lookup downstream (incl.
// KERNEL32.dll's import of ntdll.dll.__wine_dbg_header). CHALICE PROBE
// (this atom, ntboot.c) independently confirmed our OWN NtQueryVirtualMemory
// on the same address returns the CORRECT AllocationBase when the call is
// allowed to succeed -- the bug was purely the struct-layout/size
// mismatch, not the image-registry lookup.
// =====================================================================
#define MBI_SIZE               0x30
#define MBI_BASEADDRESS_OFF    0x00
#define MBI_ALLOCATIONBASE_OFF 0x08
#define MBI_ALLOCPROTECT_OFF   0x10
#define MBI_REGIONSIZE_OFF     0x18
#define MBI_STATE_OFF          0x20
#define MBI_PROTECT_OFF        0x24
#define MBI_TYPE_OFF           0x28

// CHALICE C4-41: WOW64 32-bit callers (e.g. syswow64/ntdll.dll's
// loader_init -> NtQueryVirtualMemory(-1, LdrInitializeThunk,
// MemoryBasicInformation, buf, 0x1c, NULL), disasm-confirmed this atom)
// pass MEMORY_BASIC_INFORMATION32 (wine/include/winternl.h: seven
// consecutive 4-byte fields, NO padding -- unlike the 64-bit struct's
// 8-align pads) and Length==0x1c(28), not 0x30(48). Our old fixed
// MBI_SIZE/wr64 write shape rejected every WOW64 caller with
// STATUS_INFO_LENGTH_MISMATCH and NEVER touched the buffer; loader_init
// does not check the return status, reads AllocationBase out of
// (uninitialized-zero) stack memory regardless -- feeding a NULL module
// base into alloc_module -> RtlImageNtHeader(NULL) -> the 7th-fault NULL
// 'MZ' check C4-40 named. Fix: detect WOW64 (nt_wow64_get_peb32() != 0,
// set once at process boot in ntboot.c before loader_init ever runs) and
// serve the donor-exact 28-byte 32-bit layout/size instead.
#define MBI32_SIZE               0x1c
#define MBI32_BASEADDRESS_OFF    0x00
#define MBI32_ALLOCATIONBASE_OFF 0x04
#define MBI32_ALLOCPROTECT_OFF   0x08
#define MBI32_REGIONSIZE_OFF     0x0C
#define MBI32_STATE_OFF          0x10
#define MBI32_PROTECT_OFF        0x14
#define MBI32_TYPE_OFF           0x18

long NtQueryVirtualMemory(void *ProcessHandle, void *BaseAddress, unsigned long MemoryInformationClass,
                           void *MemoryInformation, unsigned long MemoryInformationLength,
                           unsigned long *ReturnLength) {
    (void)ProcessHandle;
    // CHALICE C3c-2m: log EVERY call including ones the early-return checks
    // below would otherwise swallow silently (e.g. a non-MemoryBasicInformation
    // class -- the guest's OWN runtime call during pe7 hits exactly this).
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQueryVirtualMemory ENTRY base="); puthex((u64b)BaseAddress);
        puts(" class="); puthex((u64b)MemoryInformationClass);
        puts(" buf="); puthex((u64b)MemoryInformation);
        puts(" len="); puthex((u64b)MemoryInformationLength);
        puts("\n");
    }
    // CHALICE C4-49: MemorySectionName (class 2 / MemoryMappedFilenameInformation)
    // -- this floor previously fell straight to the generic
    // "class!=0 -> STATUS_INVALID_INFO_CLASS" reject below for EVERY other
    // class, including this one. Live receipt (scratch/c4-48_pe10_fex_receipt_full.log,
    // scratch/c4-49_pe10_fex_receipt_full.log) proved FEX::Windows::
    // GetSectionFilePath (FEX/Source/Windows/Common/Module.h) calls exactly
    // this class/addr pair (class=0x2, len=0x218 == sizeof(MEMORY_SECTION_NAME)
    // (16B) + array<WCHAR,PATH_MAX>(260*2=520B)) right before every
    // BTCpuNotifyMapViewOfSection/HandleImageMap invocation -- and always got
    // STATUS_INVALID_INFO_CLASS back (unimplemented), forcing GetSectionFilePath
    // to return {} (empty path/name) on every call, silently, no crash. C4-49's
    // own re-armed XIntervals dumper then proved, empirically (not inferred):
    // XIntervals NEVER gains kernel32.dll's or kernelbase.dll's ranges despite
    // BOTH notify calls completing rc=0x0 -- confirming this empty-path gap is
    // load-bearing for FEX's real (donor-vendored, Hangover-11.9-adjacent)
    // HandleImageMap body (even though the SOURCE this floor vendors,
    // FEX/Source/Windows/WOW64/Module.cpp:174-179, reads only Address for the
    // interval-tree insert itself -- the ACTUAL shipped libwow64fex.dll blob is
    // a separate prebuilt Hangover-11.9 binary, not built from this source
    // tree, and evidently needs more). Answers honestly from this floor's own
    // image registry (nt_image_find_containing), donor-exact shape/semantics
    // (wine/dlls/ntdll/unix/virtual.c:6079-6104 get_memory_section_name):
    // SectionFileName.Buffer points at the caller's OWN buffer tail (info+1,
    // i.e. MemoryInformation+16 -- FEX's own PathW array sits exactly there),
    // Length/MaximumLength in bytes (Length excludes, MaximumLength includes,
    // the trailing NUL), NUL-terminated. Path synthesized as a real NT-style
    // path ("C:\windows\" + the registry's own registered name, '/' -> '\\')
    // so FEX's BaseName() (Common/Module.h) still extracts the correct real
    // basename (e.g. "kernelbase.dll") regardless of the synthetic prefix.
    if (MemoryInformationClass == 2 /* MemorySectionName */) {
        if (!MemoryInformation) return STATUS_INVALID_PARAMETER;
        if (MemoryInformationLength < 16) return STATUS_INFO_LENGTH_MISMATCH;
        u64b addr2 = (u64b)BaseAddress;
        NtImageEntry *img2 = nt_image_find_containing(addr2);
        if (!img2) return STATUS_INVALID_PARAMETER;
        char path[128]; u64b pi = 0;
        const char *prefix = "C:\\windows\\";
        for (; prefix[pi]; pi++) path[pi] = prefix[pi];
        for (u64b j = 0; img2->name[j] && pi < 126; j++, pi++)
            path[pi] = (img2->name[j] == '/') ? '\\' : img2->name[j];
        path[pi] = 0;
        u64b name_chars = pi;
        u64b avail_for_name = MemoryInformationLength - 16; // bytes after the UNICODE_STRING header
        u64b copy_chars = name_chars;
        int truncated = 0;
        if (copy_chars * 2 + 2 > avail_for_name) {
            copy_chars = avail_for_name >= 2 ? (avail_for_name - 2) / 2 : 0;
            truncated = 1;
        }
        u8 *out2 = (u8 *)MemoryInformation;
        wr16(out2 + 0, (u16b)(copy_chars * 2));       // Length (bytes, excl NUL)
        wr16(out2 + 2, (u16b)(copy_chars * 2 + 2));   // MaximumLength (incl NUL)
        wr64(out2 + 8, (u64b)(out2 + 16));            // Buffer -> caller's own buffer tail (info+1)
        for (u64b k = 0; k < copy_chars; k++) wr16(out2 + 16 + k * 2, (u16b)(unsigned char)path[k]);
        wr16(out2 + 16 + copy_chars * 2, 0);          // NUL terminator
        if (ReturnLength) *ReturnLength = 16 + name_chars * 2 + 2;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE C4-49: NtQueryVirtualMemory MemorySectionName addr="); puthex(addr2);
            puts(" name="); puts(path); puts(truncated ? " (truncated)\n" : "\n");
        }
        return truncated ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
    }
    // CHALICE C4-41 (2nd pass): nt_wow64_get_peb32()!=0 is a PROCESS-wide
    // "this process is running a wow64 32-bit thread" flag (set once at
    // process boot, ntboot.c), NOT a per-call bitness tag -- ntboot.c's OWN
    // internal build_ntdll_module-mirroring probe (nt_boot_process,
    // "CHALICE PROBE") calls this SAME NtQueryVirtualMemory directly
    // (ProcessHandle=0 sentinel, host-side helper, not a guest syscall)
    // with a real 0x30-byte 64-bit buffer even while wow64 is already wired
    // for an earlier-booted module -- gating on the flag alone regressed
    // that caller into a 20-byte OOB-adjacent host stack fault (elr landed
    // in pe_find_named_export with a garbage module-base arg, this atom's
    // receipt). The actual, donor-accurate discriminator NT itself uses is
    // the CALLER's OWN buffer size (32-bit callers allocate exactly
    // sizeof(MEMORY_BASIC_INFORMATION32)==0x1c on their stack, disasm-
    // confirmed this atom; this host-side probe deliberately over-allocates
    // 0x30) -- so use32 only when wow64 AND the caller didn't pass a full
    // 64-bit-sized buffer.
    int wow64 = nt_wow64_get_peb32() != 0;
    int use32 = wow64 && MemoryInformationLength < MBI_SIZE;
    u64b mbi_min = use32 ? MBI32_SIZE : MBI_SIZE;
    if (MemoryInformationLength < mbi_min) return STATUS_INFO_LENGTH_MISMATCH;

    // CHALICE C4-41: all branches below still write the 64-bit MBI_* shape
    // (single source of truth for field VALUES) into a local staging
    // buffer, NEVER directly into the caller's `out` -- a WOW64 caller's
    // buffer is only guaranteed MBI32_SIZE(0x1c) bytes (its own stack
    // slot), and the 64-bit shape spans up to 0x2C: writing it straight
    // into `out` would overrun a 32-bit caller's stack by 20 bytes.
    u8 stage[MBI_SIZE];
    for (u64b i = 0; i < MBI_SIZE; i++) stage[i] = 0;
    u8 *out = stage;

    u64b addr = (u64b)BaseAddress;
    // FEX's finite VirtualQuery address-space walk terminates when the
    // address reaches this floor's real modeled user-VA ceiling.
    if (addr >= NT_ARENA_LIMIT) return STATUS_INVALID_PARAMETER;

    NtImageEntry *img = nt_image_find_containing(addr);
    u64b arena_base = 0, arena_size = 0; int arena_committed = -1;
    u64b vm_base = 0, vm_size = 0;
    int kuser_hit = addr >= NT_KUSER_SHARED_DATA_ADDR && addr < NT_KUSER_SHARED_DATA_ADDR + NT_PAGE_SIZE;
    int arena_hit = !img && !kuser_hit && nt_arena_query(addr, &arena_base, &arena_size, &arena_committed);
    int vm_hit = !img && !kuser_hit && !arena_hit && nt_vm_region_query(addr, &vm_base, &vm_size);
    if (img) {
        wr64(out + MBI_BASEADDRESS_OFF, img->base);
        wr64(out + MBI_ALLOCATIONBASE_OFF, img->base);
        wr32(out + MBI_ALLOCPROTECT_OFF, PAGE_EXECUTE_READWRITE);
        wr64(out + MBI_REGIONSIZE_OFF, img->size - (addr - img->base));
        wr32(out + MBI_STATE_OFF, MEM_COMMIT);
        wr32(out + MBI_PROTECT_OFF, PAGE_EXECUTE_READWRITE);
        wr32(out + MBI_TYPE_OFF, MEM_IMAGE);
    } else if (kuser_hit) {
        wr64(out + MBI_BASEADDRESS_OFF, NT_KUSER_SHARED_DATA_ADDR);
        wr64(out + MBI_ALLOCATIONBASE_OFF, NT_KUSER_SHARED_DATA_ADDR);
        wr32(out + MBI_ALLOCPROTECT_OFF, PAGE_READWRITE);
        wr64(out + MBI_REGIONSIZE_OFF, NT_PAGE_SIZE);
        wr32(out + MBI_STATE_OFF, MEM_COMMIT);
        wr32(out + MBI_PROTECT_OFF, PAGE_READWRITE);
        wr32(out + MBI_TYPE_OFF, MEM_PRIVATE);
    } else if (arena_hit) {
        // CHALICE C4-9 (monad ruling #5): report State honestly per
        // accounting -- MEM_FREE (no reservation covers addr, sentinel -1),
        // MEM_RESERVE (reservation exists, containing 2MB window not yet
        // committed, 0), or MEM_COMMIT (window backed, 1). AllocationBase is
        // the reservation's own base (0 for the MEM_FREE-inside-arena case,
        // matching the pre-existing outside-arena free-region convention
        // just below, which also uses AllocationBase=0).
        wr64(out + MBI_BASEADDRESS_OFF, arena_base);
        wr64(out + MBI_ALLOCATIONBASE_OFF, arena_committed == -1 ? 0 : arena_base);
        wr32(out + MBI_ALLOCPROTECT_OFF, arena_committed == -1 ? 0 : PAGE_READWRITE);
        wr64(out + MBI_REGIONSIZE_OFF, arena_size);
        wr32(out + MBI_STATE_OFF, arena_committed == -1 ? MEM_FREE : (arena_committed ? MEM_COMMIT : MEM_RESERVE));
        wr32(out + MBI_PROTECT_OFF, arena_committed == 1 ? PAGE_READWRITE : PAGE_NOACCESS);
        wr32(out + MBI_TYPE_OFF, arena_committed == -1 ? 0 : MEM_PRIVATE);
    } else if (vm_hit) {
        wr64(out + MBI_BASEADDRESS_OFF, vm_base);
        wr64(out + MBI_ALLOCATIONBASE_OFF, vm_base);
        wr32(out + MBI_ALLOCPROTECT_OFF, PAGE_READWRITE);
        wr64(out + MBI_REGIONSIZE_OFF, vm_size);
        wr32(out + MBI_STATE_OFF, MEM_COMMIT);
        wr32(out + MBI_PROTECT_OFF, PAGE_READWRITE);
        wr32(out + MBI_TYPE_OFF, MEM_PRIVATE);
    } else {
        // CHALICE C4-15: VirtualQuery describes a contiguous region, not a
        // page. Coalesce this free gap through the nearest known image,
        // LOW-pool VM allocation, KUSER page, arena start, or VA ceiling.
        // Source consumer: FEX InvalidationTracker.cpp:16-28.
        u64b page = addr & ~(NT_PAGE_SIZE - 1);
        u64b next = page < NT_ARENA_BASE ? NT_ARENA_BASE : NT_ARENA_LIMIT;
        u64b candidate = nt_image_next_base(page);
        if (candidate && candidate < next) next = candidate;
        candidate = nt_vm_region_next(page);
        if (candidate && candidate < next) next = candidate;
        if (NT_KUSER_SHARED_DATA_ADDR > page && NT_KUSER_SHARED_DATA_ADDR < next)
            next = NT_KUSER_SHARED_DATA_ADDR;
        wr64(out + MBI_BASEADDRESS_OFF, page);
        wr64(out + MBI_ALLOCATIONBASE_OFF, 0);
        wr32(out + MBI_ALLOCPROTECT_OFF, 0);
        wr64(out + MBI_REGIONSIZE_OFF, next - page);
        wr32(out + MBI_STATE_OFF, MEM_FREE);
        wr32(out + MBI_PROTECT_OFF, PAGE_NOACCESS);
        wr32(out + MBI_TYPE_OFF, 0);
    }

    // CHALICE C3c-2m: unconditional log of every NtQueryVirtualMemory call
    // (not gated behind the dispatch-log 64-cap -- this atom's probe fires
    // late in a long boot and must not be silently dropped) when
    // nt_dispatch_log_enabled is armed. Reports which branch answered
    // (image-registry hit vs free-region fallback) + the exact
    // AllocationBase/RegionSize/State/Type the guest's own loader would see
    // for this BaseAddress -- this IS what build_ntdll_module's
    // NtQueryVirtualMemory(LdrInitializeThunk) call reads to learn its own
    // module base (donor: wine/dlls/ntdll/loader.c build_ntdll_module).
    // CHALICE C4-41: re-pack the just-written 64-bit-shaped fields into the
    // donor-exact 32-bit layout for WOW64 callers -- every branch above
    // still writes via the wr64/wr32 MBI_* (64-bit) offsets unconditionally
    // (single source of truth for the field VALUES, no duplicated branch
    // logic); this final step is the only bitness-aware part, matching the
    // CTX32-conversion pattern already used elsewhere in this file (ntsvc.c).
    u8 *realout = (u8 *)MemoryInformation;
    if (use32) {
        u64b base64 = rd64(stage + MBI_BASEADDRESS_OFF);
        u64b allocbase64 = rd64(stage + MBI_ALLOCATIONBASE_OFF);
        u32b allocprot = rd32(stage + MBI_ALLOCPROTECT_OFF);
        u64b regionsize64 = rd64(stage + MBI_REGIONSIZE_OFF);
        u32b state = rd32(stage + MBI_STATE_OFF);
        u32b protect = rd32(stage + MBI_PROTECT_OFF);
        u32b type = rd32(stage + MBI_TYPE_OFF);
        for (u64b i = 0; i < MBI32_SIZE; i++) realout[i] = 0;
        wr32(realout + MBI32_BASEADDRESS_OFF, (u32b)base64);
        wr32(realout + MBI32_ALLOCATIONBASE_OFF, (u32b)allocbase64);
        wr32(realout + MBI32_ALLOCPROTECT_OFF, allocprot);
        wr32(realout + MBI32_REGIONSIZE_OFF, (u32b)regionsize64);
        wr32(realout + MBI32_STATE_OFF, state);
        wr32(realout + MBI32_PROTECT_OFF, protect);
        wr32(realout + MBI32_TYPE_OFF, type);
        if (nt_dispatch_log_enabled) {
            puts("CHALICE C4-41: NtQueryVirtualMemory(wow64) addr="); puthex(addr);
            puts(" branch="); puts(img ? "image" : "fallback");
            puts(" allocbase32="); puthex((u64b)(u32b)allocbase64);
            puts(" regionsize32="); puthex((u64b)(u32b)regionsize64);
            puts(" state="); puthex((u64b)state);
            puts(" type="); puthex((u64b)type); puts("\n");
        }
        if (ReturnLength) *ReturnLength = MBI32_SIZE;
        return STATUS_SUCCESS;
    }

    for (u64b i = 0; i < MBI_SIZE; i++) realout[i] = stage[i];

    if (nt_dispatch_log_enabled) {
        u32b state = (u32b)stage[MBI_STATE_OFF] | ((u32b)stage[MBI_STATE_OFF+1]<<8) |
                     ((u32b)stage[MBI_STATE_OFF+2]<<16) | ((u32b)stage[MBI_STATE_OFF+3]<<24);
        u32b type  = (u32b)stage[MBI_TYPE_OFF]  | ((u32b)stage[MBI_TYPE_OFF+1]<<8) |
                     ((u32b)stage[MBI_TYPE_OFF+2]<<16) | ((u32b)stage[MBI_TYPE_OFF+3]<<24);
        puts("CHALICE: NtQueryVirtualMemory addr="); puthex(addr);
        puts(" branch="); puts(img ? "image" : "fallback");
        puts(" allocbase="); puthex(rd64(stage + MBI_ALLOCATIONBASE_OFF));
        puts(" regionsize="); puthex(rd64(stage + MBI_REGIONSIZE_OFF));
        puts(" state="); puthex((u64b)state);
        puts(" type="); puthex((u64b)type); puts("\n");
    }

    if (ReturnLength) *ReturnLength = MBI_SIZE;
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C3c-2n: NtInitializeNlsFiles -- maps the combined locale/casing/
// codepage NLS data blob (locale.nls) kernelbase's init_locale ultimately
// needs, via ntdll's RtlGetLocaleFileMappingAddress (locale.c:359) ->
// NtInitializeNlsFiles (real Wine syscall id 158/0x9e, wine-arm64-
// syscalls.txt:157). Donor contract (wine/dlls/ntdll/unix/env.c:2243
// NtInitializeNlsFiles): open "<data_dir>/nls/locale.nls", map it
// SEC_COMMIT/PAGE_READONLY (a flat data section, NOT a SEC_IMAGE/PE map),
// write the mapped base through *ptr and system_lcid (env.c:79/813,
// defaults to MAKELANGID(LANG_ENGLISH,SUBLANG_DEFAULT)==0x0409 when no
// locale override is configured) through *lcid. The donor's own `size`
// out-param is DEAD -- read the donor source: the function never writes
// through it (a local `mapsize` absorbs map_section's own size answer
// instead) -- so this mirrors that exactly, leaving
// *DefaultCasingTableSize untouched.
// Our equivalent of "map the file": the same blob-store/FS-fallback lookup
// NtOpenFile (above) and ntboot.c's stock-DLL path already use
// (nt_blob_lookup_or_fs—"locale.nls" lives on the RedSea-spirit disk,
// chalice/stock/ + mkdisk.py populate, same as apisetschema.dll). On an
// FS-fallback hit the returned bytes pointer is ALREADY territory-backed
// (nt_blob_lookup_fs allocates its read buffer via nt_territory_alloc) --
// same "hand the blob pointer straight back, no extra copy" idiom
// NtCreateSection/NtMapViewOfSection already use for fh->blob/sh->blob
// above, and satisfies the "stable address, read for process lifetime,
// never freed" contract RtlGetLocaleFileMappingAddress's caching wrapper
// assumes (it never calls NtUnmapViewOfSection on the success path).
// =====================================================================
long NtInitializeNlsFiles(void **BaseAddress, unsigned long *DefaultLocaleId,
                           void *DefaultCasingTableSize) {
    (void)DefaultCasingTableSize;   // donor leaves this out-param unwritten too (receipted above)
    nt_blob_ensure_init();
    if (!BaseAddress || !DefaultLocaleId) return STATUS_INVALID_PARAMETER;

    const unsigned char *bytes; u64b len; const char *name;
    if (!nt_blob_lookup_or_fs("locale.nls", &bytes, &len, &name)) return STATUS_OBJECT_NAME_NOT_FOUND;

    *BaseAddress = (void *)bytes;
    *DefaultLocaleId = 0x0409UL;   // MAKELANGID(LANG_ENGLISH,SUBLANG_DEFAULT), env.c:813 default

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtInitializeNlsFiles base="); puthex((u64b)bytes);
        puts(" len="); puthex(len); puts(" lcid=0x409\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C3c-2n: NtGetNlsSectionPtr -- the sibling NLS-mapping syscall
// (real Wine id 155/0x9b) kernelbase's own locale init calls RIGHT AFTER
// NtInitializeNlsFiles succeeds: load_sortdefault_nls (donor wine/dlls/
// kernelbase/locale.c:494) does NtGetNlsSectionPtr(NLS_SECTION_SORTKEYS=9,
// 0, NULL, &header, &size) to map sortdefault.nls -- observed live
// (nt_dispatch_log) firing 3x UNIMPLEMENTED immediately before this atom's
// deepest fault (a garbage `header` dereference inside kernelbase, the
// direct consequence of this call always failing). Donor contract (env.c
// get_nls_file_path/get_nls_section_name, both receipted in this file's
// header comment above): open "<dir>/nls/<name>.nls" where name depends on
// `type` (enum nls_section_type, dlls/ntdll/locale_private.h) --
// SORTKEYS(9)->"sortdefault", CASEMAP(10)->"l_intl",
// CODEPAGE(11)->"c_%03u"%id, NORMALIZE(12)->"normnfc"/"normnfd"/
// "normnfkc"/"normnfkd"/"normidna" keyed by NormalizationForm `id` -- map
// SEC_COMMIT/PAGE_READONLY (flat data, not SEC_IMAGE), *ptr=mapped base,
// *size=the mapped size (donor's map_section DOES feed this one back,
// unlike NtInitializeNlsFiles's dead `size` param). Same territory-backed-
// blob-pointer-reuse idiom as NtInitializeNlsFiles above (no extra copy on
// the FS-fallback hit). Only SORTKEYS/CASEMAP are pre-staged on the disk
// (chalice/stock/, fetch-stock-dlls.sh) -- CODEPAGE/NORMALIZE resolve
// through the exact same lookup and will succeed too if their file is ever
// added there; until then they cleanly answer STATUS_OBJECT_NAME_NOT_FOUND
// (same as any other unstaged nt_blob_lookup_or_fs miss, e.g. NtOpenFile).
// =====================================================================
#define NLS_SECTION_SORTKEYS   9
#define NLS_SECTION_CASEMAP   10
#define NLS_SECTION_CODEPAGE  11
#define NLS_SECTION_NORMALIZE 12

static int nt_nls_section_name(u32b type, u32b id, char *out, int outmax) {
    const char *fixed = 0;
    switch (type) {
    case NLS_SECTION_SORTKEYS: fixed = "sortdefault"; break;
    case NLS_SECTION_CASEMAP:  fixed = "l_intl"; break;
    case NLS_SECTION_NORMALIZE:
        switch (id) {
        case 1: fixed = "normnfc"; break;    // NormalizationC
        case 2: fixed = "normnfd"; break;    // NormalizationD
        case 5: fixed = "normnfkc"; break;   // NormalizationKC
        case 6: fixed = "normnfkd"; break;   // NormalizationKD
        case 13: fixed = "normidna"; break;
        default: return 0;
        }
        break;
    case NLS_SECTION_CODEPAGE: {
        // "c_%03u" (min 3 digits, no upper truncation -- real codepage ids
        // like 1252 print in full, matching donor snprintf semantics).
        char digits[12]; int nd = 0;
        u32b v = id;
        do { digits[nd++] = (char)('0' + (v % 10)); v /= 10; } while (v);
        while (nd < 3) digits[nd++] = '0';   // zero-pad up to 3
        int i = 0;
        out[i++] = 'c'; out[i++] = '_';
        for (int k = nd - 1; k >= 0 && i < outmax - 1; k--) out[i++] = digits[k];
        out[i] = 0;
        return 1;
    }
    default: return 0;
    }
    int i = 0;
    for (; fixed[i] && i < outmax - 1; i++) out[i] = fixed[i];
    out[i] = 0;
    return 1;
}

long NtGetNlsSectionPtr(unsigned long Type, unsigned long Id, void *Unknown,
                         void **BaseAddress, u64b *Size) {
    (void)Unknown;
    nt_blob_ensure_init();
    if (!BaseAddress) return STATUS_INVALID_PARAMETER;

    char base[24];
    if (!nt_nls_section_name((u32b)Type, (u32b)Id, base, sizeof(base)))
        return STATUS_INVALID_PARAMETER;
    char name[NT_PATH_MAX];
    int i = 0;
    for (; base[i] && i < NT_PATH_MAX - 5; i++) name[i] = base[i];
    name[i++] = '.'; name[i++] = 'n'; name[i++] = 'l'; name[i++] = 's'; name[i] = 0;

    const unsigned char *bytes; u64b len; const char *manifest_name;
    if (!nt_blob_lookup_or_fs(name, &bytes, &len, &manifest_name)) return STATUS_OBJECT_NAME_NOT_FOUND;

    *BaseAddress = (void *)bytes;
    if (Size) *Size = len;

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtGetNlsSectionPtr type="); puthex(Type);
        puts(" id="); puthex(Id); puts(" name="); puts(name);
        puts(" base="); puthex((u64b)bytes); puts(" len="); puthex(len); puts("\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C3c-2o: minimal in-memory registry floor.
//
// Donor contract (dlls/kernelbase/locale.c, wine/ gitignored -- quoted per
// HANDOFF's FRAMING NOTE, this repo's summon text has the exact lines):
// init_locale() does RegCreateKeyExW(HKEY_CURRENT_USER,
// "Control Panel\\International", ..., &intl_key) then, if that succeeds,
// RegQueryValueExW(intl_key, "LocaleName", ..., bufferW, &count) and only
// reads bufferW when RegQueryValueExW reports success; NlsValidateLocale()
// separately walks the same key's subkeys and "Sorting\\Ids". None of
// this data exists on this floor -- the fix is to make every leg of that
// probe resolve the way the real registry resolves on a box with no user
// locale override configured: key-open succeeds (callers of
// RegCreateKeyExW/RegOpenKeyExW expect a usable key back), but every
// value read comes back NOT-FOUND so callers take their own documented
// empty-registry fallback path (NLS defaults) instead of trusting an
// uninitialized output buffer. No hive, no persistence -- every key is
// the same interchangeable dummy handle, every value is permanently
// absent, exactly enough surface for stock kernelbase's init path to fall
// through cleanly.
// =====================================================================

// CHALICE C3c-2o diagnostic: decode a UNICODE_STRING* (ValueName) the same
// way NtOpenFile decodes ObjectAttributes->ObjectName (US_LENGTH_OFF/
// US_BUFFER_OFF, this file) -- names the value/key being probed in the
// log so a live trace can confirm which locale-override paths kernelbase
// asks for (e.g. "LocaleName", "Control Panel\International") instead of
// just counting anonymous hits.
static void nt_log_unicode_string(const void *us) {
    if (!us) { puts("(null)"); return; }
    const u8 *p = (const u8 *)us;
    u16b wlen_bytes = *(const u16b *)(p + US_LENGTH_OFF);
    const u16b *wbuf = *(const u16b **)(p + US_BUFFER_OFF);
    if (!wbuf || wlen_bytes == 0) { puts("(empty)"); return; }
    u32b nchars = wlen_bytes / 2;
    if (nchars > 63) nchars = 63;
    char buf[64];
    for (u32b i = 0; i < nchars; i++) buf[i] = (char)wbuf[i];
    buf[nchars] = 0;
    puts(buf);
}

// Decode ObjectAttributes->ObjectName (OA_OBJECTNAME_OFF, this file).
static void nt_log_object_attributes_name(const void *oa) {
    if (!oa) { puts("(null-OA)"); return; }
    const u8 *p = (const u8 *)oa;
    nt_log_unicode_string(*(void **)(p + OA_OBJECTNAME_OFF));
}

long NtCreateKey(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                  unsigned long TitleIndex, void *Class, unsigned long CreateOptions,
                  unsigned long *Disposition) {
    (void)DesiredAccess; (void)ObjectAttributes; (void)TitleIndex; (void)Class; (void)CreateOptions;
    if (!KeyHandle) return STATUS_INVALID_PARAMETER;
    // FIXED pseudo-handle (NT_REGISTRY_KEY_HANDLE, ntids.h), same idiom as
    // the pre-existing NT_STDOUT_HANDLE singleton constant -- deliberately
    // NOT allocated from ntfile.c's shared {file,section} handle table
    // (NT_HANDLE_MAX=32). First cut used that table and it broke: the
    // DLL-load-heavy boot path (kernel32+kernelbase+apisetschema, each
    // several NtOpenFile/NtCreateSection calls) exhausts all 32 slots
    // before locale-init's NtCreateKey ever runs, so it returned
    // STATUS_NO_MEMORY (confirmed live: STATUS_NO_MEMORY/0xC0000017 sitting
    // on the fault-time stack backtrace) -- a DIFFERENT failure that still
    // left kernelbase's registry-key local in a state its init_locale
    // couldn't cleanly recover from. A fixed sentinel needs no table slot,
    // never exhausts, and every key is interchangeable anyway (no hive).
    *KeyHandle = (void *)NT_REGISTRY_KEY_HANDLE;
    if (Disposition) *Disposition = 2;   // REG_CREATED_NEW_KEY (winreg.h)
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtCreateKey name="); nt_log_object_attributes_name(ObjectAttributes);
        puts(" -> dummy handle\n");
    }
    return STATUS_SUCCESS;
}

// CHALICE C4-6: exact wide-string match of an ObjectAttributes' own
// ObjectName against an ASCII literal (case-insensitive, exact length --
// no prefix/substring match) -- same UNICODE_STRING decode NtOpenFile
// (above) already does, reused here so ONE registry key (the WOW64
// CPU-backend selector, see ntids.h NT_REGISTRY_WOW64CPU_KEY_HANDLE) can
// be special-cased without disturbing the "no hive, everything else
// NOT_FOUND" contract for every other key.
static int nt_oa_name_matches_ascii(const void *oa, const char *ascii) {
    if (!oa) return 0;
    const u8 *p = (const u8 *)oa;
    const u8 *us = *(u8 **)(p + OA_OBJECTNAME_OFF);
    if (!us) return 0;
    u16b wlen_bytes = *(const u16b *)(us + US_LENGTH_OFF);
    const u16b *wbuf = *(const u16b **)(us + US_BUFFER_OFF);
    if (!wbuf) return 0;
    u32b nchars = wlen_bytes / 2;
    u32b i = 0;
    for (; ascii[i]; i++) {
        if (i >= nchars) return 0;
        char wc = (char)wbuf[i];
        char ac = ascii[i];
        if (wc >= 'A' && wc <= 'Z') wc += 'a' - 'A';
        if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
        if (wc != ac) return 0;
    }
    return i == nchars;
}

// CHALICE C4-14: RootDirectory-aware registry-key match -- see
// NT_REGISTRY_HKLM_ROOT_HANDLE (ntids.h) for the full donor-grounded
// rationale (real RegOpenKeyExA/W opens "\Registry\Machine" ONCE, caches
// the handle, then opens every subkey RELATIVE to it in a second call).
// `absolute_ascii` is always the FULL "\Registry\Machine\..." string (one
// literal per call site, unchanged from before this atom); this wrapper
// picks whether to compare ObjectName against the full string (bare/
// RootDirectory-less callers, e.g. this floor's own C4-4/C4-13 direct-
// NtOpenKey selftests) or just the tail after the "\Registry\Machine\"
// prefix (RootDirectory==NT_REGISTRY_HKLM_ROOT_HANDLE callers, e.g. any
// real stock RegOpenKeyExA-shaped caller like FEX's CPUFeaturesFromRegistry).
#define NT_HKLM_ROOT_PREFIX_LEN 18   // strlen("\Registry\Machine\") -- literal, verified below

static int nt_oa_key_matches_absolute(const void *oa, const char *absolute_ascii) {
    if (!oa) return 0;
    const u8 *p = (const u8 *)oa;
    void *root = *(void **)(p + OA_ROOTDIRECTORY_OFF);
    if (root == (void *)NT_REGISTRY_HKLM_ROOT_HANDLE)
        return nt_oa_name_matches_ascii(oa, absolute_ascii + NT_HKLM_ROOT_PREFIX_LEN);
    return nt_oa_name_matches_ascii(oa, absolute_ascii);
}

// CHALICE C4-13: match a UNICODE_STRING* (ValueName -- NOT an ObjectAttributes
// wrapper, unlike nt_oa_name_matches_ascii above) case-SENSITIVE, exact
// length -- box64's RegQueryValueExA always passes a literal ASCII value
// name ("CP 4030"/"CP 4039"/"CP 403A", exact casing including the space
// and uppercase hex letter), so no case-fold is needed or wanted here.
static int nt_us_name_matches_ascii(const void *us, const char *ascii) {
    if (!us) return 0;
    const u8 *p = (const u8 *)us;
    u16b wlen_bytes = *(const u16b *)(p + US_LENGTH_OFF);
    const u16b *wbuf = *(const u16b **)(p + US_BUFFER_OFF);
    if (!wbuf) return 0;
    u32b nchars = wlen_bytes / 2;
    u32b i = 0;
    for (; ascii[i]; i++) {
        if (i >= nchars) return 0;
        if ((char)wbuf[i] != ascii[i]) return 0;
    }
    return i == nchars;
}

long NtOpenKey(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes) {
    (void)DesiredAccess;
    // CHALICE C4-14: the bare "\Registry\Machine" ROOT key -- real
    // RegOpenKeyExA/W's get_special_root_hkey opens this ONCE (RootDirectory
    // NULL, ObjectName absolute) and caches the handle for every subsequent
    // RootDirectory-relative subkey open below. See ntids.h
    // NT_REGISTRY_HKLM_ROOT_HANDLE receipt.
    if (nt_oa_name_matches_ascii(ObjectAttributes, "\\Registry\\Machine")) {
        if (KeyHandle) *KeyHandle = (void *)NT_REGISTRY_HKLM_ROOT_HANDLE;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtOpenKey name="); nt_log_object_attributes_name(ObjectAttributes);
            puts(" -> hklm-root dummy handle\n");
        }
        return STATUS_SUCCESS;
    }
    // CHALICE C4-6: the ONE registry key wow64.dll's get_cpu_dll_name()
    // (donor dlls/wow64/syscall.c:919-936) opens to pick a non-Microsoft
    // CPU-backend replacement -- see ntids.h receipt. Everything else
    // below is the pre-existing "no hive" NOT_FOUND floor, unchanged.
    // CHALICE C4-14: nt_oa_key_matches_absolute (not the bare _ascii
    // matcher) -- also matches a RootDirectory-relative open against the
    // same literal's tail, see receipt above the helper.
    if (nt_oa_key_matches_absolute(ObjectAttributes, "\\Registry\\Machine\\Software\\Microsoft\\Wow64\\x86")) {
        if (KeyHandle) *KeyHandle = (void *)NT_REGISTRY_WOW64CPU_KEY_HANDLE;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtOpenKey name="); nt_log_object_attributes_name(ObjectAttributes);
            puts(" -> wow64-cpu-backend dummy handle\n");
        }
        return STATUS_SUCCESS;
    }
    // CHALICE C4-13: the CentralProcessor\0 key -- box64-for-WOW64's
    // DetectHostCpuFeatures (hostext_wine.c) RegOpenKeyExA's this key then
    // reads CP 4030/4039/403A off it (NtQueryValueKey branch below).
    // CHALICE C4-14: same RootDirectory-relative match upgrade -- this is
    // the exact key FEX's own CPUFeaturesFromRegistry (a stock
    // RootDirectory-relative caller, root cause this leg) opens too.
    if (nt_oa_key_matches_absolute(ObjectAttributes, "\\Registry\\Machine\\Hardware\\Description\\System\\CentralProcessor\\0")) {
        if (KeyHandle) *KeyHandle = (void *)NT_REGISTRY_CENTRALPROCESSOR_KEY_HANDLE;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtOpenKey name="); nt_log_object_attributes_name(ObjectAttributes);
            puts(" -> centralprocessor dummy handle\n");
        }
        return STATUS_SUCCESS;
    }
    // No key exists to open (no hive) -- NOT_FOUND lets RegOpenKeyExW-style
    // callers take their own "key absent" fallback. If a future trace shows
    // a boot path that needs NtOpenKey to succeed, switch this one call
    // site to the same fixed-handle answer NtCreateKey uses.
    if (KeyHandle) *KeyHandle = 0;
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtOpenKey name="); nt_log_object_attributes_name(ObjectAttributes);
        puts(" -> NOT_FOUND\n");
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

// CHALICE C4-14: NtOpenKeyEx -- real Wine ARM64 4-arg cousin of NtOpenKey
// (donor: dlls/ntdll/unix/registry.c NtOpenKeyEx(KeyHandle, DesiredAccess,
// ObjectAttributes, OpenOptions); dlls/kernelbase/registry.c:300-316's own
// open_key wrapper -- what RegOpenKeyExA/W actually call, NOT plain
// NtOpenKey -- uses this exact syscall). Root cause this leg: FEX's own
// FEX::Windows::CPUFeatures::FetchHostFeatures (libwow64fex.dll, RE'd via
// llvm-objdump -- symbol table intact, unstripped) opens
// "Hardware\Description\System\CentralProcessor\0" through a real
// RegOpenKeyExA-shaped call (HKEY_LOCAL_MACHINE/KEY_READ/&result 4-register
// signature, x8 fmt string "Hardware\Description\System\CentralProcessor\{}"
// @ RVA 0x320614) -- NOT the direct NtOpenKey path this floor's own C4-4/
// C4-13 selftests use -- so it fell straight through to STATUS_NOT_
// IMPLEMENTED (svc id=176), core index 0 came back null, and
// FEXCore::Assert::ForcedAssert() (libwow64fex.dll+0x1545DC, a deliberate
// `hlt #1; brk #1` trap) fired with LogMan message "Couldn't detect CPU
// features" (string RVA 0x3205e6, called via LogMan::Msg::MFmtImpl from the
// helper at +0x165F14, lr resolves exactly to the observed fault's own
// lr=+0x165D14 return site) -- same ATOM A KUSER/registry floor leg-13 built
// for wowbox64's DIFFERENT (raw-NtOpenKey) access pattern, now also reached
// via the REAL Windows call shape a stock/unmodified backend actually uses.
// OpenOptions (4th arg) is a real NT param (REG_OPTION_* flags, e.g.
// OPEN_LINK) this floor's flat, hive-less registry floor has no use for --
// dropped, exact same behavior as NtOpenKey otherwise (same match table,
// same fixed pseudo-handles, same NOT_FOUND fallback).
long NtOpenKeyEx(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes, unsigned long OpenOptions) {
    (void)OpenOptions;
    return NtOpenKey(KeyHandle, DesiredAccess, ObjectAttributes);
}

long NtQueryValueKey(void *KeyHandle, void *ValueName, unsigned long KeyValueInformationClass,
                      void *KeyValueInformation, unsigned long Length, unsigned long *ResultLength) {
    // FEX's RegGetValueA bridge calls exactly KeyValuePartialInformation
    // (FEX/Source/Windows/Common/WinAPI/Misc.cpp); never pretend a partial
    // information buffer satisfies an unrelated KEY_VALUE_INFORMATION_CLASS.
    if (KeyValueInformationClass != KEY_VALUE_PARTIAL_INFORMATION) {
        if (ResultLength) *ResultLength = 0;
        return STATUS_INVALID_INFO_CLASS;
    }
    // CHALICE C4-6: the wow64-cpu-backend key's DEFAULT value (get_cpu_dll_
    // name queries with an empty ValueName, i.e. the key's default value) --
    // answer with our chosen backend name as a REG_SZ, KEY_VALUE_PARTIAL_
    // INFORMATION-shaped (TitleIndex(4)+Type(4)+DataLength(4)+Data[], donor
    // wine/include/winternl.h) so get_cpu_dll_name's own
    // `info->Type == REG_SZ` + `(WCHAR*)info->Data` reads land on real data.
    if (KeyHandle == (void *)NT_REGISTRY_WOW64CPU_KEY_HANDLE) {
        static const char backend_name[] = NT_WOW64_CPU_BACKEND_NAME;
        u32b nchars = (u32b)(sizeof(backend_name) - 1);
        u32b data_bytes = nchars * 2;
        u32b need = 12 + data_bytes;
        if (ResultLength) *ResultLength = need;
        if (KeyValueInformation && Length >= need) {
            u8 *out = (u8 *)KeyValueInformation;
            wr32(out + 0, 0);            // TitleIndex
            wr32(out + 4, 1);             // REG_SZ
            wr32(out + 8, data_bytes);    // DataLength
            u16b *wdata = (u16b *)(out + 12);
            for (u32b i = 0; i < nchars; i++) wdata[i] = (u16b)backend_name[i];
        }
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtQueryValueKey wow64-cpu-backend -> "); puts(backend_name); puts("\n");
        }
        return STATUS_SUCCESS;
    }
    // CHALICE C4-16: CentralProcessor\0's complete FEX CPU-feature contract.
    // "CP xxxx" is Windows' packed sysreg encoding, derived in ntsvc.c:
    // ((op0-2)<<14)|(op1<<11)|(CRn<<7)|(CRm<<3)|op2. Each REG_QWORD is a
    // fresh raw EL1 mrs value: no CPU-model constants or hand-picked bits.
    // Any other value remains absent, which FEX's ReadRegU64 deliberately
    // treats as zero.
    if (KeyHandle == (void *)NT_REGISTRY_CENTRALPROCESSOR_KEY_HANDLE) {
        u64b value = 0;
        int found = 1;
        if      (nt_us_name_matches_ascii(ValueName, "CP 4000")) value = nt_read_midr_el1();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4020")) value = nt_read_id_aa64pfr0();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4021")) value = nt_read_id_aa64pfr1();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4024")) value = nt_read_id_aa64zfr0();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4030")) value = nt_read_id_aa64isar0();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4031")) value = nt_read_id_aa64isar1();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4032")) value = nt_read_id_aa64isar2();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4038")) value = nt_read_id_aa64mmfr0();
        else if (nt_us_name_matches_ascii(ValueName, "CP 4039")) value = nt_read_id_aa64mmfr1();
        else if (nt_us_name_matches_ascii(ValueName, "CP 403A")) value = nt_read_id_aa64mmfr2();
        else if (nt_us_name_matches_ascii(ValueName, "CP 5801")) value = nt_read_ctr_el0();
        else found = 0;
        if (!found) {
            if (ResultLength) *ResultLength = 0;
            if (nt_dispatch_log_enabled) {
                puts("CHALICE: NtQueryValueKey centralprocessor name="); nt_log_unicode_string(ValueName);
                puts(" -> NOT_FOUND\n");
            }
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        u32b need = 12 + 8;   // TitleIndex(4)+Type(4)+DataLength(4)+QWORD data(8)
        if (ResultLength) *ResultLength = need;
        if (KeyValueInformation && Length >= need) {
            u8 *out = (u8 *)KeyValueInformation;
            wr32(out + 0, 0);          // TitleIndex
            wr32(out + 4, (u32b)REG_QWORD);
            wr32(out + 8, 8);          // DataLength
            wr64(out + 12, value);
        }
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtQueryValueKey centralprocessor name="); nt_log_unicode_string(ValueName);
            puts(" -> "); puthex(value); puts("\n");
        }
        return STATUS_SUCCESS;
    }
    // Load-bearing return: STATUS_OBJECT_NAME_NOT_FOUND (not
    // STATUS_NOT_IMPLEMENTED). RegQueryValueExW maps this NTSTATUS to a
    // clean "value absent" Win32 failure, so kernelbase's
    // `if (!RegQueryValueExW(...))` is false and it SKIPS reading its
    // (uninitialized) output buffer -- the wcslen-on-garbage fault's root
    // cause. Never write KeyValueInformation/ResultLength on this path.
    (void)KeyHandle; (void)KeyValueInformation; (void)Length;
    if (ResultLength) *ResultLength = 0;
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQueryValueKey name="); nt_log_unicode_string(ValueName);
        puts(" -> NOT_FOUND\n");
    }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

long NtSetValueKey(void *KeyHandle, void *ValueName, unsigned long TitleIndex,
                    unsigned long Type, void *Data, unsigned long DataSize) {
    // No-op success: init_locale writes Locale/LocaleName back after
    // reading them (locale.c ~1896/1940) -- accept and discard, matches
    // the "no hive, no persistence" contract (nothing downstream on this
    // floor ever reads a value back through NtQueryValueKey, which always
    // answers NOT_FOUND regardless of any prior NtSetValueKey call).
    (void)KeyHandle; (void)TitleIndex; (void)Type; (void)Data; (void)DataSize;
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtSetValueKey name="); nt_log_unicode_string(ValueName);
        puts(" -> SUCCESS (discarded)\n");
    }
    return STATUS_SUCCESS;
}

long NtQueryDefaultLocale(unsigned char UserProfile, unsigned long *DefaultLocaleId) {
    // NtInitializeNlsFiles (C3c-2n) already writes *DefaultLocaleId=0x0409
    // into its own out-param on the SAME call path, but NlsValidateLocale's
    // separate NtQueryDefaultLocale(TRUE, &user_lcid) call (locale.c:1966)
    // was still an unregistered NtStub -- STATUS_NOT_IMPLEMENTED there
    // leaves user_lcid uninitialized on stock kernelbase's stack, a second
    // possible garbage-deref source alongside the registry value read.
    // en-US (0x0409) matches this floor's only staged NLS data set.
    (void)UserProfile;
    if (DefaultLocaleId) *DefaultLocaleId = 0x0409;
    if (nt_dispatch_log_enabled) puts("CHALICE: NtQueryDefaultLocale -> 0x0409\n");
    return STATUS_SUCCESS;
}
