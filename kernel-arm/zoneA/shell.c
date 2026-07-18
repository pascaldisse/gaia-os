// GAIA-OS Zone A — the first shell. Cooperative, polling, Temple prompt.
// Lsh (Shrine) gets ported once HolyC lands; this is the bootstrap voice.
#include "kernel.h"
#include "zcmod.h"
#include "pemod.h"
#include "ntboot.h"
#include "ntsvc.h"   // CHALICE C3c Atom 3C-1: nt_dispatch_log_reset/enabled (pe6)

#define UART_DR   0x09000000UL
#define UART_FR   0x09000018UL

static int getc_nb(void) {                 // -1 if no byte waiting
    if (MMIO32(UART_FR) & (1 << 4)) return -1;   // RXFE
    return (int)(MMIO32(UART_DR) & 0xFF);
}

static void putdec(u64 v) {
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts(&b[i]);
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void cmd_help(void) {
    puts("  help       this\n");
    puts("  love       the invariant\n");
    puts("  mem        free heap\n");
    puts("  ticks      heartbeat count\n");
    puts("  uptime     seconds since boot\n");
    puts("  dr <sec>   read disk sector, hexdump first 128B\n");
    puts("  dw <sec> <text>  write text to disk sector\n");
    puts("  format     make RedSea-spirit fs\n");
    puts("  ls         list files\n");
    puts("  wr <name> <text>   write file\n");
    puts("  cat <name> read file\n");
    puts("  fb         redraw ramfb boot pattern\n");
    puts("  hc <file>  compile+run HolyC file (SeedC JIT)\n");
    puts("  hcl <code> compile+run HolyC one-liner\n");
    puts("  zc         run Shrine/zoneC GaiaMod\n");
    puts("  say        run Shrine/zoneC GaiaSay via kernel puts\n");
    puts("  aot        run linked Shrine/zoneC AotBoot module\n");
    puts("  plot       run Shrine/zoneC GaiaPlot: HolyC draws on ramfb\n");
    puts("  pe         run CHALICE PE-ARM64 loader: hello1/2 (slice1) + hello3 (NT svc dispatch)\n");
    puts("  pe4        run CHALICE hello4: loader-path services + DLL machinery (slice3 Atom 3A)\n");
    puts("  pe5        run CHALICE hello5 over fake-ntdll: boot protocol dress rehearsal (slice3 Atom 3B)\n");
    puts("  pe6        run CHALICE hello5 over STOCK wine ntdll.dll (FS-backed): first walk + fault log (slice3 Atom 3C-1)\n");
    puts("  pe7        run CHALICE hello6 (real KERNEL32.dll import) over STOCK wine ntdll.dll: import-table/IAT gate (slice3 Atom 3C-2j)\n");
    puts("  pe8        run CHALICE hello7 (widened KERNEL32.dll surface) over STOCK wine ntdll.dll (slice3 Atom 3C-2r)\n");
    puts("  ip         network info\n");
    puts("  ping <a.b.c.d>  ICMP echo x3 (gw is 10.0.2.2)\n");
    puts("  dns <name> resolve via 10.0.2.3\n");
    puts("  wget <host> [path]  HTTP GET (TCP)\n");
    puts("  reboot     PSCI system reset\n");
}

static u64 parsedec(const char **p) {
    u64 v = 0;
    while (**p == ' ') (*p)++;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
    return v;
}

static unsigned char dbuf[512];

static void cmd_dr(const char *args) {
    u64 sec = parsedec(&args);
    if (BlkRW(sec, dbuf, 0)) { puts("read error\n"); return; }
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 16; c++) {
            unsigned char x = dbuf[r * 16 + c];
            putc("0123456789ABCDEF"[x >> 4]); putc("0123456789ABCDEF"[x & 15]); putc(' ');
        }
        puts(" |");
        for (int c = 0; c < 16; c++) {
            unsigned char x = dbuf[r * 16 + c];
            putc(x >= 32 && x < 127 ? (char)x : '.');
        }
        puts("|\n");
    }
}

static void cmd_dw(const char *args) {
    u64 sec = parsedec(&args);
    while (*args == ' ') args++;
    for (int i = 0; i < 512; i++) dbuf[i] = 0;
    int i = 0;
    while (*args && i < 511) dbuf[i++] = (unsigned char)*args++;
    if (BlkRW(sec, dbuf, 1)) puts("write error\n");
    else { puts("wrote sector "); putdec(sec); puts("\n"); }
}

