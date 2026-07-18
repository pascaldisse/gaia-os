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

// ---- MMU: identity map (device @0, RAM @0x40000000) ----
// CHALICE C4-8 (monad ruling #1): QEMU RAM is now a build.sh parameter
// (GAIA_QEMU_MEM, default 4G -- see build.sh) -- was a single 1GB L1 block,
// widened to NT_RAM_BLOCKS blocks (4 * 1GB = 4GB default) so the enlarged
// physical range stays fully identity-mapped. Bump NT_RAM_BLOCKS (and
// GAIA_QEMU_MEM) together if the RAM parameter ever grows past 4G.
// NT_RAM_TOP feeds the bound checks below (sync_handler's fp-chain walk +
// root_key_names spot-check) so debug dumps don't silently stop resolving
// pointers above the old 1GB ceiling now that ntfile.c's HIGH territory
// pool (>=4GB) is live real memory too.
#define NT_RAM_BASE   0x40000000ULL
#define NT_RAM_BLOCKS 4ULL                                   // 4 * 1GB = 4GB (GAIA_QEMU_MEM default)
#define NT_RAM_TOP    (NT_RAM_BASE + NT_RAM_BLOCKS * 0x40000000ULL)
static u64 l1_table[512] __attribute__((aligned(4096)));

static void mmu_on(void) {
    l1_table[0] = 0x00000000ULL | (0ULL << 2) | (1ULL << 10) | 1ULL;               // device nGnRnE
    for (u64 i = 0; i < NT_RAM_BLOCKS; i++) {
        u64 base = NT_RAM_BASE + i * 0x40000000ULL;
        l1_table[1 + i] = base | (1ULL << 2) | (3ULL << 8) | (1ULL << 10) | 1ULL;  // normal WBWA ISH
    }

    // CHALICE C4-8: T0SZ 32->25 -- T0SZ=32 gave only a 32-bit (4GB) TTBR0
    // VA range (0x0-0xFFFFFFFF), so `l1_table` slots beyond index 3 (any
    // physical address >= 0x100000000, i.e. the entire HIGH territory pool,
    // ntfile.c) were OUTSIDE the address space the MMU could translate AT
    // ALL -- not a missing block-descriptor bug, an address-size fault the
    // table content can never fix (live-confirmed: sync_handler fired on
    // the very first HIGH-pool touch, far=0x100000000, immediately after
    // this leg's pool split landed). ARMv8 4KB-granule starting-level rule
    // (ARM ARM D8.2.1): T0SZ 25-33 (IA 31-39 bits) all start at level 1 --
    // same single-level block-descriptor scheme this file already uses, NO
    // new table level needed -- so T0SZ=25 (IA=39 bits, 512GB VA range)
    // stays on the exact same mechanism, just makes the FULL 512-entry
    // l1_table addressable (2^(39-30)=512 entries) instead of only its
    // first 4. Comfortable headroom past NT_RAM_TOP for any GAIA_QEMU_MEM
    // this floor is likely to need before the next architecture change.
    u64 mair = 0xFF00;
    u64 tcr  = 25ULL | (1ULL << 8) | (1ULL << 10) | (3ULL << 12)
             | (1ULL << 23) | (1ULL << 32);
    asm volatile("msr mair_el1, %0\n msr tcr_el1, %1\n msr ttbr0_el1, %2\n isb"
                 :: "r"(mair), "r"(tcr), "r"((u64)l1_table));
    asm volatile("tlbi vmalle1\n dsb ish\n isb");
    u64 sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    asm volatile("msr sctlr_el1, %0\n isb" :: "r"(sctlr));
}

