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

void irq_handler(void) {
    u64 iar;
    asm volatile("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
    if ((iar & 0xFFFFFF) == 30) {
        ticks++;
        asm volatile("msr cntp_tval_el0, %0" :: "r"(tick_interval));
    }
    asm volatile("msr S3_0_C12_C12_1, %0" :: "r"(iar));
}

// CHALICE C3c Atom 3C-2a: image-relative resolve for ELR/FAR, from the
// same registry NtQueryVirtualMemory/nt_boot_process already populate
// (ntfile.c). Declared here rather than pulling in ntfile.h (which drags
// pemod.h/ntids.h) -- same file-local prototype-duplication convention
// this codebase already uses for struct offsets.
int nt_image_resolve(u64 addr, char *name_out, u64 namelen, u64 *off_out);

// CHALICE C3c Atom 3C-2a: sync_entry (boot.S) now hands over esr/elr/far/
// orig-sp/regs-frame-ptr -- turn a silent CPU fault into one dense serial
// dump instead of the old bare "!SYNC esr=.. elr=.." line. regs[] is the
// SAVEALL frame (boot.S): x0..x18,x29@0..152, x30(lr)@160, 8B slots ->
// index i corresponds to xN at word offset i for N<=18, word 19=x29,
// word 20=x30.
void sync_handler(u64 esr, u64 elr, u64 far, u64 sp_orig, u64 *regs) {
    u64 ec = (esr >> 26) & 0x3F;
    puts("\nCHALICE FAULT esr="); puthex(esr);
    puts(" ec="); puthex(ec);
    puts(" elr="); puthex(elr);
    puts(" far="); puthex(far);
    puts(" sp="); puthex(sp_orig);
    puts("\n");

    puts("  x0=");  puthex(regs[0]);  puts(" x1=");  puthex(regs[1]);
    puts(" x2=");   puthex(regs[2]);  puts(" x3=");  puthex(regs[3]); puts("\n");
    puts("  x4=");  puthex(regs[4]);  puts(" x5=");  puthex(regs[5]);
    puts(" x6=");   puthex(regs[6]);  puts(" x7=");  puthex(regs[7]); puts("\n");
    puts("  x16="); puthex(regs[16]); puts(" x17="); puthex(regs[17]);
    puts(" x18=");  puthex(regs[18]); puts(" lr=");  puthex(regs[20]); puts("\n");

    // IMAGE-RELATIVE line: the gold -- resolves ELR/FAR to "<name>+0x<off>"
    // when inside a mapped image (3A/3B image registry, ntfile.c). This is
    // what points straight at the faulting ntdll instruction.
    char name[64]; u64 off;
    if (nt_image_resolve(elr, name, sizeof(name), &off)) {
        puts("  elr: "); puts(name); puts("+"); puthex(off); puts("\n");
    } else {
        puts("  elr: (not in any mapped image)\n");
    }
    if (nt_image_resolve(far, name, sizeof(name), &off)) {
        puts("  far: "); puts(name); puts("+"); puthex(off); puts("\n");
    } else {
        puts("  far: (not in any mapped image)\n");
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
