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
#define NT_SERVICE_COUNT                 22

// ---- NTSTATUS consts ----
#define STATUS_SUCCESS                0x00000000L
#define STATUS_NOT_IMPLEMENTED        0xC0000002L
#define STATUS_INVALID_SYSTEM_SERVICE 0xC000001CL

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

#endif
