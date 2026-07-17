// CHALICE C1 slice 1 — aarch64 PE loader (pemod.c) + embedded test images.
#pragma once

// Embedded by build.sh from kernel-arm/chalice/hello{1,2}.exe (xxd -i style,
// mirrors the zcaot_bin embed step in zcmod.c).
extern const unsigned char pehello1_bin[];
extern const unsigned long pehello1_len;
extern const unsigned char pehello2_bin[];
extern const unsigned long pehello2_len;

// CHALICE C2a slice 2 Atom A: PE test artifact 3 -- exercises NT service
// dispatch (TEB/PEB trapdoors + NtWriteFile/NtClose/NtTerminateProcess).
extern const unsigned char pehello3_bin[];
extern const unsigned long pehello3_len;

// CHALICE C3a Atom 3A: PE test artifact 4 (hello4.exe) + mini.dll -- the
// loader-path/DLL-machinery gate (NtOpenFile -> NtCreateSection ->
// NtMapViewOfSection -> hello4's own export-dir walk -> MiniAdd).
extern const unsigned char pehello4_bin[];
extern const unsigned long pehello4_len;
extern const unsigned char peminidll_bin[];
extern const unsigned long peminidll_len;

// CHALICE C3a Atom 3A: manifest-driven blob store (build.sh generated,
// pehello_bin.c) -- {name, bytes, len} per embedded PE image, looked up by
// ntfile.c's NtOpenFile (case-insensitive basename match). Replaces the
// pattern of hardcoded per-name externs with one generic table.
typedef struct { const char *name; const unsigned char *bytes; unsigned long len; } NtBlobEntry;
#define NT_BLOB_MANIFEST_MAX 8
extern NtBlobEntry nt_blob_manifest[NT_BLOB_MANIFEST_MAX];
extern unsigned long nt_blob_manifest_count;
void nt_blob_manifest_init(void);   // generated in pehello_bin.c, idempotent-by-caller

// Loads img (a PE32+/ARM64 image of len bytes), applies base relocations,
// resolves imports via zc_resolve(), runs the entry point, prints
// "<tag> ret=<N>". Returns the entry's return value, or -1 on any parse/
// relocation/import failure (with an error line already printed).
// use_nt: 0 = slice-1 behavior, call entry() directly (hello1/hello2, no
// TEB needed). 1 = slice-2 Atom A -- build a TEB/PEB floor (ntsvc.c) and
// invoke entry via pe_enter (x18=TEB), with NtTerminateProcess able to
// unwind straight back here via nt_setjmp/nt_longjmp.
long PeRun(const unsigned char *img, unsigned long len, const char *tag, int use_nt);

// CHALICE C3a Atom 3A: generic PE mapper -- PeRun (above) and
// NtMapViewOfSection (ntfile.c) both graduate onto this shared core (header
// parse, section copy, DIR64 relocs; imports only if do_imports). `allocfn`
// supplies the backing memory (PeRun: heap via MAlloc; NtMapViewOfSection:
// the DLL-territory bump allocator, kernel-arm/chalice/SLICE3-SPEC.md Atom
// 3A) -- it must return a ZEROED buffer of >= size_of_image bytes, page-
// aligned (ARM64 PE code's adrp addressing requires it, same receipt as
// slice-1's original MAlloc-align trick). On success (0): *out_base =
// buffer address, *out_size = SizeOfImage, *out_entry = *out_base +
// EntryPointRVA (an absolute address, not yet a callable fn pointer
// without a cast). do_imports=0 path never touches the import directory at
// all (SLICE3-SPEC verdict 4: "PE-side loader does its own imports").
// (kernel side is aarch64-none-elf/LP64: `unsigned long` is already 8 bytes
// here -- no need for `unsigned long long`, and mixing the two just trips
// clang's distinct-type pointer-compatibility warning for no benefit.)
typedef unsigned char *(*PeAllocFn)(unsigned long size_of_image);
long PeMapImage(const unsigned char *img, unsigned long len, const char *tag, PeAllocFn allocfn,
                 int do_imports, unsigned long *out_base, unsigned long *out_size,
                 unsigned long *out_entry);

// CHALICE C3a Atom 3A: export parser (pemod growth). Walks an
// already-mapped image's IMAGE_DIR_EXPORT (name table, ordinal table,
// address table -- standard PE/COFF layout) for `name`. base/size_of_image
// as returned by PeMapImage. *out_addr = absolute function address on
// success. Returns: 0 = found, -1 = not found, -2 = forwarder (function
// RVA lands inside the export directory itself -- unsupported, distinct
// error per spec), -3 = image has no export directory.
long pe_find_named_export(unsigned long base, unsigned long size_of_image,
                           const char *name, unsigned long *out_addr);
