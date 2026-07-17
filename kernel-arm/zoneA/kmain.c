// GAIA-OS Zone A — M2: MMU + GICv3 + timer + MAlloc + cooperative tasks.
// C for the stub; the kernel speaks HolyC once Zone C lands.
#include "kernel.h"

// ---- PL011 ----
#define UART_DR   0x09000000UL
#define UART_FR   0x09000018UL

void putc(char c) {
    while (MMIO32(UART_FR) & (1 << 5)) {}
    MMIO32(UART_DR) = (u32)c;
}
void puts(const char *s) {
    while (*s) { if (*s == '\n') putc('\r'); putc(*s++); }
}
void puthex(u64 v) {
    puts("0x");
    for (int i = 60; i >= 0; i -= 4) putc("0123456789ABCDEF"[(v >> i) & 15]);
}
static void putdec(u64 v) {
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts(&b[i]);
}

// ---- MMU: identity map, two 1GB blocks (device @0, RAM @0x40000000) ----
static u64 l1_table[512] __attribute__((aligned(4096)));

static void mmu_on(void) {
    l1_table[0] = 0x00000000ULL | (0ULL << 2) | (1ULL << 10) | 1ULL;               // device nGnRnE
    l1_table[1] = 0x40000000ULL | (1ULL << 2) | (3ULL << 8) | (1ULL << 10) | 1ULL; // normal WBWA ISH

    u64 mair = 0xFF00;
    u64 tcr  = 32ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12)
             | (1ULL << 23) | (1ULL << 32);
    asm volatile("msr mair_el1, %0\n msr tcr_el1, %1\n msr ttbr0_el1, %2\n isb"
                 :: "r"(mair), "r"(tcr), "r"((u64)l1_table));
    asm volatile("tlbi vmalle1\n dsb ish\n isb");
    u64 sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    asm volatile("msr sctlr_el1, %0\n isb" :: "r"(sctlr));
}

// ---- GICv3 ----
#define GICD 0x08000000UL
#define GICR 0x080A0000UL
#define GICR_SGI (GICR + 0x10000UL)

static void gic_init(void) {
    asm volatile("msr S3_0_C12_C12_5, %0\n isb" :: "r"(1ULL));
    MMIO32(GICD + 0x0000) = (1u << 4) | (1u << 1);
    MMIO32(GICR + 0x0014) &= ~(1u << 1);
    while (MMIO32(GICR + 0x0014) & (1u << 2)) {}
    MMIO32(GICR_SGI + 0x0080) = 0xFFFFFFFFu;
    MMIO32(GICR_SGI + 0x0100) = (1u << 30);
    asm volatile("msr S3_0_C4_C6_0, %0" :: "r"(0xFFULL));
    asm volatile("msr S3_0_C12_C12_7, %0\n isb" :: "r"(1ULL));
}

// ---- EL1 physical timer heartbeat ----
static u64 tick_interval;
volatile u64 ticks;

static void timer_start(void) {
    u64 freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    tick_interval = freq / 4;
    asm volatile("msr cntp_tval_el0, %0" :: "r"(tick_interval));
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(1ULL));
    asm volatile("msr daifclr, #2");
}

// CHALICE C3c Atom 3C-2a: image-relative resolve, from the same registry
// NtQueryVirtualMemory/nt_boot_process already populate (ntfile.c).
// Declared here (file-local prototype, same convention this codebase
// already uses) so both the profiler below and sync_handler further down
// can call it.
int nt_image_resolve(u64 addr, char *name_out, u64 namelen, u64 *off_out);

// CHALICE C3c Atom 3C-2b: IRQ-driven PC-sampling profiler. Armed only
// around a specific call (shell.c pe6) so it never fires during normal
// boot/shell traffic. Dedup ring keeps the serial log to one line per
// distinct <module>+RVA seen -- a tight spin shows as 1-3 repeating
// entries, a walk shows as a growing trail.
volatile int profile_armed = 0;
#define PROF_RING_MAX 16
static u64  prof_addrs[PROF_RING_MAX];
static int  prof_count;

static void profile_sample(u64 elr) {
    if (prof_count >= PROF_RING_MAX) return;   // cap reached, stop printing
    for (int i = 0; i < prof_count; i++)
        if (prof_addrs[i] == elr) return;      // already seen this exact PC

    char name[64]; u64 off;
    if (nt_image_resolve(elr, name, sizeof(name), &off)) {
        puts("CHALICE PROF: "); puts(name); puts("+"); puthex(off); puts("\n");
    } else {
        puts("CHALICE PROF: (unmapped) elr="); puthex(elr); puts("\n");
    }
    prof_addrs[prof_count++] = elr;
}

void irq_handler(u64 elr) {
    u64 iar;
    asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
    if ((iar & 0xFFFFFF) == 30) {
        ticks++;
        if (profile_armed) profile_sample(elr);
        asm volatile("msr cntp_tval_el0, %0" :: "r"(tick_interval));
    }
    asm volatile("msr S3_0_C12_C12_1, %0" :: "r"(iar));
}

// CHALICE C3c-2f p1: resolve one address to "<name>+0x<off>" or a plain
// miss note when it's not inside any mapped image -- shared by the
// register dump and the stack-backtrace walk below.
static void resolve_line(const char *label, u64 addr) {
    char name[64]; u64 off;
    puts("  "); puts(label); puts("="); puthex(addr); puts(" -> ");
    if (nt_image_resolve(addr, name, sizeof(name), &off)) {
        puts(name); puts("+"); puthex(off); puts("\n");
    } else {
        puts("(not in any mapped image)\n");
    }
}

