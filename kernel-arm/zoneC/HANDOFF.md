# zoneC HANDOFF — AArch64 code generator for the Shrine HolyC compiler

Purpose: a native AArch64 code generator (this `kernel-arm/zoneC` tree, all
HolyC) so GAIA-OS — a TempleOS-lineage operating system — can compile its own
source on Apple Silicon. The Shrine HolyC compiler front end runs hosted inside
the `aiwnios` HolyC runtime; zoneC replaces Shrine's x86 back end with an
AArch64 one. This is ordinary compiler engineering: lexer/parser reused as-is,
we only rewrite the instruction-selection + emission tail.

Last green commit: **round 7 commit**. Rounds 1–7 all pass.

---

## Build & test (the only loop that counts)

```
bash kernel-arm/zoneC/mkboot.sh                       # stage flat boot dir -> /tmp/a64boot
timeout 90 aiwnios/aiwnios.app/Contents/MacOS/aiwnios \
    -c -t /tmp/a64boot LoadA64.HC < /dev/null 2>&1 \
    | head -c 2000000 > ~/.gaia/logs/smoke.log; echo exit=${PIPESTATUS[0]}
```

Gate = grep the log for the round markers below. `exit=` reflects `head`'s pipe,
not the process; a hang shows as *missing later markers*, a fault shows as a
`welcome to <addr>()` line (host debugger). A clean run ends with
`JIT hello returned`.

Green markers (all must be present, in order):
`Hello GAIA` · `Val=42` · `Try OK` · `Ind OK` · `Def OK` · `Res6=41` · `Res7=110`.
Any `CAUGHT` / `LIBTHROW` / `UNHANDLED` / `welcome to` line = failure.

Notes:
- `LoadA64.HC` is the driver/harness (probe prints + round rig). Edit it to add
  a new round; it self-includes the whole zoneC stack in load order.
- `shrine/` is **gitignored by design** — it's the donor tree. Permanent
  front-end fixes live as stage-time transforms in `mkboot.sh` (currently:
  PrsExp variadic marker, PrsFun W^X copy, NoRegOpt). Never commit `shrine/`.
- Logs → `~/.gaia/logs/`, specs → `~/.gaia/specs/`.

---

## What works now (rounds 1–7)

| round | program | proves |
|---|---|---|
| 1 | `PutS("Hello GAIA\n")` | string const, fixed call, W^X exec |
| 2 | `Print("Val=%d\n",42)` | variadic call (argc/argv marshal) |
| 3 | `try{...}catch{...}` | try-block sub-compile + inline exec |
| 4 | `zc_fp("Ind OK\n")` | indirect call via fn-ptr slot |
| 5 | `U0 ZC5(){...}` then `ZC5;` | self-compiled void fn, JIT→JIT call |
| 6 | `I64 ZC6(a,b){I64 c; c=a*2; c=c+b; return c;}` | args + frame locals + return value |
| 7 | `I64 ZC7(I64 n){while/if, 2 locals}` | IC_ASSIGN dest stores + fwd-cond/backward branch deltas under a real frame |

## Load-bearing invariants (know these before editing)

- **ABI:** callers pass args in `x0..x7` (aiwnios convention, proven against
  host `Print`/`PutS`). Callees, with reg-opt disabled, read args from stack
  frame homes — so the prologue must spill the incoming registers there.
- **Frame (`A64FrameEnter`, BackA64Br):** save fp/lr, `x29` = frame anchor,
  allocate `locals + arg-home` area, spill `x0..x_{n-1}` to `[sp+16k]`.
  16-byte stack slots throughout (Apple hardware-checks SP alignment — bug#7c).
  Locals map 1:1 below `x29`; args map to `[x29 - argbase + 16k]`
  (`A64FrameRef`, BackA64Lib; `a64_frame_argbase` set at each `IC_ENTER`).
- **Return:** Shrine never emits `IC_RET` inside a fn — `IC_LEAVE`'s epilogue
  emits the return. `return <val>` = `IC_RETURN_VAL` (value→x0) + `IC_JMP` to
  the leave label.
- **W^X:** compile into the data heap, copy to `Fs->code_heap` via
  `WriteProtectMemCpy` (toggles jit-write-protect + flushes icache), execute the
  copy. Baked string addresses still point into the readable data original.
- **Calls:** fixed = `IC_CALL` (absolute addr in `ic_data`); variadic marker
  rides `ic_flags` high bits (mid-passes scrub `arg2` in place — do not park
  data there); indirect = `IC_CALL_INDIRECT` / `IC_CALL_INDIRECT2`.

---

## Next steps (in order)

1. **Round 8 — variadic host call inside a JIT callee.** `Print("%d",x)`
   prints garbage (observed: lone tab) while computation stays correct;
   suspect `a64_arg_cnt`/`a64_arg_sp` argv marshal relative to `sp` under a
   callee frame.
2. **BackA64F (floats).** The F64 intermediate codes have never executed:
   `%f` formatting, F64 arithmetic, int↔float conversion. Emitters exist but
   are unproven live.
3. **AOT mode.** `IC_CALL_IMPORT` / `IC_CALL_EXTERN` under `CCF_AOT_COMPILE`,
   then compile a Zone A kernel module with this back end. That closes M4:
   the compiler compiles the OS.

## Known small debts

- **>8 args:** only `x0..x7` are spilled; stack-passed args 9+ are not handled
  yet (`A64FrameEnter` clamps `nargs` to 8).
- **Large frames (>256 B):** negative-offset arg/local access falls through to
  the general `A64MovImm`+`add`+`load/store` path (BackA64Lib) — correct, but
  only tiny frames are exercised so far; verify when round 7 grows the frame.
- **MDF_NULL:** added to `A64StoreRes` (unused-result stores). Not added to
  `A64LoadArg` (loading a null source shouldn't occur; revisit if it throws).
- **Branch delta normalization:** `delta -= tmpi->ic_cnt` at
  `A64EmitB`/`A64EmitBcc`/`A64EmitCbz`/`A64EmitTbz` entry;
  `A64EmitCmpChain` unaudited for the same pattern.
- **IC_ASSIGN:** `IC_ASSIGN_PP`/`IC_ASSIGN_MM` unhandled → alarm; float
  `IC_ASSIGN` → `BackA64F` unproven.

## Working discipline (what made this tractable)

- Prove every claim with the real smoke above — never assert "it works".
- Commit the green config first; strip/clean as a separate re-gated step.
- Diagnose from emitted machine code and probe trails, not from theory.
- One change per smoke; keep the previous green run as the diff baseline.