// CHALICE C4-9 (monad ruling #2): arena L1 table-descriptor installer --
// lets ntfile.c's RESERVE/COMMIT arena add L1->L2 TABLE entries (bits[1:0]
// = 0b11, ARM ARM D8.3) for the arena's own 1GB slots ONLY, without ever
// touching the RAM/device 1GB BLOCK entries (bits[1:0]=0b01) mmu_on installs
// above -- l1_table is otherwise private to this file, so this is the one
// controlled seam. l2_phys is a 4KB-aligned physical (== virtual, identity-
// mapped) base of a 512-entry L2 table page (ntfile.c allocates it from the
// LOW territory pool, already zeroed). A fresh tlbi vmalle1 after the
// install covers the conservative case (ruling #2: "tlbi vmalle1 acceptable");
// callers doing many installs in a row may batch and invalidate once, but
// this floor's commit rate (per-window, not per-instruction) makes that an
// unneeded optimization for now.
void nt_l1_install_table(u64 l1_idx, u64 l2_phys) {
    l1_table[l1_idx] = l2_phys | 0b11ULL;
    asm volatile("dsb ishst\n tlbi vmalle1\n dsb ish\n isb");
}

u64 nt_l1_read(u64 l1_idx) { return l1_table[l1_idx]; }

// CHALICE C4-9: standalone TLB invalidate for ntfile.c's arena COMMIT path
// (after writing a new L2 block descriptor for a just-backed 2MB window) --
// same instruction sequence mmu_on itself already uses, factored out so the
// arena code doesn't need its own inline asm.
void nt_tlbi_vmalle1(void) {
    asm volatile("dsb ishst\n tlbi vmalle1\n dsb ish\n isb");
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
int nt_image_base_by_name(const char *name, u64 *base_out, u64 *size_out);
int nt_arena_contains(unsigned long addr);
int nt_arena_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out, int *committed_out);
int nt_vm_region_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out);
int nt_arena_commit(unsigned long base, unsigned long size);
void nt_fault_region(const char *label, unsigned long addr);
int nt_fault_read32(unsigned long addr, unsigned int *word_out);

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

// CHALICE C4-15: FEX's Windows allocator deliberately installs raw Linux
// prctl/madvise SVC hooks when Wine exposes __wine_unix_call_dispatcher but
// its call-checker prevents the unixlib trampoline path (pinned donor
// FEX/Source/Windows/Common/Allocator.cpp:140-176,178-241). Linux normally
// consumes x8=167/233. GAIA has no Linux substrate, so recover only these
// two advisory calls, only when ELR resolves inside libwow64fex.dll. prctl
// returns nonzero -> donor disables later VirtualName attempts (:162-169);
// madvise is an ignored THP hint (:173-175). ELR_EL1 already points at the
// instruction after SVC; sync_entry restores the modified frame + eret.
int sync_try_fex_linux_hint(u64 esr, u64 elr, u64 *regs) {
    if (((esr >> 26) & 0x3f) != 0x15) return 0; // AArch64 SVC
    if (regs[8] != 167 && regs[8] != 233) return 0;

    char name[64]; u64 off;
    if (!nt_image_resolve(elr, name, sizeof(name), &off)) return 0;
    const char expected[] = "libwow64fex.dll";
    int i = 0;
    while (expected[i] && name[i] == expected[i]) i++;
    if (expected[i] || name[i]) return 0;

    static int logged_prctl, logged_madvise;
    if (regs[8] == 167) {
        regs[0] = ~0ULL; // nonzero: disable optional VirtualName path
        if (!logged_prctl) {
            puts("CHALICE: FEX raw prctl VirtualName hint -> unsupported; resumed\n");
            logged_prctl = 1;
        }
    } else {
        regs[0] = 0; // advisory THP madvise accepted as a no-op
        if (!logged_madvise) {
            puts("CHALICE: FEX raw madvise THP hint -> no-op; resumed\n");
            logged_madvise = 1;
        }
    }
    return 1;
}

