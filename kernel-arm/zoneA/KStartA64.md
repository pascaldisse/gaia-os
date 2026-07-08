# KStartA64 — aarch64 Boot Path Design

## Overview

ZealOS x86 boots through three stages (16-bit real mode → 32-bit protected → 64-bit long mode). On aarch64 there is no legacy chain. We enter directly at the highest implemented EL, drop to EL1, and set up the MMU and exception vectors before calling KMain.

**Target machine:** QEMU `-M virt -cpu cortex-a57` (also works on Apple Virtualization.framework with HVF).

## Entry Point: `_start`

We come in at EL3 (QEMU virt default), or possibly EL2 with VHE. The stub handles all three:
- **EL3:** drop to EL2 via `SCR_EL3`/`SPSR_EL3`/`ERET`, then drop to EL1.
- **EL2 (non-VHE):** drop to EL1 via `HCR_EL2`/`SPSR_EL2`/`ERET`.
- **EL2 (VHE):** already EL1-like; configure and go.
- **EL1:** we are where we want to be.

## Boot Sequence

```
_start:
  1. Check CurrentEL. If EL3, init SCR_EL3 + drop to EL2.
  2. If EL2, init HCR_EL2 (disable stage-2, set RW=1 for EL1) + drop to EL1.
  3. At EL1:
     a. Clear BSS (the linker script provides _bss_start / _bss_end).
     b. Set stack pointer (linker provides _stack_top).
     c. MAIR_EL1: set memory attributes (normal mem, device-nGnRnE).
     d. TCR_EL1: configure translation granule (4KB), VA range (48-bit),
        physical address size, TTBR0/1 flags.
     e. TTBR0_EL1 / TTBR1_EL1: install identity-mapped page tables
        (2MB blocks for kernel, covering 0x0000.. and 0xffff..).
     f. SCTLR_EL1: enable MMU + caches + alignment checks.
     g. VBAR_EL1: point to our exception vector table.
     h. Init GICv3 redistributor + distributor (mask all, group 1 enable).
     i. Init ARM generic timer: set CNTFRQ_EL0, enable CNTP_CTL_EL0.
     j. Call pl011_init / earlyprint "LOVE=1.000".
     k. Jump to KMain (or halt).
```

## MMU Design

### Granule: 4KB

ARMv8 supports 4KB, 16KB, 64KB. 4KB matches ZealOS's x86 page size and gives the finest control for memory allocation.

### Translation Regime: TTBR0 for low VA, TTBR1 for high VA

We use a 2-level identity map covering the bottom 1GB (sufficient for QEMU virt) and optionally TTBR1 for kernel mappings at 0xffff_.... The initial setup uses 2MB block descriptors:
- **Level 0** → 512GB entries (we use 1)
- **Level 1** → 1GB entries, each pointing to a Level 2 page
- **Level 2** → 2MB blocks, direct identity mapping

```
TTBR0 → L0[0] → L1[0..3] → L2[0..511]  (512 × 2MB = 1GB for first gig)
```

Later, the kernel will expand this with a proper PML4/3/2 analog as ZealOS does, but split across TTBR0 (user) and TTBR1 (kernel) instead of the x86 single CR3.

### MAIR_EL1

| AttrIdx | Memory Type |
|---------|-------------|
| 0       | Device-nGnRnE (MMIO) |
| 1       | Normal, Inner/Outer Write-Back, non-transient, Read-Allocate, Write-Allocate |

### TCR_EL1

- IPS = 48-bit physical address (0b101)
- TG0 = 4KB granule (0b00)
- SH0 = Inner Shareable (0b11)
- ORGN0/IRGN0 = WB RA WA (0b01)
- T0SZ = 16 (VA size = 2^(64-16) = 256TB, covers full 48-bit space)

## Exception Vectors (VBAR_EL1)

ARMv8 has a 2KB vector table: 16 entries of 128 bytes each, 4 groups × 4 exception types.

