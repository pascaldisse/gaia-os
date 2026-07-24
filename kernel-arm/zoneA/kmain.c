// GAIA-OS Zone A — M2: MMU + GICv3 + timer + MAlloc + cooperative tasks.
// C for the stub; the kernel speaks HolyC once Zone C lands.
#include "kernel.h"
#include "sync_frame.h"

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
// CHALICE C4-28 fix: volatile -- stops the compiler fusing adjacent
// element stores into a single stp when source layout leaves an odd
// (8-byte-only-aligned) starting offset into this 16B-aligned array; see
// mmu_on() below for the full live-caught fault story (Device-memory-
// alignment vs MMU-off boot state). Plain compiler barriers around the
// call sites reduced but did not fully suppress it once the compiler
// re-introduced the same fusion inside a loop's own unrolling; volatile
// is the actual C-semantics guarantee (no store/load may be elided,
// reordered across, or fused with another volatile access).
static volatile u64 l1_table[512] __attribute__((aligned(4096)));
// CHALICE C4-28 -- guard page(s) for [0x0,0x200000): mmu_on used to block-map
// the WHOLE first 1GB (incl. address 0) as valid device nGnRnE, so a guest
// null-pointer dereference silently read real device memory instead of
// faulting. FEX's own GuestSignal_SIGSEGV dispatcher marker (FEXCore
// Dispatcher.cpp:414-430, "Force a SIGSEGV by loading zero") depends on
// address 0 being genuinely unmapped, exactly like real hardware/Windows;
// on this floor it silently no-op'd, execution fell through into the
// immediately-following (Linux-signal-only, never Windows-serviced)
// ThreadPauseHandlerAddress/PauseReturnInstruction block, and THAT hlt is
// what previously misreported as dispatcher+0x2F0/"SignalHandlerReturnAddress"
// (see C4-28 HANDOFF/FEX-MAP receipt -- that identity was mis-pinned; the
// real SignalHandlerReturnAddress/RT pair lives at dispatcher+0x218/+0x21C).
// l1_table[0] now points at this L2 table instead of being a 1GB block
// descriptor directly: entry[0] (covering [0x0,0x200000)) stays fully
// invalid; entries[1..511] keep the identical device-nGnRnE 2MB block
// attributes the removed 1GB block used, so GICD(0x08000000)/GICR(0x080A0000)
// /UART(0x09000000)/VIRTIO(0x0a000000) -- all >= 2MB -- are unaffected.
static volatile u64 l2_dev_table[512] __attribute__((aligned(4096)));

