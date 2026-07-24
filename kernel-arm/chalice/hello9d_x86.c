// CHALICE C4-67 V1 probe -- E8 direct relative same-image call. Baseline
// for the call-shape bisection: does hello9's crash even depend on the
// CROSS-image/indirect nature of its FF15 call, or does ANY branch off
// exe_entry die? Zero imports (mkpe32.sh style, hello8_x86.c's own
// zero-import lineage) -- exits via hello8's PROVEN direct-syscall path
// (PeMain's bare `ret`, no WriteFile needed), distinct exit code 51.
static int helper(void) {
    return 51;
}
long PeMain(void) {
    return helper();
}
