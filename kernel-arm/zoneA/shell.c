// GAIA-OS Zone A — the first shell. Cooperative, polling, Temple prompt.
// Lsh (Shrine) gets ported once HolyC lands; this is the bootstrap voice.
#include "kernel.h"

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
    if (streq(line, "reboot")) {
        puts("resetting...\n");
        asm volatile("mov x0, %0\n hvc #0" :: "r"(0x84000009ULL) : "x0"); // PSCI SYSTEM_RESET
        return;
    }
    puts("unknown: "); puts(line); puts("  (try 'help')\n");
}

void shell_task(void) {
    char line[128];
    int n = 0;
    puts("\nGAIA-OS shell — 'help' for commands\n");
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
        } else if (n < 127 && c >= 32) {
            line[n++] = (char)c;
            putc((char)c);
        }
    }
}