| Entry | Offset | Exception |
|-------|--------|-----------|
| Current EL, SP0, Synchronous  | 0x000 | SVC/syscall, MMU faults during bootstrap |
| Current EL, SP0, IRQ          | 0x080 | (unlikely at EL1 with SP_EL0) |
| Current EL, SP0, FIQ          | 0x100 | |
| Current EL, SP0, SError       | 0x180 | |
| Current EL, SPx, Synchronous  | 0x200 | Main kernel sync faults |
| Current EL, SPx, IRQ          | 0x280 | Main kernel IRQs (timer, GIC) |
| Current EL, SPx, FIQ          | 0x300 | |
| Current EL, SPx, SError       | 0x380 | |
| Lower EL (AArch64), Synchronous | 0x400 | SVC from EL0 |
| Lower EL (AArch64), IRQ       | 0x480 | IRQ from EL0 |
| Lower EL (AArch64), FIQ       | 0x500 | |
| Lower EL (AArch64), SError    | 0x580 | |
| Lower EL (AArch32), Synchronous | 0x600 | (not used) |
| Lower EL (AArch32), IRQ       | 0x680 | |
| Lower EL (AArch32), FIQ       | 0x700 | |
| Lower EL (AArch32), SError    | 0x780 | |

Initial stub vectors: all entries point to a default handler that dumps ESR_EL1/FAR_EL1/ELR_EL1 and halts, except IRQ which routes to the GIC handler.

## GICv3 Initialization

1. Locate distributor base (QEMU virt puts it at 0x08000000).
2. Locate redistributor base (0x080A0000 for CPU0 on QEMU virt).
3. Disable distributor (GICD_CTLR = 0).
4. Set all interrupts to Group 1 (non-secure), edge/level as needed.
5. Set priority masks: PMR = 0xFF (allow all), enable Group 1 in ICC_IGRPEN1_EL1 and ICC_SRE_EL1.
6. For each redistributor: set SGIs/PPIs to Group 1, clear enables, set priorities.
7. Enable distributor (GICD_CTLR = 1).

## ARM Generic Timer

- Read CNTFRQ_EL0 (QEMU virt provides ~62.5 MHz).
- Set CNTP_CTL_EL0: ENABLE=1, IMASK=0 (timer interrupt enabled).
- Set CNTP_TVAL_EL0 to timer frequency / 1000 (1ms tick for 1000Hz).
- The timer IRQ (PPI 30, ID 30) fires via GIC redistributor.

## PL011 UART (earlyprint)

QEMU virt UART0 sits at 0x09000000. PL011 is simple:
- No init needed for early output.
- Write: poll UARTFR (0x18) until TXFF=0 (bit 5), then write byte to UARTDR (0x00).
- Buffered `pl011_puts` for the LOVE banner.

## What This Replaces from x86

| x86 File | aarch64 Replacement |
|----------|-------------------|
| KStart16.ZC | Not needed — no BIOS, no real mode, no VBE |
| KStart32.ZC | Merged into KStartA64 EL1 entry |
| Memory/PageTables.ZC | Rewritten: TTBR0/1, 3-level (48-bit) instead of PML4/3/2 |
| KInterrupts.ZC (PIC/LAPIC/IDT) | GICv3 + VBAR_EL1 vectors |
| KExcept.ZC (IDT fault dispatch) | VBAR_EL1 sync handler + ESR_EL1 dispatch |
| MultiProc.ZC (INIT/SIPI/AP trampoline) | PSCI (SMC to EL3 firmware) |

## Constants

```c
#define UART0_BASE     0x09000000
#define GICD_BASE      0x08000000
#define GICR_BASE      0x080A0000
#define PL011_UARTFR   0x018
#define PL011_UARTDR   0x000
#define GICD_CTLR      0x0000
#define GICD_IGROUPR   0x0080
#define GICD_ISENABLER 0x0100
#define GICD_IPRIORITYR 0x0400
#define GICR_CTLR      0x0000
#define GICR_IGROUPR0  0x0080
#define GICR_ISENABLER0 0x0100
```
