// CHALICE C3b Atom 3B — boot protocol (fake-ntdll dress rehearsal).
#pragma once

// Phase-0 emulation, SLICE3-SPEC Atom 3B: map ntdll_name + exe_name (blob
// manifest names, pemod.h) via the 3A machinery, wire the unixlib
// passthrough into the mapped ntdll's data exports, build TEB/PEB/stack,
// push a synthetic CONTEXT (Architecture verdict 2) and enter the ntdll's
// LdrInitializeThunk. Prints "pe5 ret=<N>" itself (mirrors PeRun's own
// trailing print) once the booted exe calls NtTerminateProcess (unwinds
// back here via the same nt_setjmp/nt_longjmp channel PeRun's use_nt=1
// path already uses). Returns the exit code, or -1 on any boot-time
// failure (blob missing, required export missing, map failure) -- error
// already printed to serial in that case.
// `stock`: CHALICE C3c Atom 3C-1 -- when nonzero (pe6/pe7, real Wine
// ntdll.dll, as opposed to pe5's synthetic fake-ntdll.dll dress
// rehearsal), also prints "CHALICE: stock ntdll mapped" right after the
// map+unixlib wiring step and "CHALICE: stock thunk enters" right before
// the jump into LdrInitializeThunk -- the two markers SLICE3-SPEC's fault
// log needs to bracket "whatever happens next".
// `label`: CHALICE C3c Atom 3C-2j -- the shell-verb name to print in the
// trailing "<label> ret=<N>" line (was hardcoded "pe5"/"pe6" off `stock`
// alone; pe7 also has stock=1 but needs its own distinct label).
long nt_boot_process(const char *exe_name, const char *ntdll_name, int stock, const char *label);

// CHALICE C4-2 (atom C4-2, C4-SPEC.md): native WOW64 image trio -- maps
// wow64.dll (native-side WOW64 dispatcher, aarch64-windows/0xAA64) +
// ONE CPU-simulation backend (cpu_backend_name: "wowbox64.dll" or
// "libwow64fex.dll", both aarch64-windows/0xAA64, same BTCpu* interface --
// IRON law, the backend is a caller-supplied PARAMETER here, never
// hardcoded) into DLL territory via the existing PeMapImage/
// nt_territory_alloc/nt_image_register machinery -- no new mapping
// primitive, same pattern nt_boot_process already uses for ntdll/exe. No
// guest code runs (no TEB/thread/CONTEXT built) -- this atom proves the
// two images load+relocate clean over our floor, nothing more. Prints
// "CHALICE: wow64.dll mapped" and "CHALICE: CPU backend mapped" (with the
// chosen name) on success. Returns 0, or -1 on any blob/map failure
// (error already printed to serial).
long nt_boot_wow64_probe(const char *cpu_backend_name, const char *label);
