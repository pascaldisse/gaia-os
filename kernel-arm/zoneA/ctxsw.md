# aarch64 Context Switch Design

## x86 Baseline (what we replace)

ZealOS x86 context switch lives in `Sched.ZC` as inline asm blocks:

```
TASK_CONTEXT_SAVE:
  PUSH RSI; PUSHFD; push all GPRs (RAX,RCX,RDX,RBX,RBP,RDI,R8-R15)
  save RFLAGS, RSP, RIP, RSI
  FXSAVE (FPU/SSE state)
  save debug breakpoints
  RET

_TASK_CONTEXT_RESTORE:
  INC swap_counter
  FXRSTOR
  pop all GPRs
  IRET (restores RIP, CS, RFLAGS, RSP, SS atomically)
```

Key observations:
- Tasks share the same address space (identity-mapped, single CR3). No TLB flush on switch.
- `IRET` to the 64-bit code segment; `FS` base (WRFSBASE) carries the `CTask*`.
- Save/restore is symmetric: save places regs into `CTask` struct, restore pops them.
- Breakpoints: saved/restored separately (INT3 patching).

## aarch64 Equivalent

### Register File

When entering an exception/timer IRQ at EL1 (kernel), the hardware automatically saves:

```
SPSR_EL1   — saved PSTATE (analogous to x86 RFLAGS)
ELR_EL1    — exception return address (analogous to RIP)
ESR_EL1    — exception syndrome (for fault dispatch)
SP_EL0     — stack pointer used at EL0 (if applicable)
```

The GPRs (x0-x30) are NOT auto-saved. We must save/restore them manually.

### Task Frame (stored on kernel stack)

```
struct A64TaskFrame {
    // Callee-saved (per AAPCS64)
    U64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29, x30;
    // Exception context (saved by entry path)
    U64 elr_el1;
    U64 spsr_el1;
    U64 sp_el0;
    // FP/SIMD state (optional, lazy)
    U8  fpsimd[512];  // 32×128-bit registers
};
```

### Task struct (CTask equivalent for aarch64)

Key fields relevant to context switch:

```
class CTask {
    U64  x19..x30;      // callee-saved registers
    U64  elr_el1;        // exception return address (RIP analog)
    U64  spsr_el1;       // saved PSTATE (RFLAGS analog)
    U64  sp;             // kernel stack pointer (SP_EL1)
    U64  sp_el0;         // user stack pointer (if EL0 tasks exist)
    U8  *fpsimd;         // FP/SIMD state (lazy save)
    U64  task_flags;
    // ... rest of ZealOS task fields (task_num, wake_jiffy, etc.)
};
```

### Context Save (on timer IRQ / Yield)

```
// Entry: exception vector already saved ELR_EL1, SPSR_EL1 to stack.
// We are on the outgoing task's kernel stack (SP_EL1).

irq_timer_entry:
    // Save caller-saved regs to stack frame
    stp x0,  x1,  [sp, #-16]!
    stp x2,  x3,  [sp, #-16]!
    ... through x18
    stp x28, x29, [sp, #-16]!
    str x30,      [sp, #-8]!

    // Load current CTask pointer from TPIDR_EL1
    mrs x9, tpidr_el1          // x9 = Fs (CTask*)
    // x9 is the aarch64 analog of x86 FS base

    // Copy from stack frame into CTask struct fields
    // (or just save SP_EL1 as the task's stack pointer — the frame
    //  stays where it is until the task runs again)

    // Save SP_EL1 into CTask.sp
    mov x10, sp
    str x10, [x9, #CTask_sp_offset]

    // Acknowledge timer IRQ (CNTP_CTL_EL0)
    // EOI to GIC

    // Now we are at a yield point. Scheduler picks next task.
    bl scheduler_pick_next     // returns new CTask* in x0

    // Fall through to restore.
```

### Context Restore

