// CHALICE Slice 2 Atom A — shared kernel<->PE-stub header.
// Included by BOTH sides of the floor:
//   kernel-arm/zoneA/ntsvc.{c,h}   (aarch64-none-elf, kernel side)
//   kernel-arm/chalice/ntstub.S    (aarch64-windows-gnu, PE side, via cpp)
// Plain #defines only (no enum/typedef) so a .S file can #include this
// directly through the assembler's C-preprocessor pass — a C `enum` would
// survive cpp untouched and choke the assembler.

#ifndef GAIA_CHALICE_NTIDS_H
#define GAIA_CHALICE_NTIDS_H

// ---- table-0 service ids. Order = HANDOFF.md "ordered boot service list".
// Real in Atom A: NtWriteFile, NtClose, NtTerminateProcess (marked REAL below).
// Everything else stubs STATUS_NOT_IMPLEMENTED (boot survives per HANDOFF).
#define NT_SVC_NTALLOCATEVIRTUALMEMORY    0
#define NT_SVC_NTFREEVIRTUALMEMORY        1
#define NT_SVC_NTPROTECTVIRTUALMEMORY     2
#define NT_SVC_NTCREATESECTION            3
#define NT_SVC_NTMAPVIEWOFSECTION         4
#define NT_SVC_NTCREATEFILE               5
#define NT_SVC_NTOPENFILE                 6
#define NT_SVC_NTREADFILE                 7
#define NT_SVC_NTWRITEFILE                8   // REAL
#define NT_SVC_NTCLOSE                    9   // REAL
#define NT_SVC_NTCREATETHREAD            10
#define NT_SVC_NTRESUMETHREAD            11
#define NT_SVC_NTTERMINATETHREAD         12
#define NT_SVC_NTTERMINATEPROCESS        13   // REAL
#define NT_SVC_NTWAITFORSINGLEOBJECT     14
#define NT_SVC_NTCREATEEVENT             15
#define NT_SVC_NTQUERYINFORMATIONPROCESS 16
#define NT_SVC_NTQUERYSYSTEMINFORMATION  17
#define NT_SVC_NTCONTINUE                18   // slice-2 boss, Atom B
#define NT_SVC_NTCREATEKEY               19
#define NT_SVC_NTOPENKEY                 20
#define NT_SVC_NTQUERYVALUEKEY           21
#define NT_SVC_NTQUERYVIRTUALMEMORY      22  // CHALICE C3a Atom 3A, REAL
#define NT_SERVICE_COUNT                 23

// ---- NTSTATUS consts ----
#define STATUS_SUCCESS                0x00000000L
#define STATUS_NOT_IMPLEMENTED        0xC0000002L
#define STATUS_INVALID_SYSTEM_SERVICE 0xC000001CL
#define STATUS_INVALID_PARAMETER      0xC000000DL   // Atom B
#define STATUS_INFO_LENGTH_MISMATCH   0xC0000004L   // Atom B
#define STATUS_NO_MEMORY              0xC0000017L   // Atom B
// ---- CHALICE C3a Atom 3A: loader-path service NTSTATUS consts ----
#define STATUS_INVALID_INFO_CLASS     0xC0000003L
#define STATUS_INVALID_HANDLE         0xC0000008L
#define STATUS_OBJECT_NAME_INVALID    0xC0000033L
#define STATUS_OBJECT_NAME_NOT_FOUND  0xC0000034L
#define STATUS_INVALID_IMAGE_FORMAT   0xC000007BL

// ---- Atom B: NtAllocateVirtualMemory/NtFreeVirtualMemory AllocationType/
// FreeType bits (winnt.h). Only these are accepted; anything else in
// AllocationType -> STATUS_INVALID_PARAMETER (SLICE2-SPEC Atom B).
#define MEM_COMMIT   0x00001000L
#define MEM_RESERVE  0x00002000L
#define MEM_DECOMMIT 0x00004000L
#define MEM_RELEASE  0x00008000L

// ---- Atom 3A: NtCreateSection AllocationAttributes (winnt.h). Only the
// SEC_IMAGE path is implemented -- anything else -> STATUS_INVALID_PARAMETER
// (SLICE3-SPEC Atom 3A: "anything else -> STATUS_INVALID_PARAMETER").
#define SEC_IMAGE    0x1000000L
// ---- Atom 3A: MEMORY_BASIC_INFORMATION.Type / .State bits used by
// NtQueryVirtualMemory's MemoryBasicInformation answer.
#define MEM_IMAGE    0x1000000L
#define MEM_FREE     0x00010000L
#define PAGE_NOACCESS          0x01L
#define PAGE_EXECUTE_READWRITE 0x40L

// ---- TEB slot convention (offsets from TEB base; x18 holds TEB on the PE
// side per Windows ARM64 ABI — x18 is a reserved/platform register there,
// never general scratch, so this is safe to rely on).
// +0x370 (ServiceTable) / +0x378 (syscall_frame): PINNED BY DONOR —
//   wine/dlls/ntdll/unix/signal_arm64.c:1630 __wine_syscall_dispatcher:
//     "ldr x10, [x18, #0x378]"        thread_data->syscall_frame
//     "ldr x16, [x18, #0x370]"        thread_data->syscall_table
// +0x380 (dispatch entry): OUR OWN GAIA-OS EXTENSION — deliberate deviation.
//   Real Wine's ARM64 syscall thunk (wine/include/wine/asm.h,
//   __ASM_SYSCALL_FUNC __aarch64__ branch) does NOT read a TEB slot for the
//   dispatcher address at all — it embeds a literal-pool pointer to the
//   fixed __wine_syscall_dispatcher symbol at PE-BUILD time:
//     "ldr x16, 1f\n\t ldr x16,[x16]\n\t blr x16\n\t 1: .quad __wine_syscall_dispatcher"
//   That only works because real ntdll.dll is linked against a real,
//   address-stable unix-side symbol. Our hello3.exe/ntstub.S are built by
//   mkpe.sh independently of the kernel image and can't know the kernel's
//   nt_dispatch address at PE-link time, so we route the indirection
//   through a TEB slot instead (populated by ntsvc.c's nt_build_teb at
//   PE-launch time). Noted in HANDOFF.md State section.
#define NT_TEB_SERVICE_TABLE_OFF   0x370
#define NT_TEB_SYSCALL_FRAME_OFF   0x378
#define NT_TEB_DISPATCH_ENTRY_OFF  0x380

