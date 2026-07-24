// CHALICE C4-67 V2 probe -- FF15 memory-indirect call through an
// own-image .data slot (a plain global function pointer) pointing at a
// local (same-image) function. Isolates the MEMORY-INDIRECT call FORM
// (same opcode class as hello9's own FF15 WriteFile/GetStdHandle call)
// while keeping the TARGET same-image, unlike V3's cross-image target --
// bisects "form" vs "target class" as the kill condition. Zero DLL
// imports; the .data slot itself needs only the already-proven HIGHLOW
// reloc (C4-65), no IAT binding at all. Exits via hello8's proven
// direct-syscall path, distinct exit code 52.
static int helper(void) {
    return 52;
}
static int (*fnptr)(void) = helper;
long PeMain(void) {
    return fnptr();
}