// CHALICE C4-15: demand-commit a reserved arena window on first data access.
// FEX intentionally reserves LookupCache's full sparse range, marks it for
// overcommit, then commits from its access-violation path (pinned donor
// FEXCore/Source/Interface/Core/LookupCache.cpp:27-49 and Source/Windows/
// Common/OvercommitTracker.h:31-53). GAIA doesn't yet deliver ARM64 data
// aborts as Windows user exceptions, so perform the same floor operation
// directly: back the containing arena window, retry the faulting instruction.
int sync_try_arena_demand_commit(u64 esr, u64 elr, u64 far) {
    u64 ec = (esr >> 26) & 0x3f, iss = esr & 0x1ffffff;
    unsigned long base = 0, size = 0; int committed = -2;
    puts("CHALICE ARENA: esr="); puthex(esr); puts(" ec="); puthex(ec);
    puts(" iss="); puthex(iss); puts(" elr="); puthex(elr);
    puts(" far="); puthex(far); puts("\n");
    nt_fault_region("elr", elr);
    nt_fault_region("far", far);
    if (ec != 0x24 && ec != 0x25) {
        puts("CHALICE ARENA: refused wrong-ec\n");
        return 0;
    }
    if (!nt_arena_contains(far)) {
        puts("CHALICE ARENA: refused not-arena\n");
        return 0;
    }
    nt_arena_query(far, &base, &size, &committed);
    if (committed < 0) {
        puts("CHALICE ARENA: refused not-reserved\n");
        return 0;
    }
    if (committed) {
        puts("CHALICE ARENA: refused already-committed\n");
        return 0;
    }
    if (!nt_arena_commit(far & ~0xfffULL, 0x1000)) {
        puts("CHALICE ARENA: refused commit-failed\n");
        return 0;
    }
    static unsigned long faults;
    faults++;
    puts("CHALICE ARENA: fired count="); puthex(faults); puts("; retry\n");
    return 1;
}

// CHALICE C4-18 — native ARM64 user-exception stack.
// Measured stock ntdll!KiUserExceptionDispatcher (RVA 0x66c40):
//   add x0, sp, #0x3b0 ; mov x1, sp ; bl dispatch_exception
// Thus sp+0x000=CONTEXT (0x390), +0x390=CONTEXT_EX (0x20),
// +0x3b0=EXCEPTION_RECORD64 (0x98), then Wine's 0x08 align + 0x10
// redzone -> total 0x460.  Receipt: wine/dlls/ntdll/unix/signal_arm64.c
// :140-150, :221-235, :753-779, :834-850; stock disasm in FEX-MAP.md.
#define ARM64_CONTEXT_SIZE       0x390
#define ARM64_CONTEXT_EX_OFF     0x390
#define ARM64_EXCEPTION_REC_OFF  0x3b0
#define ARM64_EXCEPTION_STACK    0x460
#define ARM64_CONTEXT_X_OFF      0x008
#define ARM64_CONTEXT_SP_OFF     0x100
#define ARM64_CONTEXT_PC_OFF     0x108
#define ARM64_CONTEXT_V_OFF      0x110
#define ARM64_CONTEXT_FPCR_OFF   0x310
#define ARM64_CONTEXT_FPSR_OFF   0x314
#define ARM64_CONTEXT_ALL        0x0040001fU
#define STATUS_ACCESS_VIOLATION  0xc0000005U  // wine/include/winnt.h:634
#define STATUS_BREAKPOINT        0x80000003U  // wine/include/winnt.h:630
#define STATUS_ILLEGAL_INSTRUCTION 0xc000001dU // wine/include/winnt.h:638
#define NTDLL_KIUSEREXCEPTIONDISPATCHER_RVA 0x66c40U

extern char _load_base[], _kernel_end[];
extern unsigned long *nt_exit_jmpbuf;
extern long nt_exit_code;
void nt_longjmp(unsigned long *buf, long val) __attribute__((noreturn));
void nt_exception_enter(void *stack, void *dispatcher) __attribute__((noreturn));