// ---- pseudo handle for stdout. Stashed at
// RTL_USER_PROCESS_PARAMETERS+0x28 (StandardOutput — receipt in ntsvc.c)
// and checked for equality by NtWriteFile.
#define NT_STDOUT_HANDLE 0x14

// ---- Atom B: syscall_frame extension. Donor's __wine_syscall_dispatcher
// (wine/dlls/ntdll/unix/signal_arm64.c:1630-1785) stashes the PE stub's own
// pre-blr x30 at frame+0xf0 (paired stp with frame+0xf8=entry sp) and
// restores x30 from there right before its final `ret x16` — meaning real
// Wine stubs are pure tail calls (`blr x16`, nothing after: frame->pc IS the
// original caller's return address, so `ret x16` skips the stub entirely).
// Our stubs are NOT pure tail calls (each has trailing cleanup/`ret`), so we
// keep frame->pc's role (0x100: dispatcher-entry x30 = return-into-stub
// address, used by `ret x16`) but ALSO need the stub's true pre-blr x30
// restored into the real x30 register before that `ret x16` lands, so the
// stub's OWN trailing `ret` later returns to the stub's actual caller (this
// is the bugfixed replacement for Atom A's callee-saved-x23 stash, which
// violated the Windows AAPCS64 ABI contract with whatever calls our stubs
// — see ntdisp.S/ntstub.S receipts). NT_FRAME_STUB_LR_OFF is GAIA-OS's own
// extension slot (not a donor-pinned offset — donor's equivalent role lives
// at its frame+0xf0, but our frame+0xf0 is already taken by frame->sp);
// placed in the gap between cpsr(0x108) and id(0x120), 24 bytes free there.
#define NT_FRAME_STUB_LR_OFF 0x110

// ---- Atom B: KUSER_SHARED_DATA, fixed physical address (HANDOFF: "we ARE
// the unix side"). Offsets receipted against reactos/sdk/include/ndk/
// ketypes.h _KUSER_SHARED_DATA (has inline // 0x... comments per field,
// matches well-known public Win64 KUSER_SHARED_DATA offsets).
#define NT_KUSER_SHARED_DATA_ADDR      0x7ffe0000UL
#define NT_KUSER_NTBUILDNUMBER_OFF     0x260   // ketypes.h:568 (WIN10+ branch)
#define NT_KUSER_NTMAJORVERSION_OFF    0x26c   // ketypes.h:580
#define NT_KUSER_NTMINORVERSION_OFF    0x270   // ketypes.h:581
#define NT_KUSER_PROCESSORFEATURES_OFF 0x274   // ketypes.h:582, 64 BOOLEAN
#define NT_KUSER_NUMBEROFPHYSICALPAGES_OFF 0x2e8  // ketypes.h:615
// -m 1G (build.sh, Atom B) / 4096 -- "sane" per SLICE2-SPEC, not dynamically
// probed (no physical page allocator here; RAM size is a build-time given).
#define NT_KUSER_NUM_PHYS_PAGES        262144
// CHALICE C3b Atom 3B: InterruptTime (KSYSTEM_TIME, well-known offset 0x08
// per reactos/sdk/include/ndk/ketypes.h) -- backs the unixlib
// system_time_precise call (ntboot.c writes it fresh on each call, kernel
// "IS the unix side" per HANDOFF, no periodic-update task needed).
#define NT_KUSER_INTERRUPTTIME_OFF     0x08

// ---- CHALICE C3b Atom 3B: unixlib passthrough. __wine_unix_call_dispatcher
// (fake-ntdll.c, PE side) forwards (handle, code, args) to whatever kernel
// wrote into *__wine_syscall_dispatcher (nt_unix_call_shim, ntboot.c),
// which does table[code](args) using handle as the table base pointer
// (== what kernel wrote into *__wine_unixlib_handle). `code` values below
// mirror SLICE3-SPEC's unix_call_funcs ordering exactly (8 entries);
// console subset (dbg_write, system_time_precise) real, rest
// STATUS_NOT_IMPLEMENTED stubs (ntboot.c).
#define NT_UNIXCALL_LOAD_SO_DLL           0
#define NT_UNIXCALL_UNWIND_BUILTIN_DLL    1
#define NT_UNIXCALL_DBG_WRITE             2
#define NT_UNIXCALL_SERVER_CALL           3
#define NT_UNIXCALL_SERVER_FD_TO_HANDLE   4
#define NT_UNIXCALL_SERVER_HANDLE_TO_FD   5
#define NT_UNIXCALL_SPAWNVP               6
#define NT_UNIXCALL_SYSTEM_TIME_PRECISE   7
#define NT_UNIXCALL_COUNT                 8

#endif
