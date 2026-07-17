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
long nt_boot_process(const char *exe_name, const char *ntdll_name);