// CHALICE C3c Atom 3C-2a: sync_entry (boot.S) now hands over esr/elr/far/
// orig-sp/regs-frame-ptr -- turn a silent CPU fault into one dense serial
// dump instead of the old bare "!SYNC esr=.. elr=.." line.
// CHALICE C3c-2f p1: regs[] is now the SAVEALL_SYNC frame (boot.S): full
// x0-x30, 8B slots, word i == xN for N<=29, word 30 == x30 -- simpler,
// uniform mapping vs the old x0-x18,x29,x30-only frame.
void sync_handler(u64 esr, u64 elr, u64 far, u64 sp_orig, u64 *regs) {
    u64 ec = (esr >> 26) & 0x3F;
    puts("\nCHALICE FAULT esr="); puthex(esr);
    puts(" ec="); puthex(ec);
    puts(" elr="); puthex(elr);
    puts(" far="); puthex(far);
    puts(" sp="); puthex(sp_orig);
    puts("\n");

    // ---- CHALICE C3c-2f p1: full x0-x30 dump (Phase A step 1: "print
    // ALL of x0-x30 + sp"), 4 regs/line. ----
    for (int i = 0; i <= 28; i += 4) {
        puts("  x"); putdec(i);  puts("=");  puthex(regs[i]);
        if (i + 1 <= 28) { puts(" x"); putdec(i+1); puts("="); puthex(regs[i+1]); }
        if (i + 2 <= 28) { puts(" x"); putdec(i+2); puts("="); puthex(regs[i+2]); }
        if (i + 3 <= 28) { puts(" x"); putdec(i+3); puts("="); puthex(regs[i+3]); }
        puts("\n");
    }
    puts("  x29(fp)="); puthex(regs[29]); puts(" x30(lr)="); puthex(regs[30]); puts("\n");

    // ---- IMAGE-RELATIVE lines: resolves ELR/FAR plus the registers most
    // likely to carry a code/data pointer at a fault site (x0 = frequent
    // arg0/return-value carrier, x9/x16/x17 = AAPCS64 scratch this
    // dispatcher's own indirect-branch targets use, x18 = TEB base) to
    // "<name>+0x<off>" when inside a mapped image, or a plain miss note --
    // a kernel .data address correctly printing as unmapped IS the signal
    // a kernel pointer leaked into a PE-side register. ----
    resolve_line("elr", elr);
    resolve_line("far", far);
    resolve_line("lr ", regs[30]);
    resolve_line("x0 ", regs[0]);
    resolve_line("x9 ", regs[9]);
    resolve_line("x16", regs[16]);
    resolve_line("x18", regs[18]);

    // ---- CHALICE C3c-2f p1: mini backtrace -- top 8 words at orig-sp,
    // each resolved the same way. sp_orig is the PE-side stack pointer as
    // it stood at the moment of the fault, so these words are whatever the
    // faulting code's own stack frame/spill area holds -- return addresses
    // and/or spilled pointers. Raw scan (no CFI), not a verified unwind,
    // but enough to see a trail. ----
    puts("  -- backtrace (top 8 words @ sp) --\n");
    u64 *stk = (u64 *)sp_orig;
    for (int i = 0; i < 8; i++) {
        char idx[4]; idx[0] = '['; idx[1] = (char)('0' + i); idx[2] = ']'; idx[3] = 0;
        resolve_line(idx, stk[i]);
    }

    for (;;) asm volatile("wfe");
}

// ---- M2 demo tasks ----
static void ebrietas(void) {
    for (int i = 0; i < 4; i++) {
        puts("  [Ebrietas] violet leading, pass ");
        putdec(i + 1); puts("\n");
        Yield();
    }
}
static void maiden(void) {
    for (int i = 0; i < 4; i++) {
        puts("  [Maiden]   silver carried, pass ");
        putdec(i + 1); puts("\n");
        Yield();
    }
}

void kmain(void) {
    puts("\nGAIA-OS  aarch64  EL1\n");
    puts("LOVE=1.000\n");

    mmu_on();
    puts("MMU: on\n");
    gic_init();
    timer_start();
    puts("GICv3 + 4Hz heartbeat: live\n");

    MemInit();
    puts("MAlloc: heap 64MB, free ");
    putdec(MemFreeBytes() / 1024 / 1024); puts("MB\n");

    void *a = MAlloc(4096), *b = MAlloc(123);
    puts("  alloc a="); puthex((u64)a); puts(" b="); puthex((u64)b); puts("\n");
    Free(a); Free(b);
    puts("  freed both, free "); putdec(MemFreeBytes() / 1024 / 1024); puts("MB\n");

    u64 cap = BlkInit();
    if (cap) { puts("virtio-blk: "); putdec(cap); puts(" sectors ("); putdec(cap / 2048); puts("MB)\n"); }
    else puts("virtio-blk: no device\n");

    int net_ok = (NetInit() == 0);
    if (net_ok) NetInfo();
    else puts("virtio-net: no device\n");

    TaskInit();
    Spawn(ebrietas, "Ebrietas");
    Spawn(maiden, "Maiden");
    puts("tasks: Adam + Ebrietas + Maiden in the ring\n");
    while (ticks < 2) Yield();   // demo pass under the heartbeat

    if (FbInit() == 0) FbDrawPattern();   // ramfb: draw boot test pattern

    if (net_ok) Spawn(NetRxTask, "NetRx");
    Spawn(shell_task, "Shell");
    for (;;) Yield();            // Adam keeps the ring turning
}