static void move_bytes(void *dst_void, const void *src_void, u64 n) {
    unsigned char *dst = (unsigned char *)dst_void;
    const unsigned char *src = (const unsigned char *)src_void;
    if (dst < src) for (u64 i = 0; i < n; i++) dst[i] = src[i];
    else if (dst > src) for (u64 i = n; i; i--) dst[i - 1] = src[i - 1];
}

static u64 guest_exception_stack(u64 interrupted_sp, const void *context_src,
                                 const void *record_src) {
    // Wine aligns down then reserves exactly struct exc_stack_layout (0x460).
    u64 stack = (interrupted_sp & ~15ULL) - ARM64_EXCEPTION_STACK;
    if (interrupted_sp < NT_RAM_BASE + ARM64_EXCEPTION_STACK ||
        interrupted_sp > NT_RAM_TOP || stack < NT_RAM_BASE) return 0;
    unsigned char *out = (unsigned char *)stack;
    for (u64 i = 0; i < ARM64_EXCEPTION_STACK; i += 8) *(u64 *)(out + i) = 0;
    if (context_src) move_bytes(out, context_src, ARM64_CONTEXT_SIZE);
    if (record_src) move_bytes(out + ARM64_EXCEPTION_REC_OFF, record_src, 0x98);

    // CONTEXT_EX: Wine context_init_empty_xstate(): all/legacy point back to
    // this CONTEXT; empty xstate points at the two-word redzone (+0x450).
    unsigned char *xctx = out + ARM64_CONTEXT_EX_OFF;
    *(u32 *)(xctx + 0x00) = (u32)-ARM64_CONTEXT_SIZE;
    *(u32 *)(xctx + 0x04) = 0x450;
    *(u32 *)(xctx + 0x08) = (u32)-ARM64_CONTEXT_SIZE;
    *(u32 *)(xctx + 0x0c) = ARM64_CONTEXT_SIZE;
    *(u32 *)(xctx + 0x10) = 0xc0;
    *(u32 *)(xctx + 0x14) = 0;
    return stack;
}

static u64 ki_user_exception_dispatcher(void) {
    u64 base = 0, size = 0;
    if (!nt_image_base_by_name("ntdll.dll", &base, &size) ||
        size <= NTDLL_KIUSEREXCEPTIONDISPATCHER_RVA) return 0;
    return base + NTDLL_KIUSEREXCEPTIONDISPATCHER_RVA;
}

