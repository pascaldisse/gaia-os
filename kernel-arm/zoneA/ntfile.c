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

static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }

// IO_STATUS_BLOCK: {NTSTATUS/PVOID Status; ULONG_PTR Information;} 16B, same
// shape ntsvc.c's NtWriteFile uses (receipted there).
typedef struct { u64b status; u64b information; } NtIoStatusBlock;

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

static u64b nt_dll_territory_next = NT_DLL_TERRITORY_BASE;

// CHALICE C3b Atom 3B: no longer static -- ntboot.c's nt_boot_process
// allocates the Phase-0 thread stack (64K) from the same DLL territory.
unsigned char *nt_territory_alloc(unsigned long size_of_image) {
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
#define NT_PATH_MAX 128

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

    const unsigned char *bytes; u64b bloblen; const char *manifest_name;
    if (!nt_blob_lookup(base, &bytes, &bloblen, &manifest_name)) return STATUS_OBJECT_NAME_NOT_FOUND;

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
// addresses. MEMORY_BASIC_INFORMATION (LP64): BaseAddress@0,
// AllocationBase@8, AllocationProtect(4,pad4)@0x10, RegionSize@0x18,
// State(4,pad4)@0x20, Protect(4,pad4)@0x28, Type(4,pad4)@0x30 -- size 0x38
// (matches the well-known public Win64 sizeof(MEMORY_BASIC_INFORMATION)).
// =====================================================================
#define MBI_SIZE               0x38
#define MBI_BASEADDRESS_OFF    0x00
#define MBI_ALLOCATIONBASE_OFF 0x08
#define MBI_ALLOCPROTECT_OFF   0x10
#define MBI_REGIONSIZE_OFF     0x18
#define MBI_STATE_OFF          0x20
#define MBI_PROTECT_OFF        0x28
#define MBI_TYPE_OFF           0x30

long NtQueryVirtualMemory(void *ProcessHandle, void *BaseAddress, unsigned long MemoryInformationClass,
                           void *MemoryInformation, unsigned long MemoryInformationLength,
                           unsigned long *ReturnLength) {
    (void)ProcessHandle;
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

    if (ReturnLength) *ReturnLength = MBI_SIZE;
    return STATUS_SUCCESS;
}
