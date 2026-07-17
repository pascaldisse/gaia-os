// CHALICE C1 slice 1 — PE test artifact 2: imports KPutS from gaia.dll.
// Custom entry PeMain, no CRT. The `msg` pointer forces lld to emit a
// DIR64 base relocation (mp is an absolute VA of a .rdata literal stored
// in .data), exercising pemod.c's relocation-apply path for real.
extern void KPutS(const char *s);

static const char msg[] = "CHALICE: the cup fills\n";
static const char *mp = msg;

long PeMain(void) {
    KPutS(mp);
    return 7;
}
