// CHALICE C1 slice 1 — aarch64 PE loader (pemod.c) + embedded test images.
#pragma once

// Embedded by build.sh from kernel-arm/chalice/hello{1,2}.exe (xxd -i style,
// mirrors the zcaot_bin embed step in zcmod.c).
extern const unsigned char pehello1_bin[];
extern const unsigned long pehello1_len;
extern const unsigned char pehello2_bin[];
extern const unsigned long pehello2_len;

// Loads img (a PE32+/ARM64 image of len bytes), applies base relocations,
// resolves imports via zc_resolve(), runs the entry point, prints
// "<tag> ret=<N>". Returns the entry's return value, or -1 on any parse/
// relocation/import failure (with an error line already printed).
long PeRun(const unsigned char *img, unsigned long len, const char *tag);
