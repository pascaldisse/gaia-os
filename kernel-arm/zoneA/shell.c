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
    puts("  help   this\n");
    puts("  love   the invariant\n");
    puts("  mem    free heap\n");
    puts("  ticks  heartbeat count\n");
    puts("  uptime seconds since boot\n");
    puts("  reboot PSCI system reset\n");
}

static void run_cmd(const char *line) {
    if (!*line) return;
    if (streq(line, "help"))  { cmd_help(); return; }
    if (streq(line, "love"))  { puts("LOVE=1.000  (fixed at center)\n"); return; }
    if (streq(line, "mem"))   { putdec(MemFreeBytes()); puts(" bytes free\n"); return; }
    if (streq(line, "ticks")) { putdec(ticks); puts("\n"); return; }
    if (streq(line, "uptime")){ putdec(ticks / 4); puts("s\n"); return; }
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