// Return a dispatcher stack pointer, zero for kernel/unknown faults.  boot.S
// keeps demand-commit ahead of this function, so an arena first-touch retries
// transparently; a COMMITTED JIT HLT reaches the guest here.
u64 sync_try_guest_exception(u64 esr, u64 elr, u64 far, u64 sp_orig, u64 *regs) {
    u64 ec = (esr >> 26) & 0x3f, status = 0, params = 0, dispatcher, stack;
    u32 insn = 0;
    int known_guest = nt_image_resolve(elr, (char[1]){0}, 1, &(u64){0}) ||
                      nt_arena_contains(elr) || nt_vm_region_query(elr, 0, 0);
    if (elr >= (u64)_load_base && elr < (u64)_kernel_end) return 0;
    if (!known_guest) return 0;

    if (ec == 0x24 || ec == 0x25) {                 // data abort, lower/current EL
        status = STATUS_ACCESS_VIOLATION;
        params = 2;
    } else if (ec == 0x3c && nt_fault_read32(elr, &insn) &&
               (insn & 0xffe0001fU) == 0xd4200000U) {
        status = STATUS_BREAKPOINT;
    } else if (ec == 0x00 && nt_fault_read32(elr, &insn) &&
               (insn & 0xffe0001fU) == 0xd4400000U) {
        status = STATUS_ILLEGAL_INSTRUCTION;
    } else return 0;

    dispatcher = ki_user_exception_dispatcher();
    if (!dispatcher) return 0;
    stack = guest_exception_stack(sp_orig, 0, 0);
    if (!stack) return 0;
    unsigned char *ctx = (unsigned char *)stack;
    unsigned char *rec = ctx + ARM64_EXCEPTION_REC_OFF;
    *(u32 *)(ctx + 0x00) = ARM64_CONTEXT_ALL;
    u64 spsr, fpcr, fpsr;
    asm volatile("mrs %0, spsr_el1\n mrs %1, fpcr\n mrs %2, fpsr"
                 : "=r"(spsr), "=r"(fpcr), "=r"(fpsr));
    *(u32 *)(ctx + 0x04) = (u32)spsr;
    for (u64 i = 0; i < 31; i++) *(u64 *)(ctx + ARM64_CONTEXT_X_OFF + i * 8) = regs[i];
    *(u64 *)(ctx + ARM64_CONTEXT_SP_OFF) = sp_orig;
    *(u64 *)(ctx + ARM64_CONTEXT_PC_OFF) = elr;
    move_bytes(ctx + ARM64_CONTEXT_V_OFF, (unsigned char *)regs + 0x100, 0x200);
    *(u32 *)(ctx + ARM64_CONTEXT_FPCR_OFF) = (u32)fpcr;
    *(u32 *)(ctx + ARM64_CONTEXT_FPSR_OFF) = (u32)fpsr;

    *(u32 *)(rec + 0x00) = (u32)status;
    *(u64 *)(rec + 0x10) = elr;
    *(u32 *)(rec + 0x18) = (u32)params;
    if (params) {
        *(u64 *)(rec + 0x20) = (esr >> 6) & 1;     // ESR.ISS.WnR
        *(u64 *)(rec + 0x28) = far;
    }
    puts("CHALICE EXCEPTION: code="); puthex(status);
    puts(" elr="); puthex(elr); puts(" dispatch="); puthex(dispatcher); puts("\n");
    asm volatile("msr elr_el1, %0" :: "r"(dispatcher));
    return stack;
}

// NtRaiseException already supplies a complete ARM64 CONTEXT + RECORD.  Use
// the identical dispatcher stack contract rather than ending the process.
void nt_raise_guest_exception(void *record, void *context) {
    u64 dispatcher = ki_user_exception_dispatcher();
    u64 stack = context ? guest_exception_stack(*(u64 *)((unsigned char *)context + ARM64_CONTEXT_SP_OFF), context, record) : 0;
    if (!dispatcher || !stack) {
        puts("CHALICE: NtRaiseException dispatch setup failed\n");
        nt_exit_code = STATUS_ILLEGAL_INSTRUCTION;
        if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
        for (;;) asm volatile("wfe");
    }
    nt_exception_enter((void *)stack, (void *)dispatcher);
}

