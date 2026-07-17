// CHALICE C1 slice 1 — PE test artifact 1: freestanding, zero imports.
// Custom entry PeMain, no CRT, no runtime deps. Compiled by mkpe.sh into
// hello1.exe (aarch64 PE), embedded by zoneA/build.sh, run by pemod.c.
long PeMain(void) {
    return 42;
}
