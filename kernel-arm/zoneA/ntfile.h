// CHALICE C3a Atom 3A — loader-path NT services (kernel side).
// Blob store + handle table {console(pseudo, ntsvc.c)/file/section} + image
// registry + DLL-territory bump allocator, wired onto pemod.c's PeMapImage.
#pragma once
#include "kernel.h"
#include "pemod.h"
#include "../chalice/ntids.h"

long NtOpenFile(void **FileHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                 void *IoStatusBlock, unsigned long ShareAccess, unsigned long OpenOptions);
// CHALICE C4-4: two-root FS -- per-process WOW64 flag NtOpenFile's own
// dirname redirect reads (system32\X -> syswow64/X only while active).
// ntboot.c's wow64 boot path sets this before any WOW64 process's own
// loader-driven NtOpenFile calls run, clears it on that process's exit
// (same per-boot-scoped-state discipline as nt_territory_mark/release).
void nt_wow64_set_active(int active);
int  nt_wow64_is_active(void);
// CHALICE C4-12 leg-12: CPU-backend A/B seam. The real wow64.dll boot path
// never takes a backend param (unlike nt_boot_wow64_probe) -- it just opens
// the literal basename "wowbox64.dll" via NtOpenFile like any other DLL.
// This override lets an isolated probe redirect that ONE lookup to a
// different on-disk backend name (e.g. "libwow64fex.dll") with zero guest-
// code changes, to test whether the leg-11 null-opcode-table fault is
// backend-specific. NULL/empty = default behavior (unchanged, what pe9/
// pe10's canonical gates always see). Per-boot scoped like nt_wow64_active.
void nt_wow64_set_backend_override(const char *name);
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
// CHALICE C3c-2i: NtQuerySection(SectionImageInformation) -- see ntfile.c
// for the SECTION_IMAGE_INFORMATION layout receipt.
long NtQuerySection(void *SectionHandle, unsigned long SectionInformationClass,
                     void *SectionInformation, unsigned long SectionInformationLength,
                     unsigned long *ReturnLength);
// CHALICE C4-44: SECTION_IMAGE_INFORMATION (winternl.h) field offsets --
// shared here (not static in ntfile.c) so ntsvc.c's NtQueryInformationProcess
// (ProcessImageInformation, class 37) fills the SAME struct shape
// NtQuerySection already does, from PeImageInfo (pemod.h), one definition.
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
// CHALICE C4-44: stash of the booted process's own main-image PeImageInfo +
// its ACTUAL (post-map, relocated) entry point address -- set once by
// ntboot.c right after PeMapImage returns the real exe_entry (the exe_info
// itself was already computed earlier, pre-map, for the wow64_mode gate;
// TransferAddress must be the runtime address, not the preferred
// ImageBase+rva NtQuerySection answers with pre-map -- donor wine/dlls/
// ntdll/unix virtual.c's build_main_module reads its OWN process's already-
// mapped image, not a not-yet-mapped section).
void nt_set_main_image_info(const PeImageInfo *info, unsigned long transfer_address);
// CHALICE C3c-2n: NtInitializeNlsFiles -- see ntfile.c for the donor
// contract receipt (wine/dlls/ntdll/unix/env.c:2243).
long NtInitializeNlsFiles(void **BaseAddress, unsigned long *DefaultLocaleId,
                           void *DefaultCasingTableSize);
// CHALICE C3c-2n: NtGetNlsSectionPtr -- see ntfile.c for the donor contract
// receipt (wine/dlls/ntdll/unix/env.c NtGetNlsSectionPtr/get_nls_file_path).
long NtGetNlsSectionPtr(unsigned long Type, unsigned long Id, void *Unknown,
                         void **BaseAddress, unsigned long *Size);

// CHALICE C3c-2o: minimal in-memory registry floor -- keys open, values
// come back absent, so kernelbase's init_locale (dlls/kernelbase/locale.c)
// falls back to NLS defaults instead of dereferencing an uninitialized
// WCHAR* from a "successful" value read. No real hive, no persistence.
// See ntfile.c for the per-handler NTSTATUS-contract receipts.
long NtCreateKey(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes,
                  unsigned long TitleIndex, void *Class, unsigned long CreateOptions,
                  unsigned long *Disposition);
long NtOpenKey(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes);
long NtOpenKeyEx(void **KeyHandle, unsigned long DesiredAccess, void *ObjectAttributes, unsigned long OpenOptions);  // CHALICE C4-14
long NtQueryValueKey(void *KeyHandle, void *ValueName, unsigned long KeyValueInformationClass,
                      void *KeyValueInformation, unsigned long Length, unsigned long *ResultLength);
long NtSetValueKey(void *KeyHandle, void *ValueName, unsigned long TitleIndex,
                    unsigned long Type, void *Data, unsigned long DataSize);
long NtQueryDefaultLocale(unsigned char UserProfile, unsigned long *DefaultLocaleId);

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
// CHALICE C3c-2r: give a previously nt_territory_alloc'd region back to the
// bump allocator's free-list for reuse (see ntfile.c receipt) -- called by
// ntsvc.c's nt_vm_free on a real MEM_RELEASE. addr/size as originally
// returned/committed by nt_territory_alloc (size need not be page-rounded,
// nt_territory_free rounds it itself).
void nt_territory_free(unsigned char *addr, unsigned long size);
void nt_territory_debug_stats(const char *tag);
unsigned long nt_territory_mark(void);
void nt_territory_release_since(unsigned long mark);

