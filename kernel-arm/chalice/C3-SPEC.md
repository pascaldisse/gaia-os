# CHALICE C3-SPEC — graphics recon (SPEC ONLY, zero code edits this slice)

Goal: answer Pascal's C3 options-page ask — minimal win32u floor, display seam,
GPU reality, atom ladder — grounded in DONOR SOURCE (wine/ @ 93e084a7, "Wine
version 11.13" per VERSION) + reactos/ + this kernel's own zoneA/ sources, not
memory. Anchor: HANDOFF.md (pe8 green, hello7 surface walk, ramfb_gate.py).
⚠ Version skew (same class HANDOFF already flags for ntdll): stock DLLs =
Hangover 11.9; donor checkout = wine 11.13. `chalice/stock/` holds ONLY
ntdll/kernel32/kernelbase/apisetschema — no user32.dll/gdi32.dll/win32u.dll
fetched yet, so the syscall numbers below are read from the wine 11.13 donor's
generated header, NOT yet cross-checked against a real 11.9 binary (that
binary doesn't exist in this tree). Re-verify at whichever atom first fetches
those 3 DLLs, same discipline as the pe6 stock-ntdll walk.

## 1 — Minimal win32u floor: entry points + real syscall numbers

Ground truth, not the runtime call graph of a live game — the **static
Nt-call surface user32.dll/gdi32.dll issue to get one window with a drawable
surface on screen**, read from the two donor call sites:
- `wine/dlls/user32/win.c:391` `NtUserCreateWindowEx(...)` inside
  `WIN_CreateWindowEx`; `class.c:360` `NtUserRegisterClassExWOW(...)` inside
  `RegisterClassExA`; `win.c:347,350,471,482` `NtUserShowWindow(...)`.
- `wine/dlls/user32/button.c:153,234,337` (representative — same pattern
  repeats in edit.c/static.c/listbox.c/combo.c/msgbox.c/dialog.c/mdi.c/
  icontitle.c): `NtUserGetDC`/`NtUserGetDCEx`/`NtUserBeginPaint`/
  `NtUserReleaseDC`/`NtUserEndPaint` — the HDC returned by these IS the
  window's drawable surface handle; no separate "create surface" call needed
  from user32's side, win32u wires HDC↔window internally
  (`wine/dlls/win32u/window.c:5788` `NtUserCreateWindowEx`, calls
  `create_window_handle` + later `window_surface_create`,
  `wine/include/wine/gdi_driver.h:322`).
- `wine/dlls/gdi32/dc.c:375,1954` `NtGdiOpenDCW`/`NtGdiBitBlt` — the actual
  pixel-copy primitive gdi32 issues once it has an HDC.

**Numbers are real, from `wine/dlls/win32u/win32syscalls.h`'s `_WIN64` block
(line 1545-3085, the ARM64/x86-64 variant — NOT the `ALL_SYSCALLS32` block
at line 3-1540, which is 32-bit-arg-width and wrong for this ABI).** Encoding
proven identical to ntdll's own table-select scheme (already live in this
kernel, `zoneA/ntdisp.S:135-136` `and x20,x8,#0xfff` / `ubfx x21,x8,#12,#2`,
donor equivalent `wine/dlls/ntdll/unix/signal_arm64.c:1681-1682`, same two
instructions verbatim): **table index = bits 12-13 of x8, service id = low
12 bits.** win32u registers itself as **table 1**
(`wine/dlls/win32u/syscall.c:184` `KeAddSystemServiceTable(syscalls, NULL,
ARRAY_SIZE(syscalls), arguments, 1)`), so every id below has `table_idx=1`,
and the **low-12-bit value (not the raw 0x1xxx id) is the array index** into
our `nt_service_tables[1]`. `args` = total argument bytes (regs+stack, same
convention this kernel already uses in `ntsvc.c` — e.g. `NtWriteFile` stored
as `9*8`), lifted verbatim from the header, no re-derivation.

| Nt* entry (win32syscalls.h:line) | raw id | table1 idx (id&0xfff) | args(B) |
|---|---|---|---|
| NtUserRegisterClassExWOW :1262 | 0x14eb | 1259 | 56 |
| NtUserCreateWindowEx :878 | 0x136b | 875 | 136 |
| NtUserShowWindow :1472 | 0x15bd | 1469 | 16 |
| NtUserGetDC :1006 | 0x13eb | 1003 | 8 |
| NtUserGetDCEx :1007 | 0x13ec | 1004 | 24 |
| NtUserReleaseDC :1292 | 0x1509 | 1289 | 16 |
| NtUserBeginPaint :810 | 0x1327 | 807 | 16 |
| NtUserEndPaint :959 | 0x13bc | 956 | 16 |
| NtUserDestroyWindow :903 | 0x1384 | 900 | 8 |
| NtUserGetClassName :988 | 0x13d9 | 985 | 24 |
| NtUserSetWindowPos :1450 | 0x15a7 | 1447 | 56 |
| NtUserGetMessage :1054 | 0x141b | 1051 | 32 |
| NtUserPeekMessage :1229 | 0x14ca | 1226 | 40 |
| NtUserTranslateMessage :1499 | 0x15d8 | 1496 | 16 |
| NtUserDispatchMessage :910 | 0x138b | 907 | 8 |
| NtUserPostMessage :1235 | 0x14d0 | 1232 | 32 |
| NtUserMessageCall :1208 | 0x14b5 | 1205 | 56 |
| NtUserCallOneParam :832 | 0x133d | 829 | 16 |
| NtUserCallNoParam :831 | 0x133c | 828 | 8 |
| NtGdiOpenDCW :585 | 0x1246 | 582 | 64 |
| NtGdiCreateCompatibleDC :177 | 0x10ae | 174 | 8 |
| NtGdiCreateCompatibleBitmap :176 | 0x10ad | 173 | 24 |
| NtGdiCreateDIBSection :179 | 0x10b0 | 176 | 72 |
| NtGdiCreateSolidBrush :194 | 0x10bf | 191 | 16 |
| NtGdiCreatePen :189 | 0x10ba | 186 | 32 |
| NtGdiSelectBitmap :622 | 0x126b | 619 | 16 |
| NtGdiSelectBrush :623 | 0x126c | 620 | 16 |
| NtGdiSelectPen :626 | 0x126f | 623 | 16 |
| NtGdiBitBlt :154 | 0x1097 | 151 | 88 |
| NtGdiPatBlt :591 | 0x124c | 588 | 48 |
| NtGdiRectangle :604 | 0x1259 | 601 | 40 |
| NtGdiExtTextOutW :460 | 0x11c9 | 457 | 72 |
| NtGdiDeleteObjectApp :402 | 0x118f | 399 | 8 |
| NtGdiFlush :472 | 0x11d5 | 469 | 0 |
| NtGdiSwapBuffers :662 | 0x1293 | 659 | 8 |

Full win32u universe = **1540 entries** (`0x1000`-`0x1603`,
`win32syscalls.h:1545-3085`) — DComposition/DXGK/RIM/MIT/composition-surface
noise is 90%+ of it (font, printing, ICM, OPM/DRM-adjacent, multi-monitor,
touch — irrelevant to "one drawable window"). List above is the traced-minimum
for RegisterClass→CreateWindow→ShowWindow→GetDC/BeginPaint→one blit→
message-pump-enough-to-not-busy-spin-forever; NOT every symbol user32/gdi32
import — `GetProcAddress`-style lazy resolution + our own stub-fill (already
proven pattern, `zoneA/ntsvc.c:552-561`, `NtStub`→`STATUS_NOT_IMPLEMENTED`)
means unresolved extras surface as one `CHALICE FAULT` line each, burned down
per the pe6/pe7/pe8 precedent — not blocking, not silent.

## 2 — Display driver seam: chosen = none (kernel IS the driver, table 1 only)

Wine's real seam (`wine/include/wine/gdi_driver.h:362`
`struct user_driver_funcs`, `:292` `struct window_surface_funcs`,
`:443` `__wine_set_user_driver`) is a **Unix-side-only** vtable
(`gdi_driver.h:26` `#error The GDI driver can only be used on the Unix side`)
that `winex11.drv`/`winewayland.drv` register into from a `.so` unixlib built
*separately* from their thin PE stub module
(`wine/dlls/winex11.drv/Makefile.in:2-3` `MODULE=winex11.drv` /
`UNIXLIB=winex11.so`, linked `-lwin32u`, i.e. it calls back into win32u's own
Unix-side internals — a live X11 client process talking to a live X server,
routed through wineserver). **None of that substrate exists here** (no Unix
side, no X11, no wineserver, no second process — this kernel is already
single-address-space/direct-call, per HANDOFF's `nt_dispatch` architecture).

`win32u.dll`'s PE side is confirmed mechanically identical to ntdll's: its
`.spec` marks every one of these entries `-syscall`
(`wine/dlls/win32u/win32u.spec:153,877` e.g.
`@ stdcall -syscall NtGdiBitBlt(...)` / `NtUserCreateWindowEx(...)`) — same
winebuild-generated `mov x8,#id; blr [dispatcher]` stub shape as every ntdll
Nt* export this kernel already intercepts. The real semantics
(`window.c`/`dc.c`/`class.c` — 1000s of lines) run **only** in win32u's own
Unix-side unixlib (`Makefile.in:2-3` `MODULE=win32u.dll`/`UNIXLIB=win32u.so`),
which is exactly the C-logic-we-write-ourselves side this kernel already
replaces for ntdll (NtWriteFile, NtCreateSection, etc. are OUR C, not wine's).

**Decision: implement §1's ~30 entries as new native kernel services in
`nt_service_tables[1]`** (already-provisioned, currently zeroed —
`zoneA/ntsvc.c:548,628` `NtServiceTable nt_service_tables[4]` +
`"tables[1..3]: zero-initialized statics -> ServiceLimit=0"`) — same
`nt_svc_table0`/`nt_svc_argtable0` fill pattern as table 0
(`ntsvc.c:552-624`), blitting straight onto the ramfb linear buffer
(`zoneA/fw_cfg.c:24` `RAMFB_FOURCC` = `DRM_FORMAT_XRGB8888`, 640x480,
`kmain.c:309` `FbInit`). **Justification (2 lines): (a) the alternative —
porting winex11.drv's unixlib or writing a new "winegaia.drv" unixlib — only
makes sense if a Unix-process/wineserver substrate exists to plug it into,
and it doesn't; (b) win32u's PE/Unix split means we already keep 100% of our
existing ntdll-service pattern and skip ~1000s of donor lines (window
handle tables, CBT hooks, desktop/wineserver objects) we'd otherwise have to
port for zero benefit — we only need OUR OWN semantics for OUR OWN ~30 ids.**

ReactOS win32k (`reactos/win32ss/win32k.c`, `ntoskrnl/ex/win32k.c`) is a
real in-kernel win32k reference but **x86/x86_64 only, no ARM64 port exists**
in this donor checkout — informational only, not load-bearing for this ABI.

## 3 — GPU reality per target

Current rig, verified in `zoneA/build.sh:142-149`: `qemu-system-aarch64 -M
virt,gic-version=3 -cpu cortex-a72 -m 1G ... -device ramfb`. **No
`-device virtio-gpu-*` of any kind is present** — `ramfb` is a
firmware-config-negotiated dumb linear buffer (`fw_cfg.c:1-5`, guest-RAM
allocated, QEMU only scans it out — no 3D, no command queue, no GPU object
model). Zero GPU substrate exists in the rig today, full stop.

| Target | API | Path needed | Viable now? | Needs |
|---|---|---|---|---|
| MXO (DX9, 32-bit, DXVK-proven per HANDOFF) | Direct3D9→DXVK | DXVK needs a **Vulkan** ICD in-guest | **No** — no virtio-gpu device in build.sh, no Vulkan ICD anywhere in this tree | New QEMU device (`virtio-gpu-gl`/venus) + a guest Vulkan driver that speaks the venus protocol (large Mesa port, C5-metal-adjacent scope), OR a from-scratch software Vulkan ICD (Mesa lavapipe/llvmpipe cross-built as our `aarch64-windows-gnu` PE target) — both are their own multi-slice efforts, not this atom |
| Teardown (OpenGL, x86-64 per HANDOFF) | wined3d-free direct GL | wined3d's `wine/dlls/win32u/opengl.c` + `wine/include/wine/opengl_driver.h` `pOpenGLInit` hook needs a real GL context from *something* | **No** — same zero-GPU-substrate blocker; no GL implementation (hardware-backed or software/Mesa-softpipe-as-PE) exists in-guest | Same two forks as DX9 (virtio-gpu-gl passthrough vs. software Mesa GL-as-PE), independently sized from Vulkan (shares no code) |
| 2D GDI window (this spec's own floor, §1/§2) | NtGdi* direct ramfb blit | none — CPU memcpy/blend, kernel-side | **Yes** — no GPU needed at all, XRGB8888 ramfb already proven (`ramfb_gate.py`, HANDOFF) | nothing; this is the atom ladder below |

Both real GPU forks (venus-passthrough vs. software-Mesa-as-PE) are **bigger
than a C3 atom each** — Pascal's ruling needed before either starts (same
"NO autonomous work" law HANDOFF already states for C3/C4/C5). This spec's
atom ladder (§4) stops at the 2D floor, which is unconditionally buildable
today, and marks the GPU fork as the explicit next decision point.

## 4 — Atom ladder (serial, each bounded + testable)

**G1 — table-1 skeleton (no real handlers yet).** `nt_svc_table1[N]`/
`nt_svc_argtable1[N]` arrays mirroring `ntsvc.c:552-561`'s table-0
init-to-`NtStub` pattern; wire `nt_service_tables[1]` (currently unused
zeroed struct). New opaque handle space for HWND/HDC/HBITMAP/HBRUSH/HPEN
(flat array + kind tag, same shape as the existing 3A handle table). Gate:
one real id (e.g. `NtGdiFlush`, args=0, trivial no-op) + one deliberately
unimplemented id, both routed through table 1, both logged
(`nt_dispatch_log`, `ntdisp.S:166-182` already generic across tables) —
proves table-1 routing end-to-end with zero drawing logic yet.

**G2 — GDI-only floor, no window manager.** Real handlers: `NtGdiOpenDCW`
(screen DC = the ramfb buffer itself, no window needed), `NtGdiPatBlt`/
`NtGdiRectangle` (solid-fill correctness), `NtGdiCreateCompatibleDC`+
`NtGdiCreateCompatibleBitmap`+`NtGdiSelectBitmap` (off-screen buffer) +
`NtGdiBitBlt` (copy compatible-DC → screen DC). Needs stock `gdi32.dll`
fetched (extend `fetch-stock-dlls.sh` TARGETS, same recipe already proven for
ntdll/kernel32/kernelbase). Gate: new `hello9.exe` gets the screen DC, fills
a known rect with a known color directly (no CreateWindow at all yet),
extends `ramfb_gate.py`'s pixel-check list at the new rect's coords.

**G3 — window floor.** Real handlers: `NtUserRegisterClassExWOW` (single
global class-name→proc table, no atom-table donor complexity),
`NtUserCreateWindowEx` (own minimal WND struct: rect + class + wndproc
pointer only — no owner/parent chains, no CBT hooks, no desktop object),
`NtUserShowWindow` (marks visible, triggers one full-window-rect blit onto
ramfb at its screen position), `NtUserGetDC`/`NtUserBeginPaint`/
`NtUserEndPaint`/`NtUserReleaseDC` (return a per-window compatible-DC,
clipped to the window rect, composited onto ramfb on release/endpaint).
Needs stock `user32.dll` fetched (same recipe). Gate: `hello10.exe`
RegisterClass→CreateWindowEx→ShowWindow→GetDC→Rectangle→ReleaseDC,
`ramfb_gate.py` checks pixels at the window's screen-space rect.

**G4 — message-pump floor.** Real handlers: `NtUserGetMessage`/
`NtUserPeekMessage`/`NtUserTranslateMessage`/`NtUserDispatchMessage`
(dispatch = direct call to the WND's stored wndproc pointer — no
cross-process queue, no wineserver), `NtUserPostMessage` (self-enqueue,
single-process single-thread queue, flat ring buffer), enough of
`NtUserMessageCall`/`NtUserCallOneParam`/`NtUserCallNoParam` to satisfy
user32's internal `DefWindowProc`/`SendMessage` plumbing for WM_PAINT/
WM_CLOSE/WM_DESTROY only (widen per fault, same pe6→pe8 burn-down
discipline). Gate: `hello11.exe` runs a real `while(GetMessage())
{Translate;Dispatch}` loop, self-posts WM_CLOSE after N iterations
(deterministic exit, no real input device exists yet — see gap below),
`ret=<code>` on clean `WM_QUIT`.

**Gap, explicitly out of this ladder — input.** No keyboard/mouse device
exists in zoneA at all (virtio-input, PS/2, or otherwise) — G4's gate is
self-triggered, not interactive. Real input is its own recon (a "C3b" spec),
not assumed solved by this one.

**GPU fork (Pascal's ruling required, not autonomous) — opens after G4.**
Per §3: choose venus-passthrough (`virtio-gpu-gl` QEMU device + Mesa venus
guest driver port) vs. software-Mesa-as-PE (lavapipe/llvmpipe cross-built for
`aarch64-windows-gnu`, zero QEMU/host-GPU changes) before either DX9/DXVK
(MXO) or GL/wined3d (Teardown) can move past G4's 2D floor. Both are
multi-slice efforts in their own right — this spec stops here by design.

## Sizing notes
- §1-3 recon (this file) — no code, spec only, this commit.
- G1 ≈ 100-150 loc (arrays + wiring, mirrors ntsvc.c:552-628 exactly).
- G2 ≈ 300-400 loc (4-5 real NtGdi handlers + fetch-script extension) + 1
  test binary.
- G3 ≈ 400-500 loc (WND struct + 5 real NtUser handlers + fetch-script
  extension) + 1 test binary.
- G4 ≈ 250-350 loc (flat message queue + dispatch + partial MessageCall) + 1
  test binary.
- GPU fork: unsized until Pascal's ruling; expect multi-slice either way.
