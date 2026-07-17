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

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;
typedef unsigned short u16b;

static void wr16(u8 *p, u16b v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }

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
    // territory is bump-only -- no Free() on a short read; the wasted
    // pages just sit unreclaimed (same simplification as everywhere else
    // in this file), still trivial against the 0.75GB budget.
    if (got <= 0) return 0;

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

// CHALICE C3b Atom 3B: no longer static -- ntboot.c's nt_boot_process
// allocates the Phase-0 thread stack (64K) from the same DLL territory.
unsigned char *nt_territory_alloc(unsigned long size_of_image) {
    // CHALICE C3c-2l Phase B: round the START up to 64KB granularity before
    // handing out this allocation -- every DLL image map (and, for
    // simplicity/uniformity, every other territory consumer: thread stacks,
    // FS buffers) gets a Windows-allocation-granularity-aligned base, same
    // as a real Windows VirtualAlloc/MapViewOfFile guarantee. Territory is
    // ~890MB (0x45000000..0x7ff00000) so the extra rounding waste is cheap.
    nt_dll_territory_next = (nt_dll_territory_next + (NT_DLL_TERRITORY_ALIGN - 1)) & ~(NT_DLL_TERRITORY_ALIGN - 1);
    u64b pages = (size_of_image + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    if (pages == 0) pages = NT_PAGE_SIZE;
    if (nt_dll_territory_next + pages > NT_DLL_TERRITORY_LIMIT) return 0;
    u8 *base = (u8 *)nt_dll_territory_next;
    nt_dll_territory_next += pages;
    for (u64b i = 0; i < pages; i++) base[i] = 0;
    return base;
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
#define OA_OBJECTNAME_OFF 0x10
#define US_LENGTH_OFF     0x00
#define US_BUFFER_OFF     0x08

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
    for (u32b i = 0; i < nchars; i++) if (path[i] == '\\') base = &path[i + 1];

    // CHALICE C3c-2k p2 diagnostic: unconditional (not gated behind the
    // 64-count dispatch-log cap) so a SECOND open of an already-mapped
    // module (e.g. "ntdll.dll" self-open during its own module-list
    // self-registration lookup) is visible even late in a long boot trace.
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtOpenFile "); puts(base); puts("\n");
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

    const unsigned char *bytes; u64b bloblen; const char *manifest_name;
    if (!nt_blob_lookup_or_fs(base, &bytes, &bloblen, &manifest_name)) return STATUS_OBJECT_NAME_NOT_FOUND;

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
// NtMapViewOfSection -- runs PeMapImage (pemod.c) over the section's blob
// at a fresh DLL-territory base, kernel-side DIR64 relocs, NO import
// resolution. Relocs applied kernel-side -> STATUS_SUCCESS (never
// IMAGE_NOT_AT_BASE, per spec). Records an image-registry entry.
// =====================================================================
long NtMapViewOfSection(void *SectionHandle, void *ProcessHandle, void **BaseAddress,
                         unsigned long ZeroBits, unsigned long CommitSize, void *SectionOffset,
                         unsigned long *ViewSize, unsigned long InheritDisposition,
                         unsigned long AllocationType, unsigned long Win32Protect) {
    (void)ProcessHandle; (void)ZeroBits; (void)CommitSize; (void)SectionOffset;
    (void)InheritDisposition; (void)AllocationType; (void)Win32Protect;
    if (!BaseAddress || !ViewSize) return STATUS_INVALID_PARAMETER;

    NtHandleEntry *sh = nt_handle_get(SectionHandle, NTH_SECTION);
    if (!sh) return STATUS_INVALID_HANDLE;

    u64b base = 0, size = 0, entry = 0;
    const char *tag = sh->name ? sh->name : "dll";
    long rc = PeMapImage(sh->blob, (unsigned long)sh->bloblen, tag, nt_territory_alloc, 0,
                          &base, &size, &entry);
    if (rc != 0) return STATUS_INVALID_IMAGE_FORMAT;

    *BaseAddress = (void *)base;
    *ViewSize = (unsigned long)size;
    nt_image_register(sh->name, base, size, entry, 1);
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
#define SII_SIZE                 0x40
#define SII_TRANSFERADDRESS_OFF  0x00
#define SII_MAXSTACKSIZE_OFF     0x10
#define SII_COMMITSTACKSIZE_OFF  0x18
#define SII_SUBSYSTEMTYPE_OFF    0x20
#define SII_MINORSUBSYS_OFF      0x24
#define SII_MAJORSUBSYS_OFF      0x26
#define SII_MAJOROS_OFF          0x28
#define SII_MINOROS_OFF          0x2A
#define SII_IMAGECHAR_OFF        0x2C
#define SII_DLLCHAR_OFF          0x2E
#define SII_MACHINE_OFF          0x30
#define SII_CONTAINSCODE_OFF     0x32
#define SII_LOADERFLAGS_OFF      0x34
#define SII_IMAGEFILESIZE_OFF    0x38
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
// NtProtectVirtualMemory -- flat arena = no page protection; validate args,
// record (image-registry membership check doubles as the "record" -- no
// separate protection state to track, documented deviation per spec),
// return old=PAGE_READWRITE, STATUS_SUCCESS.
// =====================================================================
long NtProtectVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                             unsigned long NewProtect, unsigned long *OldProtect) {
    (void)ProcessHandle; (void)NewProtect;
    if (!BaseAddress || !*BaseAddress || !RegionSize || *RegionSize == 0) return STATUS_INVALID_PARAMETER;
    if (OldProtect) *OldProtect = 0x04;   // PAGE_READWRITE
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
    if (MemoryInformationClass != 0 /* MemoryBasicInformation */) return STATUS_INVALID_INFO_CLASS;
    if (!MemoryInformation) return STATUS_INVALID_PARAMETER;
    if (MemoryInformationLength < MBI_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    u8 *out = (u8 *)MemoryInformation;
    for (int i = 0; i < MBI_SIZE; i++) out[i] = 0;

    u64b addr = (u64b)BaseAddress;
    NtImageEntry *img = nt_image_find_containing(addr);
    if (img) {
        wr64(out + MBI_BASEADDRESS_OFF, img->base);
        wr64(out + MBI_ALLOCATIONBASE_OFF, img->base);
        wr32(out + MBI_ALLOCPROTECT_OFF, PAGE_EXECUTE_READWRITE);
        wr64(out + MBI_REGIONSIZE_OFF, img->size - (addr - img->base));
        wr32(out + MBI_STATE_OFF, MEM_COMMIT);
        wr32(out + MBI_PROTECT_OFF, PAGE_EXECUTE_READWRITE);
        wr32(out + MBI_TYPE_OFF, MEM_IMAGE);
    } else {
        u64b page = addr & ~(NT_PAGE_SIZE - 1);
        wr64(out + MBI_BASEADDRESS_OFF, page);
        wr64(out + MBI_ALLOCATIONBASE_OFF, 0);
        wr32(out + MBI_ALLOCPROTECT_OFF, 0);
        wr64(out + MBI_REGIONSIZE_OFF, NT_PAGE_SIZE);
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
    if (nt_dispatch_log_enabled) {
        u32b state = (u32b)out[MBI_STATE_OFF] | ((u32b)out[MBI_STATE_OFF+1]<<8) |
                     ((u32b)out[MBI_STATE_OFF+2]<<16) | ((u32b)out[MBI_STATE_OFF+3]<<24);
        u32b type  = (u32b)out[MBI_TYPE_OFF]  | ((u32b)out[MBI_TYPE_OFF+1]<<8) |
                     ((u32b)out[MBI_TYPE_OFF+2]<<16) | ((u32b)out[MBI_TYPE_OFF+3]<<24);
        puts("CHALICE: NtQueryVirtualMemory addr="); puthex(addr);
        puts(" branch="); puts(img ? "image" : "fallback");
        puts(" allocbase="); puthex(rd64(out + MBI_ALLOCATIONBASE_OFF));
        puts(" regionsize="); puthex(rd64(out + MBI_REGIONSIZE_OFF));
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