// CHALICE C3c Atom 3C-2a: sync_entry (boot.S) now hands over esr/elr/far/
// orig-sp/regs-frame-ptr -- turn a silent CPU fault into one dense serial
// dump instead of the old bare "!SYNC esr=.. elr=.." line.
// CHALICE C3c-2f p1: regs[] is now the SAVEALL_SYNC frame (boot.S): full
// x0-x30, 8B slots, word i == xN for N<=29, word 30 == x30 -- simpler,
// uniform mapping vs the old x0-x18,x29,x30-only frame.
void sync_handler(u64 esr, u64 elr, u64 far, u64 sp_orig, u64 *regs) {
    u64 ec = (esr >> 26) & 0x3F, iss = esr & 0x1FFFFFF, spsr;
    asm volatile("mrs %0, spsr_el1" : "=r"(spsr));
    puts("\nCHALICE FAULT raw-esr="); puthex(esr);
    puts(" ec="); puthex(ec); puts(" iss="); puthex(iss);
    puts(" elr="); puthex(elr); puts(" far="); puthex(far);
    puts(" spsr="); puthex(spsr); puts(" sp="); puthex(sp_orig);
    unsigned int insn;
    if (nt_fault_read32(elr, &insn)) { puts(" insn="); puthex(insn); }
    else puts(" insn=unreadable");
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
    resolve_line("x1 ", regs[1]);
    resolve_line("x8 ", regs[8]);
    resolve_line("x9 ", regs[9]);
    resolve_line("x16", regs[16]);
    resolve_line("x18", regs[18]);
    resolve_line("x21", regs[21]);

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

    // ---- CHALICE C3c-2p: real fp-chain backtrace (walk x29, not a blind
    // stack scan). AAPCS64 standard frame layout is [x29+0]=saved (caller's)
    // fp, [x29+8]=saved lr -- LLVM's ARM64 codegen (Wine's ntdll/kernelbase
    // build, and our own zig-cc PE builds) keeps this chain intact unless a
    // leaf function omitted a frame, so following it gives real caller PCs
    // instead of whatever happens to sit in the next 64 bytes of stack.
    // Guarded (range + monotonic-increase check) since this walks PE-side
    // data at a fault site -- a garbage/cyclic fp must not re-fault the
    // kernel or hang this dump; it just stops the walk early instead. ----
    puts("  -- fp-chain backtrace (x29 walk) --\n");
    u64 fp = regs[29];
    for (int i = 0; i < 8; i++) {
        if (!fp || fp < NT_RAM_BASE || fp >= NT_RAM_TOP || (fp & 7)) {
            puts("  [fp "); putdec(i); puts("] fp="); puthex(fp);
            puts(" -> out of plausible RAM range or misaligned, stop\n");
            break;
        }
        u64 saved_fp = ((u64 *)fp)[0];
        u64 saved_lr = ((u64 *)fp)[1];
        puts("  [fp "); putdec(i); puts("] fp="); puthex(fp); puts("\n");
        resolve_line("    lr", saved_lr);
        if (saved_fp <= fp) {
            puts("  (next fp non-increasing, stop)\n");
            break;
        }
        fp = saved_fp;
    }

    // ---- CHALICE C3c-2p: root_key_names[] relocation spot-check.
    // kernelbase.dll's donor registry.c table (root_key_names[], indices
    // 0..6: L"\Registry\Machine", L"\Registry\User", ...) lives at RVA
    // 0x11cc18 (donor ImageBase 0x180000000) -- read entries [0] and [1]
    // straight out of THIS boot's actual mapped image (base varies run to
    // run, DLL-territory bump allocator) rather than the on-disk file, so
    // this proves whether base-relocation ran correctly. Whole 1GB RAM
    // block is identity-mapped (kmain.c mmu_on) so a plain C dereference
    // is safe once bounds-checked into that block. ----
    puts("  -- root_key_names[] relocation spot-check (kernelbase.dll+0x11cc18) --\n");
    {
        u64 kb_base = 0, kb_size = 0;
        if (nt_image_base_by_name("kernelbase.dll", &kb_base, &kb_size)) {
            u64 table = kb_base + 0x11cc18ULL;
            puts("  kernelbase.dll base="); puthex(kb_base);
            puts(" table="); puthex(table); puts("\n");
            if (table >= NT_RAM_BASE && table + 16 < NT_RAM_TOP) {
                u64 *tp = (u64 *)table;
                for (int i = 0; i < 2; i++) {
                    u64 entry = tp[i];
                    puts("  root_key_names["); putdec(i); puts("]="); puthex(entry);
                    if (entry >= NT_RAM_BASE && entry < NT_RAM_TOP && !(entry & 1)) {
                        puts(" -> \"");
                        unsigned short *ws = (unsigned short *)entry;
                        for (int k = 0; k < 24; k++) {
                            unsigned short c = ws[k];
                            if (c == 0) break;
                            putc((c >= 0x20 && c < 0x7f) ? (char)c : '.');
                        }
                        puts("\"\n");
                    } else {
                        puts(" -> (out of RAM range or misaligned, not dereferenced)\n");
                    }
                }
            } else {
                puts("  table address out of plausible RAM range, skipped\n");
            }
        } else {
            puts("  kernelbase.dll not found in image registry\n");
        }
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