static void mmu_on(void) {
    // CHALICE C4-28 fix (gate salvage): looping i=1..511 (skipping index 0)
    // left the compiler free to pair adjacent stores (stp) starting at an
    // 8-byte-only-aligned offset (l2_dev_table+8) straddling a 16B boundary.
    // With the MMU still off at this point in boot, EVERY access defaults to
    // Device-nGnRnE (SCTLR_EL1.M=0 forces the whole space Device, ARM ARM),
    // and Device memory requires natural alignment even for a plain stp ->
    // ESR ec=0x25 DFSC=0x21 (Alignment fault) at l2_dev_table[1], crashing
    // boot before "MMU: on" ever printed (live-caught this pass). Fix: loop
    // uniformly over the WHOLE array from i=0 (naturally 16B-pair-aligned,
    // same shape the working pre-guard single-block code had), THEN
    // overwrite index 0 as one isolated scalar store after the loop -- a
    // lone `str` only needs its own 8-byte natural alignment, always true
    // at the array's 4096-aligned base+0.
    for (u64 i = 0; i < 512; i++) {
        u64 addr = i * 0x200000ULL;
        l2_dev_table[i] = addr | (0ULL << 2) | (1ULL << 10) | 1ULL;              // device nGnRnE, L2 block
    }
    // Source-order alone did NOT stop the compiler re-pairing this isolated
    // store with the loop's own stores (no data dependency forces ordering,
    // and with no MMU on yet, the compiler has no reason to know Device
    // memory requires natural stp alignment here) -- live-reproduced TWICE
    // (l2_dev_table+8, then l1_table+8 below) even after reordering to
    // "loop first, odd store last". A real compiler barrier is required,
    // not just source-code sequencing.
    asm volatile("" ::: "memory");
    l2_dev_table[0] = 0;                                                          // [0x0,0x200000) unmapped -- guard page (isolated store, after the loop)
    asm volatile("" ::: "memory");
    // CHALICE C4-28 fix, part 2: identical hazard hit l1_table (the
    // l1_table[0] singular write, even moved after the RAM loop in source
    // order, still got compiler-paired with l1_table[1] absent a barrier).
    for (u64 i = 0; i < NT_RAM_BLOCKS; i++) {
        u64 base = NT_RAM_BASE + i * 0x40000000ULL;
        l1_table[1 + i] = base | (1ULL << 2) | (3ULL << 8) | (1ULL << 10) | 1ULL;  // normal WBWA ISH
    }
    asm volatile("" ::: "memory");
    l1_table[0] = (u64)l2_dev_table | 0b11ULL;                                   // L1 table entry -> l2_dev_table (isolated store, after the loop)
    asm volatile("" ::: "memory");

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
// CHALICE C4-59: shared State.rip reader + cycle counter (ntfile.c/h) --
// same file-local-prototype convention as the declarations right above.
int nt_wow64_live_state_rip(unsigned long teb, unsigned long *rip_out);
extern unsigned long nt_c459_cycle;

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
    // CHALICE C4-38 measurement (no fix, diagnostic-only): decode DFSC/WnR,
    // dump the actual L1 leaf descriptor covering `far` (TTBR0_EL1 walk,
    // T0SZ=25 -> IA=39 bits -> L1 index = far[38:30], matches mmu_on()'s
    // single-level 1GB block scheme, no L2/L3 involved for RAM), print
    // MAIR_EL1 + the AttrIndx field's byte, and SCTLR_EL1.A (alignment
    // check enable) -- ESR ec=0x25 iss=0x61 -> DFSC=iss&0x3f=0x21 which per
    // ARM ARM D17.2.28 ESR_EL1 Data-Abort DFSC encoding is ALIGNMENT FAULT
    // (0b100001), NOT a translation fault (those are 0b0001xx, DFSC 0x04-
    // 0x07) -- C4-37's own framing of this as a translation-fault-vs-
    // committed-memory gap was a misread of the ISS field; corrected here.
    {
        u64 dfsc = iss & 0x3f, wnr = (iss >> 6) & 1;
        u64 ttbr0, mair, sctlr, l1idx, l1desc;
        asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
        asm volatile("mrs %0, mair_el1"  : "=r"(mair));
        asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        l1idx = (far >> 30) & 0x1ff;
        l1desc = *(volatile u64 *)((ttbr0 & ~0xfffULL) + l1idx * 8);
        u64 attrindx = (l1desc >> 2) & 0x7;
        u64 mair_byte = (mair >> (attrindx * 8)) & 0xff;
        puts("CHALICE C4-38 ESR: dfsc="); puthex(dfsc);
        puts(" wnr="); puthex(wnr);
        puts(" (dfsc 0x21==ALIGNMENT FAULT per ARM ARM, not translation)\n");
        puts("CHALICE C4-38 PTE: ttbr0="); puthex(ttbr0);
        puts(" l1idx="); puthex(l1idx);
        puts(" l1desc="); puthex(l1desc);
        puts(" attrindx="); puthex(attrindx);
        puts(" mair_el1="); puthex(mair);
        puts(" mair_byte="); puthex(mair_byte);
        puts(mair_byte == 0xff ? " (Normal WBWA)" : (mair_byte == 0x00 ? " (Device-nGnRnE)" : " (OTHER)"));
        puts("\n");
        puts("CHALICE C4-38 SCTLR: sctlr_el1="); puthex(sctlr);
        puts(" A-bit(alignment-check-enable)="); puthex((sctlr >> 1) & 1);
        puts("\n");
    }
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

// CHALICE C4-68 — hardware write-watchpoint on FEXCore's own
// CpuStateFrame::State.rip (CurrentFrame+0x18, the C4-59/61/63-established
// offset), armed the moment CurrentFrame first becomes known (ntsvc.c's
// nt_dispatch_log, reusing C4-64's own "CurrentFrame known" gate -- see
// nt_dbg_arm_watchpoint below), to name the exact host instruction that
// zeros it (the C4-58-through-C4-67 "zeroing event", already proven
// upstream of every floor-satisfiable candidate this thread has named).
// ARMv8 DBGWVR0_EL1/DBGWCR0_EL1. This floor is EL1-only -- boot.S's vector
// table never uses a lower-EL entry, every exception (kernel C, FEX's own
// native code, the guest's own JIT'd x86) is delivered at the SAME EL1,
// curEL-SPx vectors -- so the watchpoint is configured EL1-only:
// {HMC=0, SSC=0b00, PAC=0b01} per ARM ARM D2.10.1's control-field table
// (== Linux arch/arm64/kernel/hw_breakpoint.c's own ARM_BREAKPOINT_EL1
// encoding), LSC=0b10 (store-only, matches the task's own "write-
// watchpoint" spec), BAS=0xFF (whole aligned doubleword -- CurrentFrame+
// 0x18 is 8-aligned, matches CoreState.h's own alignas(8) on this field).
// MDSCR_EL1.MDE (global monitor-debug enable) AND .KDE (EL1 debug enable,
// required in addition to MDE for a PAC=EL1-only watchpoint to actually
// fire while the PE is running AT EL1 -- same Linux convention) both set.
#define C468_DBGWCR_VAL 0x1FF3ULL // E=1 PAC=01(EL1) LSC=10(store) BAS=0xFF HMC=0 SSC=00
static u64 c468_watch_addr;    // CurrentFrame+0x18, latched once; 0=unarmed
static int c468_step_pending;  // mid step-over-the-culprit-instruction dance
static u64 c468_hit_count;
static u64 c468_zero_readback_count;
// Snapshot of the CULPRIT instruction's own elr + x0-x30, taken at the
// INITIAL watchpoint hit (before the step-over dance re-executes it) --
// this is what a "post-step read-back==0" full dump actually wants to
// show (the instruction that did the zeroing), not the step-back trap's
// own elr (which has already advanced to the NEXT instruction).
static u64 c468_saved_elr;
static u64 c468_saved_regs[31];

// Called from ntsvc.c's nt_dispatch_log, the SAME "CurrentFrame known" gate
// C4-64's own probe already uses (nt_wow64_ready && nt_wow64_live_state_
// probe returns nonzero) -- arms exactly once, the FIRST time this boot
// ever learns CurrentFrame, never before (per this atom's own design law).
// Idempotent past the first call, so the caller needs no latch of its own.
void nt_dbg_arm_watchpoint(unsigned long current_frame) {
    if (c468_watch_addr || !current_frame) return;
    c468_watch_addr = current_frame + 0x18; // CpuStateFrame::State.rip
    asm volatile("msr dbgwvr0_el1, %0" :: "r"(c468_watch_addr));
    asm volatile("msr dbgwcr0_el1, %0" :: "r"((u64)C468_DBGWCR_VAL));
    u64 mdscr;
    asm volatile("mrs %0, mdscr_el1" : "=r"(mdscr));
    mdscr |= (1ULL << 15) | (1ULL << 13); // MDE | KDE
    asm volatile("msr mdscr_el1, %0" :: "r"(mdscr));
    asm volatile("isb");
    puts("CHALICE C4-68: watchpoint ARMED addr="); puthex(c468_watch_addr);
    puts(" (CurrentFrame="); puthex(current_frame); puts(")\n");
}

// Full x0-x30 + 64-byte code-window dump at the SAVED culprit elr/regs --
// same shape as C4-61's own elr==0x26D0C02B0-gated dump, so the two
// atoms' receipts are directly diffable.
static void c468_full_dump(void) {
    puts("CHALICE C4-68 FULL REGS @culprit elr="); puthex(c468_saved_elr); puts(":\n");
    for (int i = 0; i <= 28; i += 4) {
        puts("  x"); putdec(i); puts("="); puthex(c468_saved_regs[i]);
        if (i + 1 <= 28) { puts(" x"); putdec(i+1); puts("="); puthex(c468_saved_regs[i+1]); }
        if (i + 2 <= 28) { puts(" x"); putdec(i+2); puts("="); puthex(c468_saved_regs[i+2]); }
        if (i + 3 <= 28) { puts(" x"); putdec(i+3); puts("="); puthex(c468_saved_regs[i+3]); }
        puts("\n");
    }
    puts("  x29(fp)="); puthex(c468_saved_regs[29]); puts(" x30(lr)="); puthex(c468_saved_regs[30]); puts("\n");
    puts("CHALICE C4-68 CODE WINDOW @culprit elr..+64:");
    for (long w = 0; w < 64; w += 4) {
        u64 a = c468_saved_elr + w; u32 word;
        puts(" ");
        if (nt_fault_read32(a, &word)) puthex(word);
        else puts("????????");
    }
    puts("\n");
}

// CHALICE C4-68 — the watchpoint (EC 0x34/0x35) + software-step (EC 0x32/
// 0x33) synchronous-exception handler. Called FIRST in sync_entry (boot.S),
// ahead of every other sync_try_* triage function -- these two ESR classes
// are ARM64-native debug events, never SVC/data-abort/undefined, so there
// is zero overlap with any other handler's own ec gate; ordering is chosen
// purely to keep this atom's own logic isolated and easy to reason about.
// `regs` = pointer to the pushed SAVEALL_SYNC frame (x0-x30), same
// convention sync_try_fex_linux_hint/sync_try_guest_exception already use
// -- untouched by this handler (unlike sync_try_guest_exception's own
// guest-stack CONTEXT write), so it's safe to read directly, no snapshot
// needed except the deliberate cross-exception c468_saved_* stash above.
// Returns 1 (handled -- boot.S's `cbnz x0,2f` path: RESTOREALL_SYNC; eret
// to the CURRENT ELR_EL1, unmodified) or 0 (not this atom's ESR class).
int sync_try_watchpoint(u64 esr, u64 elr, u64 far, u64 *regs) {
    u64 ec = (esr >> 26) & 0x3f;
    if (ec == 0x34 || ec == 0x35) {          // Watchpoint, lower/current EL
        c468_hit_count++;
        unsigned int pre_lo = 0, pre_hi = 0;
        int pre_ok = nt_fault_read32(far, &pre_lo) && nt_fault_read32(far + 4, &pre_hi);
        u64 pre_val = pre_ok ? (((u64)pre_hi << 32) | pre_lo) : ~0ULL;

        // Stash the culprit instruction's own PC + full GPR file NOW,
        // before the step-over dance below re-executes it -- this is what
        // a later "post-step==0" full dump needs (see c468_full_dump).
        c468_saved_elr = elr;
        for (int i = 0; i < 31; i++) c468_saved_regs[i] = regs[i];

        if (c468_hit_count <= 200) {
            puts("CHALICE C4-68 WATCHPOINT #"); puthex(c468_hit_count);
            puts(" elr="); puthex(elr); puts(" far="); puthex(far);
            puts(" x28="); puthex(regs[28]);
            puts(" pre-write-val="); if (pre_ok) puthex(pre_val); else puts("UNREADABLE");
            nt_fault_region("elr", elr);
            puts("\n");
        }

        // Step-over dance (task design step 2): disable this watchpoint,
        // arm one software step, eret back to the SAME elr (ELR_EL1 is
        // unmodified -- this is the instruction that already generated
        // the watchpoint) so it re-executes exactly once under single-
        // step, then re-traps via the EC 0x32/0x33 branch below, where the
        // watchpoint is re-armed.
        asm volatile("msr dbgwcr0_el1, %0" :: "r"(0ULL)); // E=0, disable
        u64 mdscr;
        asm volatile("mrs %0, mdscr_el1" : "=r"(mdscr));
        mdscr |= (1ULL << 0); // MDSCR_EL1.SS
        asm volatile("msr mdscr_el1, %0" :: "r"(mdscr));
        u64 spsr;
        asm volatile("mrs %0, spsr_el1" : "=r"(spsr));
        spsr |= (1ULL << 21); // PSTATE.SS
        asm volatile("msr spsr_el1, %0" :: "r"(spsr));
        asm volatile("isb");
        c468_step_pending = 1;
        return 1;
    }
    if ((ec == 0x32 || ec == 0x33) && c468_step_pending) { // Software Step
        c468_step_pending = 0;
        unsigned int post_lo = 0, post_hi = 0;
        int post_ok = nt_fault_read32(c468_watch_addr, &post_lo) &&
                      nt_fault_read32(c468_watch_addr + 4, &post_hi);
        u64 post_val = post_ok ? (((u64)post_hi << 32) | post_lo) : ~0ULL;
        int is_zero = post_ok && post_val == 0;
        if (is_zero) c468_zero_readback_count++;

        if (c468_hit_count <= 200 || is_zero) {
            puts("CHALICE C4-68 STEP-BACK #"); puthex(c468_hit_count);
            puts(" post-write-val="); if (post_ok) puthex(post_val); else puts("UNREADABLE");
            puts(" zero_total="); puthex(c468_zero_readback_count);
            puts("\n");
        }
        if (is_zero) {
            // Rate-cap-bypass: this is the exact event C4-68 exists to
            // catch -- full detail, unconditionally, every single time.
            c468_full_dump();
        }

        // Re-arm: watchpoint back on, single-step back off. PSTATE.SS was
        // already cleared to 0 by hardware on taking THIS exception (ARM
        // ARM D2.12.3) -- do not set it again, or every future instruction
        // would trap.
        asm volatile("msr dbgwcr0_el1, %0" :: "r"((u64)C468_DBGWCR_VAL));
        u64 mdscr2;
        asm volatile("mrs %0, mdscr_el1" : "=r"(mdscr2));
        mdscr2 &= ~(1ULL << 0); // clear MDSCR_EL1.SS
        asm volatile("msr mdscr_el1, %0" :: "r"(mdscr2));
        asm volatile("isb");
        return 1;
    }
    return 0;
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
// Wine save_context() delivers CONTEXT_FULL, not debug/X18 requests.
#define ARM64_CONTEXT_FULL       0x00400007U
#define STATUS_ACCESS_VIOLATION  0xc0000005U  // wine/include/winnt.h:634
#define STATUS_BREAKPOINT        0x80000003U  // wine/include/winnt.h:630
#define STATUS_ILLEGAL_INSTRUCTION 0xc000001dU // wine/include/winnt.h:638
// CHALICE C4-40: donor wine/include/winnt.h:629, ntstatus.h:223 -- ARM ARM
// D17.2.28 ESR_EL1 Data-Abort DFSC=0b100001 (0x21) is the alignment-fault
// encoding; wine/dlls/ntdll/unix/signal_arm64.c segv_handler routes that
// DFSC straight to this code, SKIPPING the AV/read-write classification.
#define STATUS_DATATYPE_MISALIGNMENT 0x80000002U // wine/include/winnt.h:629
#define ESR_DFSC_ALIGN_FAULT 0x21U
#define NTDLL_KIUSEREXCEPTIONDISPATCHER_RVA 0x66c40U

// Matches SAVEALL_SYNC/RESTOREALL_SYNC.  Any future C/asm layout drift is a
// compile error rather than a silent exception-delivery loop.
struct sync_frame_layout {
    unsigned char gpr[SYNC_FRAME_GPR_BYTES];
    unsigned char q[SYNC_FRAME_Q_BYTES];
};
typedef char sync_frame_size_must_match_asm[
    sizeof(struct sync_frame_layout) == SYNC_FRAME_SIZE ? 1 : -1];
typedef char sync_frame_q_must_match_asm[
    __builtin_offsetof(struct sync_frame_layout, q) == SYNC_FRAME_Q_OFF ? 1 : -1];
typedef char sync_frame_x18_must_match_asm[
    SYNC_FRAME_X18_OFF == 18 * sizeof(u64) ? 1 : -1];
typedef char arm64_context_x18_must_match_asm[
    ARM64_CONTEXT_X18_OFF == ARM64_CONTEXT_X_OFF + 18 * sizeof(u64) ? 1 : -1];

extern char _load_base[], _kernel_end[];
extern unsigned long *nt_exit_jmpbuf;
extern long nt_exit_code;
void nt_longjmp(unsigned long *buf, long val) __attribute__((noreturn));
void nt_exception_enter(void *stack, void *dispatcher) __attribute__((noreturn));

void nt_continue_trace(void *context, u64 teb) {
    // CHALICE C4-59: uncapped -- fires every NtContinue call (FEX's own
    // internal self-heal resume for every 0x26D0C02B0-class cycle, C4-58's
    // own "12x CHALICE CONTINUE" count was an ARTIFACT of the pre-existing
    // 12-call print cap below, not the true call count). `teb` is the
    // CALLER's (FEX-internal native code's) OWN live x18 at NtContinue's
    // entry, passed in from ntdisp.S BEFORE this floor's CONTEXT restore
    // touches x18 -- same ABI convention (x18==guest TEB, preserved across
    // FEX's own internal calls) every other syscall-side reader in this
    // codebase already relies on. Logged UNCONDITIONALLY, ahead of the old
    // capped print below, so this floor can see live State.rip at every
    // single self-heal resume, not just the first 12.
    {
        static u64 c459_count;
        unsigned long state_rip = ~0UL;
        int ok = nt_wow64_live_state_rip((unsigned long)teb, &state_rip);
        c459_count++;
        puts("CHALICE C4-59: NtContinue #"); puthex((u64)c459_count);
        puts(" teb="); puthex(teb);
        puts(" live-State.rip="); puts(ok ? "" : "(unreadable)"); if (ok) puthex((u64)state_rip);
        puts("\n");
    }
    static u64 count;
    if (count++ >= 12 || !context) return;
    unsigned char *ctx = (unsigned char *)context;
    puts("CHALICE CONTINUE: pc="); puthex(*(u64 *)(ctx + 0x108));
    puts(" sp="); puthex(*(u64 *)(ctx + 0x100));
    puts(" x18="); puthex(*(u64 *)(ctx + 0x98)); puts("\n");
}

// C4-20 assembly handoff: sync_entry reloads this target and writes ELR_EL1
// immediately before ERET, after all C work has completed.
volatile u64 sync_guest_exception_elr;
// C4-21 → sync_entry runs C on this stack; delivery lives below guest SP.
unsigned char sync_exception_stack[16384] __attribute__((aligned(16)));
// C4-21 zeroer bracket → live delivered CONTEXT; nt_dispatch_log samples it.
volatile unsigned char *nt_exception_context_watch;

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
    // C4-20 fallback context probe: QEMU's bundled RSP advertises neither
    // swbreak nor hwbreak, so retain the before-delivery bytes and inspect
    // that same stack slot on the next delivery before overwriting it.
    static unsigned char prior_context[ARM64_CONTEXT_SIZE];
    static u64 prior_context_addr;
    static int prior_context_valid, context_return_logged;
    u64 ec = (esr >> 26) & 0x3f, status = 0, params = 0, dispatcher, stack;
    u32 insn = 0;
    int known_guest = nt_image_resolve(elr, (char[1]){0}, 1, &(u64){0}) ||
                      nt_arena_contains(elr) || nt_vm_region_query(elr, 0, 0);
    if (elr >= (u64)_load_base && elr < (u64)_kernel_end) return 0;
    if (!known_guest) return 0;

    if ((ec == 0x24 || ec == 0x25) && (esr & 0x3f) == ESR_DFSC_ALIGN_FAULT) {
        // CHALICE C4-40 fix (named recon, C4-39): donor segv_handler
        // (signal_arm64.c ~1123-1128) checks DFSC==ALIGN_FAULT BEFORE the
        // generic Data-Abort read/write classification and returns a bare
        // EXCEPTION_DATATYPE_MISALIGNMENT with NumberParameters==0 (never
        // falls through to the ExceptionInformation[0/1]=WnR/addr code a
        // few lines below in the SAME donor switch case) -- mirror that
        // exact shape: no ExceptionInformation params, so FEX's own
        // BTCpuResetToConsistentStateImpl (Module.cpp:926) sees
        // ExceptionCode==EXCEPTION_DATATYPE_MISALIGNMENT (not
        // EXCEPTION_ACCESS_VIOLATION), skips the whole AV-shaped branch
        // above it (CallRetStack/OvercommitTracker/suspend/JITGuardPage/
        // InvalidationTracker -- none apply, none read ExceptionInformation
        // for this code anyway) and reaches its
        // `Exception->ExceptionCode == EXCEPTION_DATATYPE_MISALIGNMENT &&
        // Context::HandleUnalignedAccess(TLS, Context)` gate, which decodes
        // the faulting ldar/stlr at Context->Pc (== our `elr`, wired below
        // via rec+0x10) directly from the CONTEXT register file already
        // built for every other path here -- no other record field this
        // consumer reads.
        status = STATUS_DATATYPE_MISALIGNMENT;
        params = 0;
    } else if (ec == 0x24 || ec == 0x25) {           // data abort, lower/current EL
        status = STATUS_ACCESS_VIOLATION;
        params = 2;
    } else if (ec == 0x3c && nt_fault_read32(elr, &insn) &&
               (insn & 0xffe0001fU) == 0xd4200000U) {
        status = STATUS_BREAKPOINT;
    } else if (ec == 0x00 && nt_fault_read32(elr, &insn) &&
               (insn & 0xffe0001fU) == 0xd4400000U) {
        status = STATUS_ILLEGAL_INSTRUCTION;
    } else return 0;

    // CHALICE C4-30 MEASURE step 1: the ORIGINAL fault anatomy, at OUR OWN
    // delivery point, before FEX's own BTCpuResetToConsistentStateImpl ever
    // gets a chance to reconstruct/anchor a NEW record on top of it. This is
    // NOT the RAISE#1 post-reset identity (ntsvc.c NtRaiseException, always
    // libwow64fex+0x34E4/BTCpuSimulate+0x10 by FEX's own design,
    // Module.cpp:757-777/947 -- memcpy'd over the real native Context right
    // before that syscall) -- this is the raw ARM64 hardware trap sync_entry
    // (boot.S) just delivered straight from ESR_EL1/ELR_EL1/FAR_EL1, before
    // any guest code (FEX included) has touched it. WnR = ESR.ISS bit 6 (ARM
    // ARM D13.2.37 ESR_EL1.ISS{DFSC,WnR,...} for a Data Abort). Region
    // classification reuses the existing nt_fault_region classifier
    // (ntfile.c) already used by the arena demand-commit path -- run here
    // against BOTH elr and far, not just far, so a fault whose PC (not just
    // its data address) lands in an unexpected class is equally visible.
    {
        u64 wnr = (esr >> 6) & 1;
        puts("CHALICE C4-30 ORIGINAL FAULT: esr="); puthex(esr);
        puts(" ec="); puthex(ec); puts(" status="); puthex(status);
        puts(" elr="); puthex(elr); puts(" far="); puthex(far);
        puts(" wnr="); puthex(wnr); puts("\n");
        nt_fault_region("elr-orig", elr);
        nt_fault_region("far-orig", far);
        // CHALICE C4-30: raw instruction window [elr-32,elr+32) around the
        // ORIGINAL fault PC -- this fault sits inside a JIT-generated
        // reserve-commit-arena region (FEX's own dispatcher trampoline
        // class, per C4-17/18/21's dump of the SAME class of 0x4000-byte
        // arena), not the static libwow64fex.dll image, so map-fex-offsets.sh
        // cannot resolve it -- dump the raw ARM64 words instead so they can
        // be hand-disassembled (llvm-mc --disassemble --triple=aarch64)
        // against this pass's own transcript.
        puts("CHALICE C4-30 ELR WINDOW @elr-32..+32:");
        for (long w = -32; w < 32; w += 4) {
            u64 a = elr + w; u32 word;
            puts(" ");
            if (nt_fault_read32(a, &word)) puthex(word);
            else puts("????????");
        }
        puts("\n");
    }

    dispatcher = ki_user_exception_dispatcher();
    if (!dispatcher) return 0;

    // CHALICE C4-23 -- guest_exception_stack() below zero-fills the full
    // 0x460-byte CONTEXT/EX/RECORD region ending at orig_sp. SYNC_FRAME_SIZE
    // (0x300, boot.S SAVEALL_SYNC) < ARM64_EXCEPTION_STACK (0x460), and BOTH
    // regions end at the same orig_sp -- so that zero-fill fully overlaps
    // and destroys the raw register-capture frame `regs` points into
    // (x21=[orig_sp-0x300,orig_sp)) BEFORE it is read below. Snapshot it
    // onto this function's own C frame (already off-guest-stack, on
    // sync_exception_stack per C4-21) first -- measured via direct address
    // arithmetic on the offsets above, not guessed.
    unsigned char regs_snap[SYNC_FRAME_SIZE];
    move_bytes(regs_snap, (unsigned char *)regs, SYNC_FRAME_SIZE);
    u64 *regs_safe = (u64 *)regs_snap;

    // CHALICE C4-61: narrow, one-address-only full x0-x30 dump + a 64-byte
    // instruction hexdump at elr, gated strictly to the single fixed host
    // address C4-58/59 named as hello9's fatal, first-ever-occurrence fault
    // (elr=0x26D0C02B0) -- never fires on the ~20 unrelated shared self-heal
    // cycles (those sit at 20 DIFFERENT elr values, C4-59) or on the
    // canonical hello8 boot (which never reaches this address at all).
    // Measurement-only, no control-flow change.
    if (elr == 0x26D0C02B0ULL) {
        puts("CHALICE C4-61 FULL REGS @elr=0x26D0C02B0:\n");
        for (int i = 0; i <= 28; i += 4) {
            puts("  x"); putdec(i);  puts("=");  puthex(regs_safe[i]);
            if (i + 1 <= 28) { puts(" x"); putdec(i+1); puts("="); puthex(regs_safe[i+1]); }
            if (i + 2 <= 28) { puts(" x"); putdec(i+2); puts("="); puthex(regs_safe[i+2]); }
            if (i + 3 <= 28) { puts(" x"); putdec(i+3); puts("="); puthex(regs_safe[i+3]); }
            puts("\n");
        }
        puts("  x29(fp)="); puthex(regs_safe[29]); puts(" x30(lr)="); puthex(regs_safe[30]); puts("\n");
        puts("CHALICE C4-61 ELR BYTES @elr..+64:");
        for (long w = 0; w < 64; w += 4) {
            u64 a = elr + w; u32 word;
            puts(" ");
            if (nt_fault_read32(a, &word)) puthex(word);
            else puts("????????");
        }
        puts("\n");

        // CHALICE C4-62: wider PRE-elr context -- C4-61's own window only
        // went back to elr-32 (8 words); dump elr-128..elr-32 as well (24
        // MORE words, none previously captured) so the full elr-128..elr
        // range can be disassembled and checked for a conditional branch
        // (cbz/cbnz/b.cond/tbz/tbnz) landing on the elr-32 boundary --
        // measurement-only, same elr==0x26D0C02B0 gate, no control-flow
        // change.
        puts("CHALICE C4-62 WIDER PRE WINDOW @elr-128..-32:");
        for (long w = -128; w < -32; w += 4) {
            u64 a = elr + w; u32 word;
            puts(" ");
            if (nt_fault_read32(a, &word)) puthex(word);
            else puts("????????");
        }
        puts("\n");
    }

    // CHALICE C4-22: donor-grounded TLS probe -- FEX TEB64.TlsSlots @ 0x1480
    // (wine/include/winternl.h:1238); WOW64_TLS_MAX_NUMBER=19 (FEX
    // Source/Windows/include/winternl.h:29) -> THREAD_STATE slot 16,
    // CONTROL_WORD slot 17, ENTRY_CONTEXT slot 18 (FEX Module.cpp:77-80).
    // Read the interrupted guest's OWN x18 (TEB, regs_safe[18]) and the raw
    // bytes FEX itself keeps in those two slots -- measured, not guessed.
    {
        u64 teb = regs_safe[18];
        puts("CHALICE TLS PROBE: x18="); puthex(teb);
        if (teb && teb >= NT_RAM_BASE && teb + 0x1490 < NT_RAM_TOP) {
            u64 thread_state = *(volatile u64 *)(teb + 0x1480 + 16 * 8);
            u64 entry_ctx    = *(volatile u64 *)(teb + 0x1480 + 18 * 8);
            puts(" slot16(ThreadState)="); puthex(thread_state);
            puts(" slot18(EntryContext)="); puthex(entry_ctx);
            if (entry_ctx && entry_ctx >= NT_RAM_BASE && entry_ctx + ARM64_CONTEXT_SIZE < NT_RAM_TOP) {
                puts(" ec.pc="); puthex(*(volatile u64 *)(entry_ctx + ARM64_CONTEXT_PC_OFF));
                puts(" ec.sp="); puthex(*(volatile u64 *)(entry_ctx + ARM64_CONTEXT_SP_OFF));
                puts(" ec.x18="); puthex(*(volatile u64 *)(entry_ctx + ARM64_CONTEXT_X_OFF + 18 * 8));
            }
            // CHALICE C4-31 MEASURE step 2b: the consume-check's own state --
            // FEXCore::Core::CpuStateFrame::SynchronousFaultData, written by
            // IR::Break (FEXCore/Source/Interface/Core/JIT/MiscOps.cpp:65-77)
            // immediately before branching to the GuestSignal_SIGSEGV self-
            // fault trampoline (C4-30), and consumed by FEX::Windows::
            // HandleGuestException (Source/Windows/Common/Exception.h:14-79,
            // called from BTCpuResetToConsistentStateImpl, Module.cpp:936-937)
            // to translate this self-fault into the REAL guest x86 exception
            // (Fault.Signal/TrapNo/si_code/err_code). Donor-derived offsets
            // (no donor edits, computed by hand from CoreState.h field list,
            // clang++ offsetof cross-check blocked -- fmt/ submodule not
            // checked out in this quarry):
            //   InternalThreadState::CurrentFrame is the FIRST member, offset
            //     0 (FEXCore/include/FEXCore/Debug/InternalThreadState.h:91)
            //     -> Thread == CurrentFrame's address is *(u64*)ThreadState.
            //   CpuStateFrame::SynchronousFaultData offset 0x5D8: sizeof
            //     (CPUState)=0x5C0 (alignas(64), raw sum 1464 -> round to
            //     1472) + ReturningStackLocation(8)@0x5C0 + InSyscallInfo(8)
            //     @0x5C8 + SignalHandlerRefCounter(u32)@0x5D0 -> next=0x5D4,
            //     rounded to 8 (alignas(8) SynchronousFaultDataStruct) =
            //     0x5D8 (CoreState.h:393-432; matches the file's own
            //     static_assert "offsetof(...) % 8 == 0").
            //   Struct packing (SynchronousFaultDataStruct, 8 bytes, LE):
            //     byte0=FaultToTopAndGeneratedException(bool) byte1=Signal
            //     byte2=TrapNo byte3=si_code bytes4-5=err_code(u16)
            //     bytes6-7=_pad -- matches DEF_OP(Break)'s own memcpy(&
            //     Constant,&State,8) packing exactly (MiscOps.cpp:65-74).
            // thread_state/CurrentFrame live in the reserve-commit ARENA
            // (>=4GB, C4-8/C4-15 HIGH-pool-backed), NOT the plain NT_RAM_BASE
            // .. NT_RAM_TOP identity window -- use the existing safe-probe
            // reader (nt_fault_read32, ntfile.c, already arena/territory/VM
            // aware) instead of a raw bounds check, two u32 halves LE-packed.
            {
                u32 cf_lo = 0, cf_hi = 0;
                if (thread_state && nt_fault_read32(thread_state + 0, &cf_lo) &&
                    nt_fault_read32(thread_state + 4, &cf_hi)) {
                    u64 current_frame = ((u64)cf_hi << 32) | cf_lo;
                    puts(" CurrentFrame="); puthex(current_frame);
                    u32 f_lo = 0, f_hi = 0;
                    if (current_frame && nt_fault_read32(current_frame + 0x5D8, &f_lo) &&
                        nt_fault_read32(current_frame + 0x5DC, &f_hi)) {
                        u64 fault_raw = ((u64)f_hi << 32) | f_lo;
                        puts(" CHALICE C4-31 SynchronousFaultData raw="); puthex(fault_raw);
                        puts(" FaultToTopAndGeneratedException="); puthex(fault_raw & 0xff);
                        puts(" Signal="); puthex((fault_raw >> 8) & 0xff);
                        puts(" TrapNo="); puthex((fault_raw >> 16) & 0xff);
                        puts(" si_code="); puthex((fault_raw >> 24) & 0xff);
                        puts(" err_code="); puthex((fault_raw >> 32) & 0xffff);
                    } else {
                        puts(" CurrentFrame-unreadable");
                    }
                } else if (thread_state) {
                    puts(" ThreadState-unreadable");
                }
            }
        } else {
            puts(" teb-out-of-range");
        }
        puts("\n");
    }

    // CHALICE C4-33 LEAD 2: dump FEX's InvalidationTracker::XIntervals LIVE at
    // this NoExecOp self-fault (C4-31 proved this is the exact fault site), to
    // answer C4-31/32's sharpened gap directly -- does the executable-range
    // interval tree actually enclose exe_entry(0x47661000) by decode time,
    // despite the two receipted insert paths (ctor VirtualQuery sweep +
    // HandleImageMap)? The global `std::optional<InvalidationTracker>
    // InvalidationTracker` lives in libwow64fex.dll's anon namespace
    // (Module.cpp:130). Its object address is DERIVED, not guessed, from the
    // DLL's own (non-stripped) COFF symtab + disassembly of
    // WowSyscallHandler::QueryGuestExecutableRange (RVA 0x53f8):
    //   adrp x0,0x18034c000 ; add x0,x0,#0x430  =>  &InvalidationTracker =
    //   ImageBase(0x180000000)+0x34c430 (the `this` it tail-calls
    //   InvalidationTracker::QueryExecutableRange with, RVA 0x16a910).
    // std::optional stores its value inline; XIntervals is the FIRST member of
    // InvalidationTracker (InvalidationTracker.h:55) => the XIntervals
    // libc++ std::vector<Interval> sits AT the object address, layout
    //   {Interval* begin @+0, Interval* end @+8, cap @+16}; Interval =
    //   {uint64_t Offset @+0, uint64_t End @+8} (IntervalList.h:16-23).
    // count = (end-begin)/16. begin/end point into the fextl/rpmalloc heap
    // (reserve-commit arena) => read via the arena-aware nt_fault_read32.
    {
        // CHALICE C4-49: re-armed (was one-shot, static int ivt_dumped) --
        // fired at the FIRST self-fault only (an earlier, unrelated event
        // in this same run), never re-checked at the LATER kernelbase.dll
        // NoExecOp self-fault C4-49 actually needs to inspect. Now dumps at
        // every self-fault occurrence (capped, cheap) and checks BOTH the
        // original exe_entry target AND kernelbase.dll's real entry
        // (0x5E228BD0, this run's own live eip) for enclosure.
        static int ivt_dump_count;
        u64 fex_base = 0, fex_size = 0;
        if (ivt_dump_count < 16 && nt_image_base_by_name("libwow64fex.dll", &fex_base, &fex_size)) {
            ivt_dump_count++;
            u64 ivt = fex_base + 0x34c430;
            u32 b_lo = 0, b_hi = 0, e_lo = 0, e_hi = 0;
            if (nt_fault_read32(ivt + 0, &b_lo) && nt_fault_read32(ivt + 4, &b_hi) &&
                nt_fault_read32(ivt + 8, &e_lo) && nt_fault_read32(ivt + 12, &e_hi)) {
                u64 xbeg = ((u64)b_hi << 32) | b_lo;
                u64 xend = ((u64)e_hi << 32) | e_lo;
                u64 n = (xend >= xbeg && xbeg) ? (xend - xbeg) / 16 : 0;
                puts("CHALICE C4-49 XIntervals ivt="); puthex(ivt);
                puts(" begin="); puthex(xbeg); puts(" end="); puthex(xend);
                puts(" count="); puthex(n); puts("\n");
                int encloses = 0, encloses_kb = 0;
                u64 target = 0x47661000;
                u64 target_kb = 0x5E228BD0;
                for (u64 i = 0; i < n && i < 64; i++) {
                    u32 o_lo = 0, o_hi = 0, d_lo = 0, d_hi = 0;
                    u64 ep = xbeg + i * 16;
                    if (nt_fault_read32(ep + 0, &o_lo) && nt_fault_read32(ep + 4, &o_hi) &&
                        nt_fault_read32(ep + 8, &d_lo) && nt_fault_read32(ep + 12, &d_hi)) {
                        u64 off = ((u64)o_hi << 32) | o_lo;
                        u64 end = ((u64)d_hi << 32) | d_lo;
                        puts(" ["); puthex(off); puts(","); puthex(end); puts(")");
                        if (target >= off && target < end) encloses = 1;
                        if (target_kb >= off && target_kb < end) encloses_kb = 1;
                    } else { puts(" [unreadable]"); break; }
                }
                puts("\n CHALICE C4-49 exe_entry(0x47661000) enclosed="); puthex(encloses);
                puts(" kernelbase_entry(0x5E228BD0) enclosed="); puthex(encloses_kb); puts("\n");
            } else {
                puts("CHALICE C4-49 XIntervals ivt="); puthex(ivt); puts(" vector-unreadable\n");
            }
        }
    }

    // CHALICE C4-50: blob disasm (objdump -t/-d, real prebuilt Hangover-11.9
    // libwow64fex.dll, NOT vendored source) found the notify path is:
    // BTCpuNotifyMapViewOfSection -> internal wrapper (locks mutex, calls
    // GetSectionFilePath then BOTH InvalidationTracker::HandleImageMap
    // (RVA 0x168b10, source-matching) AND a SEPARATE ImageTracker::
    // HandleImageMap(name,Address,bool) (RVA 0x165710 -- NOT present in the
    // vendored FEX/Source tree at all, proving this blob is a newer/
    // different FEX revision). InvalidationTracker::HandleImageMap's own
    // gate, read straight off the disasm (RVA 0x168b3c-0x168b44):
    //   bl RtlImageNtHeader        ; x0 = RtlImageNtHeader(Address)
    //   ldrh w8, [x0, #0x6]        ; w8 = FileHeader.NumberOfSections
    //   cbz  x8, <skip-to-epilogue> ; if NumberOfSections==0, the ENTIRE
    //                                 section-interval-insert loop (the only
    //                                 code path that ever calls
    //                                 IntervalList::Insert) is skipped --
    //                                 function returns having inserted
    //                                 nothing.
    // This is THE tell: the insert is gated solely on
    // RtlImageNtHeader(Address)->FileHeader.NumberOfSections != 0. Live
    // check below reads that exact field (offset e_lfanew+4+2 from each
    // DLL's own mapped base -- IMAGE_NT_HEADERS.FileHeader.NumberOfSections,
    // the SAME bytes RtlImageNtHeader(Address) would return a pointer into)
    // directly out of THIS boot's real mapped kernel32.dll/kernelbase.dll
    // images (PeMapImage's own copy, ntfile.c/pemod.c -- RAM is
    // identity-mapped so a plain bounds-checked C deref is safe, matching
    // the root_key_names[] spot-check idiom below) to confirm which side of
    // the branch the blob actually takes for these two DLLs.
    {
        static int c450_dumped[2];
        static const char *c450_names[2] = {"syswow64/kernel32.dll", "syswow64/kernelbase.dll"};
        for (int di = 0; di < 2; di++) {
            if (c450_dumped[di]) continue;
            u64 dbase = 0, dsize = 0;
            if (!nt_image_base_by_name(c450_names[di], &dbase, &dsize)) continue;
            c450_dumped[di] = 1;
            puts("CHALICE C4-50 "); puts(c450_names[di]); puts(" base="); puthex(dbase);
            puts(" size="); puthex(dsize);
            if (dbase < NT_RAM_BASE || dbase + 0x1000 >= NT_RAM_TOP) {
                puts(" -> base out of RAM range, not dereferenced\n");
                continue;
            }
            u32 mz = *(u32 *)dbase;
            u32 e_lfanew = *(u32 *)(dbase + 0x3C);
            puts(" mz="); puthex(mz); puts(" e_lfanew="); puthex(e_lfanew);
            u64 nth = dbase + e_lfanew;
            if (nth + 8 >= NT_RAM_TOP || e_lfanew > dsize) {
                puts(" -> NT header out of range, not dereferenced\n");
                continue;
            }
            u32 pesig = *(u32 *)nth;
            u16 machine = *(u16 *)(nth + 4);
            u16 nsections = *(u16 *)(nth + 6);
            puts(" pesig="); puthex(pesig); puts(" machine="); puthex(machine);
            puts(" NumberOfSections="); puthex(nsections);
            puts(nsections == 0 ? " -> GATE TAKEN (skip, matches live XIntervals stall)\n"
                                : " -> GATE NOT TAKEN (insert should proceed)\n");

            // CHALICE C4-51: blob disasm of InvalidationTracker::HandleImageMap's
            // FULL body (RVA 0x168b10, not just C4-50's header gate) proved the
            // section-walk loop calls IntervalList::Insert(this, SectionBase,
            // SectionEnd) UNCONDITIONALLY for every section with
            // IMAGE_SCN_MEM_EXECUTE set (Characteristics byte 3 bit 5 = bit 29 =
            // 0x20000000), gated on Insert()'s own "Base==End" empty-interval
            // skip (0x18000566c) -- and IntervalList::Insert writes into
            // `this+0` (begin/end/cap, libc++ vector layout), the EXACT SAME
            // vector our own XIntervals dumper reads at fex_base+0x34c430+0 --
            // proven via the SAME `this` global (adrp 0x18034c000+0x430) used by
            // BOTH BTCpuProcessInit's wrapper call site (0x1800029c8-d0) AND
            // WowSyscallHandler::QueryGuestExecutableRange's tail-call
            // (0x1800053fc-400) -- object-identity mismatch (C4-50 candidate iii)
            // is REFUTED. Sole remaining candidate (ii): walk each section's
            // REAL Characteristics/VirtualAddress/VirtualSize as this boot's own
            // mapped image actually has them (same raw-deref technique as the
            // NumberOfSections read above), to see whether ANY section is
            // EXECUTE+nonzero-size (the two preconditions Insert() and the
            // EXECUTE-bit test both require).
            u16 opthdrsz = *(u16 *)(nth + 0x14);
            u64 sec0 = nth + 0x18 + opthdrsz;
            puts(" SizeOfOptionalHeader="); puthex(opthdrsz);
            puts(" FirstSection="); puthex(sec0); puts("\n");
            int any_exec = 0;
            for (int si = 0; si < nsections && si < 16; si++) {
                u64 sh = sec0 + (u64)si * 0x28;
                if (sh + 0x28 >= NT_RAM_TOP) { puts("  [section out of range]\n"); break; }
                u32 vsize = *(u32 *)(sh + 0x8);
                u32 vaddr = *(u32 *)(sh + 0xC);
                u32 chars = *(u32 *)(sh + 0x24);
                int is_exec = (chars & 0x20000000) ? 1 : 0;
                int nonempty = (vsize != 0) ? 1 : 0;
                if (is_exec && nonempty) any_exec = 1;
                puts("  CHALICE C4-51 section["); puthex((u64)si); puts("] VA="); puthex(vaddr);
                puts(" VSize="); puthex(vsize); puts(" Characteristics="); puthex(chars);
                puts(is_exec ? " EXEC=1" : " EXEC=0");
                puts(nonempty ? " nonempty=1" : " nonempty=0"); puts("\n");
            }
            puts(" CHALICE C4-51 any_exec_nonempty_section="); puthex((u64)any_exec); puts("\n");
        }
    }

    // CHALICE C4-34 MEASURE atom: C4-33 proved XIntervals ENCLOSES exe_entry at
    // this exact self-fault, yet the fault byte-matches NoExecOp -- meaning the
    // failing FEXCore::Frontend::Decoder::CheckRangeExecutable call queried a
    // DIFFERENT address than exe_entry (R1: a host InstStream pointer, not the
    // guest linear addr) or ran at an earlier timestamp than the interval
    // insert (R2). Read the live per-thread Decoder object directly -- no
    // donor edit, offsets DERIVED (not guessed) by disassembling libwow64fex.dll
    // (this pass, objdump -d, matching against FEXCore/Source/Interface/Core/
    // Frontend.h/.cpp source line-for-line):
    //   InternalThreadState+0x28 = FrontendDecoder (Decoder*) -- confirmed via
    //     ContextImpl::CheckIfBlockIsCacheable's own `ldr x0,[x1,#0x28]` before
    //     tail-calling Decoder::CheckIfCacheable (Thread.FrontendDecoder->...).
    //   Decoder+0x68 = ExecutableRangeBase (u64), Decoder+0x70 =
    //     ExecutableRangeEnd (u64), Decoder+0x78 = ExecutableRangeWritable
    //     (bool), Decoder+0x79 = HitNonExecutableRange (bool), Decoder+0x80 =
    //     InstStream (const uint8_t*), Decoder+0x88 = InstructionSize (uint8_t)
    //     -- all confirmed by disassembling CheckRangeExecutable/ReadByte/
    //     PeekByte (RVAs 0x1ce90/0x1cf34/0x1d000): identical Base/End compare
    //     sequence in all three, InstStream+InstructionSize+Offset==ByteAddress
    //     matches Frontend.cpp:127 exactly (PeekByte), and the HitNonExecRange
    //     store site (ReadByte, offset 0x79) matches Frontend.cpp:116 exactly.
    // ByteAddress computed here with Offset=0 (ReadByte's own PeekByte(0) call,
    // the first byte of any new instruction) -- THE queried address.
    {
        static int decoder_dumped;
        u64 teb2 = regs_safe[18];
        if (!decoder_dumped && teb2 && teb2 >= NT_RAM_BASE && teb2 + 0x1490 < NT_RAM_TOP) {
            decoder_dumped = 1;
            u64 thread_state2 = *(volatile u64 *)(teb2 + 0x1480 + 16 * 8);
            puts("CHALICE C4-34 ThreadState="); puthex(thread_state2);
            u32 d_lo = 0, d_hi = 0;
            if (thread_state2 && nt_fault_read32(thread_state2 + 0x28, &d_lo) &&
                nt_fault_read32(thread_state2 + 0x2c, &d_hi)) {
                u64 decoder = ((u64)d_hi << 32) | d_lo;
                puts(" Decoder="); puthex(decoder);
                u32 rb_lo = 0, rb_hi = 0, re_lo = 0, re_hi = 0;
                u32 flags_word = 0, is_lo = 0, is_hi = 0;
                if (decoder &&
                    nt_fault_read32(decoder + 0x68, &rb_lo) && nt_fault_read32(decoder + 0x6c, &rb_hi) &&
                    nt_fault_read32(decoder + 0x70, &re_lo) && nt_fault_read32(decoder + 0x74, &re_hi) &&
                    nt_fault_read32(decoder + 0x78, &flags_word) &&
                    nt_fault_read32(decoder + 0x80, &is_lo) && nt_fault_read32(decoder + 0x84, &is_hi)) {
                    u64 exec_base = ((u64)rb_hi << 32) | rb_lo;
                    u64 exec_end  = ((u64)re_hi << 32) | re_lo;
                    u64 inst_stream = ((u64)is_hi << 32) | is_lo;
                    u32 inst_size_byte = 0;
                    /* InstructionSize (u8) lives at Decoder+0x88 -- separate word */
                    nt_fault_read32(decoder + 0x88, &inst_size_byte);
                    u32 inst_size = inst_size_byte & 0xff;
                    u32 writable = flags_word & 0xff;
                    u32 hit_nonexec = (flags_word >> 8) & 0xff;
                    u64 byte_address = inst_stream + inst_size; /* Offset=0, ReadByte's own PeekByte(0) */
                    int enclosed34 = (byte_address >= exec_base && byte_address < exec_end) ? 1 : 0;
                    puts(" ExecutableRangeBase="); puthex(exec_base);
                    puts(" ExecutableRangeEnd="); puthex(exec_end);
                    puts(" ExecutableRangeWritable="); puthex(writable);
                    puts(" HitNonExecutableRange="); puthex(hit_nonexec);
                    puts(" InstStream="); puthex(inst_stream);
                    puts(" InstructionSize="); puthex(inst_size);
                    puts("\n CHALICE C4-34 ByteAddress(queried)="); puthex(byte_address);
                    puts(" enclosed="); puthex(enclosed34);
                    puts(" exe_entry(0x47661000) match="); puthex(byte_address == 0x47661000 ? 1 : 0);
                    puts("\n");
                } else {
                    puts(" Decoder-fields-unreadable\n");
                }
            } else {
                puts(" Decoder-ptr-unreadable\n");
            }
        }
    }

    // The prior dispatcher stack is still live at this point.  This is the
    // exact CONTEXT bytes RtlRestoreContext was handed in the prior pass.
    if (prior_context_valid && !context_return_logged) {
        unsigned char *after = (unsigned char *)prior_context_addr;
        context_return_logged = 1;
        puts("CHALICE CONTEXT RETURN: flags "); puthex(*(u32 *)(prior_context + 0));
        puts("->"); puthex(*(u32 *)(after + 0));
        puts(" pc "); puthex(*(u64 *)(prior_context + ARM64_CONTEXT_PC_OFF));
        puts("->"); puthex(*(u64 *)(after + ARM64_CONTEXT_PC_OFF));
        puts(" sp "); puthex(*(u64 *)(prior_context + ARM64_CONTEXT_SP_OFF));
        puts("->"); puthex(*(u64 *)(after + ARM64_CONTEXT_SP_OFF)); puts("\n");
    }
    stack = guest_exception_stack(sp_orig, 0, 0);
    if (!stack) return 0;
    unsigned char *ctx = (unsigned char *)stack;
    unsigned char *rec = ctx + ARM64_EXCEPTION_REC_OFF;
    *(u32 *)(ctx + 0x00) = ARM64_CONTEXT_FULL;
    u64 spsr, fpcr, fpsr;
    asm volatile("mrs %0, spsr_el1\n mrs %1, fpcr\n mrs %2, fpsr"
                 : "=r"(spsr), "=r"(fpcr), "=r"(fpsr));
    *(u32 *)(ctx + 0x04) = (u32)spsr;
    for (u64 i = 0; i < 31; i++) *(u64 *)(ctx + ARM64_CONTEXT_X_OFF + i * 8) = regs_safe[i];
    *(u64 *)(ctx + ARM64_CONTEXT_SP_OFF) = sp_orig;
    *(u64 *)(ctx + ARM64_CONTEXT_PC_OFF) = elr;
    move_bytes(ctx + ARM64_CONTEXT_V_OFF, regs_snap + SYNC_FRAME_Q_OFF,
               SYNC_FRAME_Q_BYTES);
    *(u32 *)(ctx + ARM64_CONTEXT_FPCR_OFF) = (u32)fpcr;
    *(u32 *)(ctx + ARM64_CONTEXT_FPSR_OFF) = (u32)fpsr;
    nt_exception_context_watch = ctx;
    puts("CHALICE CONTEXT DELIVERY: flags="); puthex(*(u32 *)(ctx + 0));
    puts(" cpsr="); puthex(*(u32 *)(ctx + 4));
    puts(" pc="); puthex(*(u64 *)(ctx + ARM64_CONTEXT_PC_OFF));
    puts(" sp="); puthex(*(u64 *)(ctx + ARM64_CONTEXT_SP_OFF)); puts("\n");
    for (u64 i = 0; i < ARM64_CONTEXT_SIZE; i++) prior_context[i] = ctx[i];
    prior_context_addr = stack;
    prior_context_valid = 1;

    *(u32 *)(rec + 0x00) = (u32)status;
    *(u64 *)(rec + 0x10) = elr;
    *(u32 *)(rec + 0x18) = (u32)params;
    if (params) {
        *(u64 *)(rec + 0x20) = (esr >> 6) & 1;     // ESR.ISS.WnR
        *(u64 *)(rec + 0x28) = far;
    }
    puts("CHALICE EXCEPTION: code="); puthex(status);
    puts(" elr="); puthex(elr); puts(" dispatch="); puthex(dispatcher); puts("\n");
    // CHALICE C4-59: cycle counter + live FEXCore State.rip, tagged at every
    // guest-exception DELIVERY this function performs (superset of the
    // 0x26D0C02B0 self-fault class C4-58 named -- grep this floor's own
    // "elr=0x26D0C02B0" cycles out of the full timeline below). teb =
    // regs_safe[18] (interrupted guest's own x18/TEB, same C4-22-grounded
    // read the TLS PROBE block above already performs) -- see ntfile.h/.c
    // for the full State.rip pointer-chain receipt.
    {
        unsigned long state_rip = ~0UL;
        int ok = nt_wow64_live_state_rip((unsigned long)regs_safe[18], &state_rip);
        nt_c459_cycle++;
        puts("CHALICE C4-59: cycle="); puthex((u64)nt_c459_cycle);
        puts(" elr="); puthex(elr);
        puts(" State.rip="); puts(ok ? "" : "(unreadable)"); if (ok) puthex((u64)state_rip);
        puts("\n");
    }
    // CHALICE C4-29 MEASURE step 1: who called the faulting code -- x30/LR
    // as captured by SAVEALL_SYNC (regs_safe[30], the pre-fault caller
    // return address, not a stack scan) resolved the same way sync_handler's
    // fatal path already resolves elr/far/x0 etc. Always-on for the
    // guest-forwarded path (not just the fatal dump) -- this is exactly the
    // receipt C4-29 needs: which code passed the null image base into
    // RtlImageNtHeader (or whichever function this fault happens to be).
    resolve_line("lr ", regs_safe[30]);
    resolve_line("x0 ", regs_safe[0]);
    sync_guest_exception_elr = dispatcher;
    // Register truth probe: system registers, never a frame copy.  boot.S
    // repeats the ELR write as its final instruction sequence before ERET.
    u64 live_elr, live_spsr;
    static u64 truth_count;
    asm volatile("msr elr_el1, %2\n isb\n mrs %0, elr_el1\n mrs %1, spsr_el1"
                 : "=r"(live_elr), "=r"(live_spsr) : "r"(dispatcher));
    if (truth_count++ < 12) {
        puts("CHALICE ERET TRUTH: elr_el1="); puthex(live_elr);
        puts(" spsr_el1="); puthex(live_spsr); puts("\n");
    }
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
