// CHALICE C3a Atom 3A — loader-path NT services (kernel side).
// Blob store + handle table {console(pseudo, ntsvc.c)/file/section} + image
// registry + DLL-territory bump allocator, wired onto pemod.c's PeMapImage.
#pragma once
#include "kernel.h"
#include "pemod.h"
#include "../chalice/ntids.h"

long NtOpenFile(void **FileHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                 void *IoStatusBlock, unsigned long ShareAccess, unsigned long OpenOptions);
long NtReadFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                void *IoStatusBlock, void *Buffer, unsigned long Length,
                void *ByteOffset, unsigned long *Key);
long NtCreateSection(void **SectionHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                      void *MaximumSize, unsigned long SectionPageProtection,
                      unsigned long AllocationAttributes, void *FileHandle);
long NtMapViewOfSection(void *SectionHandle, void *ProcessHandle, void **BaseAddress,
                         unsigned long ZeroBits, unsigned long CommitSize, void *SectionOffset,
                         unsigned long *ViewSize, unsigned long InheritDisposition,
                         unsigned long AllocationType, unsigned long Win32Protect);
long NtProtectVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                             unsigned long NewProtect, unsigned long *OldProtect);
long NtQueryVirtualMemory(void *ProcessHandle, void *BaseAddress, unsigned long MemoryInformationClass,
                           void *MemoryInformation, unsigned long MemoryInformationLength,
                           unsigned long *ReturnLength);

// NtClose (ntsvc.c) also releases handle-table entries allocated here --
// this is the release hook it calls for any handle value >= the table's
// base (values below stay the pre-existing pseudo-handle no-op path, e.g.
// NT_STDOUT_HANDLE).
int nt_file_handle_close(void *handle);

// CHALICE C3b Atom 3B: exposed (were static-internal in Atom 3A) so
// ntboot.c's nt_boot_process can reuse the exact same DLL-territory
// allocator + image registry the loader-path services already use, per
// SLICE3-SPEC's "map ntdll blob + exe blob via the 3A machinery (registry
// entries)" instruction -- no parallel/duplicate mechanism.
void nt_blob_ensure_init(void);
unsigned char *nt_territory_alloc(unsigned long size_of_image);

// CHALICE C3c Atom 3C-1: blob-manifest lookup with FS fallback (stock DLLs
// too big to embed, live on the RedSea-spirit disk instead -- fs.c/
// mkdisk.py). Case-insensitive. On an FS-fallback hit, *out_bytes/*out_name
// are heap-owned (MAlloc, live for the kernel's lifetime like everything
// else this atom allocates -- no free path needed, matching the
// DLL-territory bump allocator's own never-free simplification). Used by
// NtOpenFile (this file) and ntboot.c's nt_boot_process (pe6: stock
// ntdll.dll comes from here).
int nt_blob_lookup_or_fs(const char *basename, const unsigned char **out_bytes, unsigned long *out_len,
                          const char **out_name);
void nt_image_register(const char *name, unsigned long base, unsigned long size,
                        unsigned long entry, int is_dll);

// CHALICE C3c Atom 3C-2a: image-relative resolve for the exception-vector
// fault dump (kmain.c sync_handler). See ntfile.c for contract.
int nt_image_resolve(unsigned long addr, char *name_out, unsigned long namelen,
                      unsigned long *off_out);
