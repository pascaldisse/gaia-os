// CHALICE C3a Atom 3A — mini.dll: no-import export test payload for
// NtMapViewOfSection(SEC_IMAGE) + hello4.exe's own export-directory walk.
// No DllMain: entry (MiniEntry) is never called by NtMapViewOfSection
// (spec: mapping only, no import resolution, no entry invocation) -- it
// exists purely so the linker has a valid AddressOfEntryPoint; "returns 1"
// per spec, mirroring the EXTRADLLFLAGS-equivalent convention.
// LLP64 note: aarch64-windows-gnu `unsigned long` = 32-bit; only by-value
// `long` args/return used here, no pointer-target integers -- nothing for
// the LLP64 trap to catch in this file.
__declspec(dllexport) long MiniAdd(long a, long b) { return a + b + 58; }

long MiniEntry(void) { return 1; }