```
task_context_restore:
    // x0 = next CTask*
    msr tpidr_el1, x0          // set Fs = new task

    // Restore SP_EL1
    ldr x10, [x0, #CTask_sp_offset]
    mov sp, x10

    // Load caller-saved regs from stack frame
    ldp x0,  x1,  [sp], #16
    ldp x2,  x3,  [sp], #16
    ... through x18
    ldp x28, x29, [sp], #16
    ldr x30,      [sp], #8

    // Restore ELR_EL1, SPSR_EL1 from CTask (for new tasks)
    ldp x10, x11, [x0, #CTask_elr_offset]
    msr elr_el1, x10
    msr spsr_el1, x11

    // Return from exception — restores PC to ELR_EL1
    eret
```

### Timer IRQ Scheduler (Yield equivalent)

On aarch64, `Yield` is replaced by the timer IRQ handler. The timer fires at 1000 Hz (same as ZealOS). On each tick:

1. Save outgoing task state as above.
2. Call `Sched` → pick next runnable task from circular queue (same round-robin logic as ZealOS: skip suspended, skip awaiting-message, skip wake_jiffy not yet reached, skip idle if no IRQ happened, etc.).
3. If no runnable task besides executive/idle, wait for interrupt (`WFI` instead of x86 `HLT`).
4. Restore incoming task.

`Yield` is simply: save self → call scheduler → restore next. No timer involvement needed.

### Key Differences from x86

| Aspect | x86 | aarch64 |
|--------|-----|---------|
| Return mechanism | IRET (auto-restores CS:RIP,RFLAGS,SS:RSP) | ERET (restores PC from ELR_EL1, PSTATE from SPSR_EL1) |
| Current task pointer | FS segment base (WRFSBASE) | TPIDR_EL1 (MSR TPIDR_EL1) |
| Idle wait | HLT (x86 halt) | WFI (Wait For Interrupt) |
| IRQ acknowledge | LAPIC EOI write | GICv3 EOI (ICC_EOIR1_EL1 write) |
| FPU save | FXSAVE / FXRSTOR (512 bytes) | STP/STR of Q0-Q31 (512 bytes) — or lazy via CPACR_EL1 trap |
| CR3 switch on task | Not needed (identity mapped) | Not needed (TTBR0/1 shared) |
| Task switch time | ~0.5µs (claimed) | Comparable: ~30 instructions, mostly LDP/STP pairs |

### Lazy FPU

ARMv8 allows trapping FP/SIMD access. Set CPACR_EL1.FPEN to trap (0b00). On first FP access after switch, the trap handler:
1. Saves FP state for the *previous* task (stored via TPIDR_EL1).
2. Restores FP state for the *current* task.
3. Sets CPACR_EL1.FPEN to not-trap.

On context switch out, if FP was used (dirty flag on task), save FP state. Otherwise skip. This is the standard lazy-FPU optimization and matches what ZealOS's `FXSAVE` per-switch does (ZealOS saves FP eagerly but the cost is small).

### Multi-Core (PSCI)

x86 AP startup uses INIT/SIPI IPI sequence + a real-mode trampoline at a fixed physical address (MP_VECT_ADDR). On aarch64, secondary cores are started via PSCI:

```
// Core 0 calls PSCI to power-on secondary cores
// PSCI_CPU_ON(target_cpu, entry_point, context_id)

ldr x0, =0xC4000003   // PSCI_CPU_ON function ID (64-bit)
ldr x1, =target_cpu   // MPIDR-style affinity
ldr x2, =_start_secondary  // entry point at EL1
ldr x3, =context_id
smc #0                // call EL3 firmware
```

The secondary core starts at `_start_secondary`, which:
1. Configures its own MMU (TTBR0_EL1 — same page tables, or a copy with per-CPU stack).
2. Sets VBAR_EL1 (same vector table).
3. Initializes local GIC redistributor.
4. Sets TPIDR_EL1 to its CCPU struct pointer.
5. Enables timer and drops into the scheduler loop.
