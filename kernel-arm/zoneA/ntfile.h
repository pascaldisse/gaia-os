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
