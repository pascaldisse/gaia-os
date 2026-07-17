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

// Loads img (a PE32+/ARM64 image of len bytes), applies base relocations,
// resolves imports via zc_resolve(), runs the entry point, prints
// "<tag> ret=<N>". Returns the entry's return value, or -1 on any parse/
// relocation/import failure (with an error line already printed).
// use_nt: 0 = slice-1 behavior, call entry() directly (hello1/hello2, no
// TEB needed). 1 = slice-2 Atom A -- build a TEB/PEB floor (ntsvc.c) and
// invoke entry via pe_enter (x18=TEB), with NtTerminateProcess able to
// unwind straight back here via nt_setjmp/nt_longjmp.
long PeRun(const unsigned char *img, unsigned long len, const char *tag, int use_nt);