// CHALICE C4-8 (monad ruling #2): HIGH territory pool (>=4GB) -- see
// ntfile.c for the base/limit + donor zero_bits-selection receipt.
// ntsvc.c's NtAllocateVirtualMemory tries this pool first for unconstrained
// (ZeroBits==0) requests, falling back to nt_territory_alloc (LOW) if it's
// exhausted; ntboot.c resets both pools per-boot the same way.
unsigned char *nt_high_territory_alloc(unsigned long size_of_image);
unsigned long nt_high_territory_mark(void);
void nt_high_territory_release_since(unsigned long mark);

// ntsvc.c VM-region tracking; fault classifier + NtQueryVirtualMemory.
int nt_vm_region_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out);

// CHALICE C4-9 (monad ruling): RESERVE/COMMIT arena -- see ntfile.c for the
// full donor-grounded design receipt. ntsvc.c's NtAllocateVirtualMemory
// routes ZeroBits==0 requests here instead of the HIGH pool directly;
// ZeroBits!=0 (32-bit-ceiling wow64 thunk callers) is UNCHANGED, still the
// LOW pool. ntboot.c resets the arena per-boot the same way as the other
// two pools. NtQueryVirtualMemory/NtFreeVirtualMemory use nt_arena_contains/
// nt_arena_query/nt_arena_release to answer honestly for arena addresses.
int nt_arena_contains(unsigned long addr);
void nt_fault_region(const char *label, unsigned long addr);
int nt_fault_read32(unsigned long addr, unsigned int *word_out);
unsigned char *nt_arena_reserve(unsigned long size);
int nt_arena_commit(unsigned long base, unsigned long size);
int nt_arena_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out, int *committed_out);
int nt_arena_release(unsigned long addr, unsigned long *size_out);
unsigned long nt_arena_mark(void);
void nt_arena_release_since(unsigned long mark);

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
// CHALICE C4-29: exposed for ntboot.c's own direct Phase-0 call (see
// ntfile.c definition for the full donor-grounded receipt).
void nt_wow64_populate_init_block_if_ntdll32(const char *name, unsigned long base32, unsigned long size32);

// CHALICE C3c Atom 3C-2a: image-relative resolve for the exception-vector
// fault dump (kmain.c sync_handler). See ntfile.c for contract.
int nt_image_resolve(unsigned long addr, char *name_out, unsigned long namelen,
                      unsigned long *off_out);

// CHALICE C3c-2p: name -> {base,size} lookup for the fault dump's targeted
// diagnostics (e.g. reading a specific stock-DLL's .data table at a known
// RVA, relative to THIS boot's actual DLL-territory placement). Same
// registry nt_image_resolve reads, exact-name match (nt_image_find_by_name's
// contract). Returns 0 if that image was never registered.
int nt_image_base_by_name(const char *name, unsigned long *base_out, unsigned long *size_out);

// CHALICE C4-54: exposed (was static) so ntsvc.c's NtAllocateVirtualMemory
// can register a guest EXEC-protect allocation's ACTUAL (base,size,protect)
// result with FEX's InvalidationTracker, same call this file already makes
// from NtProtectVirtualMemory (C4-53) and from its own retroactive C4-27
// stub-page registration (C4-54, nt_wow64_maybe_notify_map_view). No-ops
// (via the shared nt_wow64_ready gate) until FEX's WOW64 runtime is proven
// constructed. `teb` = the caller's OWN x18-captured guest TEB (same
// stash-immediately-on-entry idiom C4-48/53 established -- every callee in
// between is plain aarch64-elf C, free to clobber x18 as scratch).
void nt_wow64_maybe_notify_protect(unsigned long base, unsigned long size,
                                    unsigned long new_protect, unsigned long teb);

// CHALICE C4-59: shared live-State.rip reader -- TEB(x18) -> TlsSlots[16]
// (WOW64_TLS_THREAD_STATE, InternalThreadState*) -> CurrentFrame (FIRST
// member of InternalThreadState, offset 0, FEXCore/include/FEXCore/Debug/
// InternalThreadState.h:91 "CpuStateFrame* const CurrentFrame = &BaseFrame
// State;") -> CpuStateFrame::State (FIRST member of CpuStateFrame, offset 0,
// CoreState.h:393-394) -> CPUState::rip (offset 0x18: InlineJITBlockHeader
// u64@0 + DeferredSignalRefCount u64@8 + pf_raw u32@16 + af_raw u32@20 ->
// rip u64, 8-aligned, @24=0x18; offsets independently re-derived AND
// compile-checked against the real FEX/FEXCore/include/FEXCore/Core/
// CoreState.h header this atom, not hand-guessed -- see HANDOFF.md C4-59).
// Returns 1 + *rip_out on a fully-readable chain, 0 (■ *rip_out untouched)
// on any unreadable/out-of-range link -- same nt_fault_read32 safe-probe
// discipline as every other arena/territory-aware reader in this file.
int nt_wow64_live_state_rip(unsigned long teb, unsigned long *rip_out);

// CHALICE C4-59: shared cycle counter -- incremented once per guest
// exception DELIVERY in kmain.c's sync_try_guest_exception (every self-
// fault/AV cycle this floor forwards to the guest, including the
// elr=0x26D0C02B0 dispatcher-marker self-fault class C4-58 named), read
// (never incremented) by ntsvc.c's NtSetInformationThread(ThreadWow64
// Context) to tag its own eip<-write receipts with "which cycle" they
// landed in -- correlates the two instrumentation sites into one ordered
// timeline without a shared call path between the two translation units.
extern unsigned long nt_c459_cycle;
