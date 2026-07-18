// CHALICE C4 leg1 atom -- 32-bit x86 PE test artifact. Freestanding, zero
// imports, mirrors hello1.c's pattern exactly but targets i386 (Machine
// 0x14C) instead of aarch64 -- this is the guest-arch probe for the x86
// door: proves the toolchain can emit a genuine 32-bit x86 PE before any
// WOW64/box64cpu mounting work starts. Custom entry PeMain, no CRT.
long PeMain(void) {
    return 42;
}