static void run_cmd(const char *line) {
    if (!*line) return;
    if (streq(line, "help"))  { cmd_help(); return; }
    if (streq(line, "love"))  { puts("LOVE=1.000  (fixed at center)\n"); return; }
    if (streq(line, "mem"))   { putdec(MemFreeBytes()); puts(" bytes free\n"); return; }
    if (streq(line, "ticks")) { putdec(ticks); puts("\n"); return; }
    if (streq(line, "uptime")){ putdec(ticks / 4); puts("s\n"); return; }
    if (line[0] == 'd' && line[1] == 'r' && line[2] == ' ') { cmd_dr(line + 3); return; }
    if (line[0] == 'd' && line[1] == 'w' && line[2] == ' ') { cmd_dw(line + 3); return; }
    if (streq(line, "format")) { puts(FsFormat(8192) ? "format failed\n" : "formatted: RedSea-spirit, 4MB\n"); return; }
    if (streq(line, "ls")) { FsLs(); return; }
    if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' && line[3] == ' ') {
        static char fbuf[4096];
        long n = FsRead(line + 4, fbuf, 4095);
        if (n < 0) puts("no such file\n");
        else { fbuf[n] = 0; puts(fbuf); puts("\n"); }
        return;
    }
    if (line[0] == 'w' && line[1] == 'r' && line[2] == ' ') {   // wr <name> <text>
        const char *p = line + 3;
        char name[39]; int i = 0;
        while (*p && *p != ' ' && i < 38) name[i++] = *p++;
        name[i] = 0;
        while (*p == ' ') p++;
        u64 len = 0; const char *q = p;
        while (*q++) len++;
        if (FsWrite(name, p, len)) puts("write failed\n");
        else { puts("wrote "); puts(name); puts("\n"); }
        return;
    }
    if (line[0] == 'h' && line[1] == 'c' && line[2] == 'l' && line[3] == ' ') {
        HcRun(line + 4);
        return;
    }
    if (line[0] == 'z' && line[1] == 'c' && !line[2]) {
        puts("zc: GaiaMod="); putdec((u64)((long (*)(void))zcmod_bin)()); puts("\n");
        return;
    }
    if (streq(line, "fb")) { FbDrawPattern(); puts("fb: redrawn\n"); return; }
    if (streq(line, "say")) {
        ((void(*)(void(*)(const char*)))zcsay_bin)(zc_kputs);
        return;
    }
    if (streq(line, "aot")) {
        zcaot_link();
        zcaot_entry_fn();
        puts("aot: ok\n");
        return;
    }
    if (streq(line, "pe4")) {
        PeRun(pehello4_bin, pehello4_len, "pe4", 1);
        return;
    }
    if (streq(line, "pe5")) {
        nt_boot_process("hello5.exe", "fake-ntdll.dll", 0, "pe5");
        return;
    }
    if (streq(line, "pe6")) {
        // CHALICE C3c Atom 3C-1: stock ntdll.dll (from FS, mkdisk.py-baked)
        // + hello5.exe (reused exe payload). Dispatcher call-log turned on
        // only for this call (nt_dispatch_log_reset + enabled=1) -- the
        // fault log IS the deliverable here; a crash/hang after thunk-entry
        // is expected (SLICE3-SPEC: "the shell may not survive pe6"), so
        // this verb is deliberately LAST in any driven sequence.
        // CHALICE C3c Atom 3C-2b: arm the IRQ PC-sampling profiler around
        // the same call -- turns the silent post-thunk spin into a visible
        // ntdll.dll+RVA trail in the serial log (kmain.c irq_handler).
        nt_dispatch_log_reset();
        nt_dispatch_log_enabled = 1;
        profile_armed = 1;
        nt_boot_process("hello5.exe", "ntdll.dll", 1, "pe6");
        profile_armed = 0;
        nt_dispatch_log_enabled = 0;
        return;
    }
    if (streq(line, "pe7")) {
        // CHALICE C3c Atom 3C-2j: stock ntdll.dll + hello6.exe (genuine
        // KERNEL32.dll import, real GetStdHandle/WriteFile/ExitProcess
        // calls) -- the import-table/IAT resolution gate. Diagnostic
        // frontier, not yet a required-pass gate line (mirrors pe6's own
        // dispatch-log + PC-sampling profiler rig; a crash/hang past
        // thunk-entry is a possible, non-fatal-to-the-gate outcome here
        // too).
        nt_dispatch_log_reset();
        nt_dispatch_log_enabled = 1;
        profile_armed = 1;
        nt_boot_process("hello6.exe", "ntdll.dll", 1, "pe7");
        profile_armed = 0;
        nt_dispatch_log_enabled = 0;
        return;
    }
    if (streq(line, "pe8")) {
        // CHALICE C3c Atom 3C-2r: stock ntdll.dll + hello7.exe (widened
        // KERNEL32.dll surface walk: GetCommandLineA, GetModuleHandleA,
        // GetProcAddress, GetProcessHeap/HeapAlloc/HeapFree,
        // GetCurrentProcessId, GetLastError/SetLastError). Same log/
        // profiler rig as pe7 -- diagnostic frontier past hello6's own
        // proven WriteFile milestone.
        nt_dispatch_log_reset();
        nt_dispatch_log_enabled = 1;
        profile_armed = 1;
        nt_boot_process("hello7.exe", "ntdll.dll", 1, "pe8");
        profile_armed = 0;
        nt_dispatch_log_enabled = 0;
        return;
    }
    if (streq(line, "pe")) {
        PeRun(pehello1_bin, pehello1_len, "pe1", 0);
        PeRun(pehello2_bin, pehello2_len, "pe2", 0);
        PeRun(pehello3_bin, pehello3_len, "pe3", 1);
        return;
    }
    if (streq(line, "plot")) {
        puts("plot: GaiaPlot (Shrine/zoneC HolyC) drawing on ramfb...\n");
        zcplot_link();
        zcplot_entry_fn();
        puts("plot: ok - magenta rect + yellow diagonals drawn by HolyC\n");
        return;
    }
    if (line[0] == 'h' && line[1] == 'c' && line[2] == ' ') {
        static char hbuf[8192];
        long n = FsRead(line + 3, hbuf, 8191);
        if (n < 0) { puts("no such file\n"); return; }
        hbuf[n] = 0;
        HcRun(hbuf);
        return;
    }
    if (streq(line, "ip")) { NetInfo(); return; }
    if (line[0] == 'p' && line[1] == 'i' && line[2] == 'n' && line[3] == 'g' && line[4] == ' ') {
        const char *p = line + 5;
        u32 ip = 0;
        for (int i = 0; i < 4; i++) {
            ip = (ip << 8) | (u32)parsedec(&p);
            if (i < 3) { if (*p != '.') { puts("usage: ping a.b.c.d\n"); return; } p++; }
        }
        NetPing(ip, 3);
        return;
    }
    if (line[0] == 'd' && line[1] == 'n' && line[2] == 's' && line[3] == ' ') {
        u32 ip;
        if (NetDns(line + 4, &ip)) puts("dns: no answer\n");
        else { puts(line + 4); puts(" -> "); NetPrintIp(ip); puts("\n"); }
        return;
    }
    if (line[0] == 'w' && line[1] == 'g' && line[2] == 'e' && line[3] == 't' && line[4] == ' ') {
        const char *p = line + 5;
        char name[64]; int i = 0;
        while (*p && *p != ' ' && i < 63) name[i++] = *p++;
        name[i] = 0;
        while (*p == ' ') p++;
        const char *path = *p ? p : "/";
        u32 ip;
        if (NetDns(name, &ip)) puts("dns: no answer\n");
        else NetHttpGet(ip, name, path);
        return;
    }
    if (streq(line, "reboot")) {
        puts("resetting...\n");
        asm volatile("mov x0, %0\n hvc #0" :: "r"(0x84000009ULL) : "x0"); // PSCI SYSTEM_RESET
        return;
    }
    puts("unknown: "); puts(line); puts("  (try 'help')\n");
}

void shell_task(void) {
    char line[256];
    int n = 0;
    puts("\nGAIA-OS shell — 'help' for commands\n");
    if (FsMount() == 0) puts("T:/ mounted (RedSea-spirit)\n");
    else puts("T:/ unformatted — run 'format'\n");
    puts("T:/> ");
    for (;;) {
        int c = getc_nb();
        if (c < 0) { Yield(); continue; }
        if (c == '\r' || c == '\n') {
            puts("\n");
            line[n] = 0;
            run_cmd(line);
            n = 0;
            puts("T:/> ");
        } else if (c == 127 || c == 8) {
            if (n > 0) { n--; puts("\b \b"); }
        } else if (n < 255 && c >= 32) {
            line[n++] = (char)c;
            putc((char)c);
        }
    }
}
