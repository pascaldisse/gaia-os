# CHALICE C5-M1 SPEC — bare-metal first light on Apple silicon

Milestone M1 = "first light": iBoot → m1n1 → gaia-os.img → FbDrawPattern on
the real panel. RECON+SPEC only; zero kernel edits in this leg. Quarry:
`m1n1/` (AsahiLinux/m1n1, shallow clone @ repo root, gitignored). All
citations file:line against that checkout + our `kernel-arm/zoneA` (read-only).
Claims not receipt-grounded are marked UNVERIFIED.

---

## §1 Chainload payload protocol

- m1n1 = iBoot stage-2 payload, itself a **Mach-O** (header hand-built in
  linker script: m1n1.ld:43-108, segments RODA/DATA); runs at high VA
  `_va_base = 0xFFFFFE0007004000` (m1n1.ld:4).
- **Payloads are appended to m1n1.bin**; scanner walks concatenated blobs:
  `load_one_payload` (src/payload.c:216) magic-detects per blob —
  gzip kernel (payload.c:224) · XZ kernel (payload.c:227) · m1n1 initramfs
  (payload.c:246) · custom logo (payload.c:250) · unknown → stop
  (payload.c:257-260). Entry `payload_run` (payload.c:291) → kboot path.
  Raw uncompressed Image append: UNVERIFIED in this recon (magic branch not
  captured) — leg-1 verifies; compressed path is the receipted road.
- **Boot road for us = the Linux kboot path**: m1n1 builds an **FDT** from
  Apple's ADT and boots the payload per the Linux arm64 boot protocol
  (src/kboot.c, 2885 LOC: FDT assembly throughout — /chosen kboot.c:259-282,
  /memory from dram_min/dram_max kboot.c:381-383, reserved-memory walk
  kboot.c:386-407). Payload receives **x0 = FDT phys ptr, x1-x3 = 0, MMU off**
  (Linux protocol; exact EL at entry — EL2 vs EL1, Apple has no EL3 —
  UNVERIFIED, verify leg 1; m1n1 manages EL2 state: CNTP_CTL_EL02 writes
  exception.c:137-138 imply EL2 present).
- Our boot.S **already speaks this protocol**: Linux Image header at _start
  (zoneA/boot.S:10-21, image_size = _file_size per C1 scar) → gaia-os.img is
  a legal m1n1 kernel payload shape as-is.
- `src/chainload.c` (148 LOC) = different door: re-load a **new m1n1/Mach-O
  image over the running one** (chainload_image src/chainload.c:23: new_base =
  `_base`, appends m1n1 vars, preserves SEPFW via ADT /chosen/memory-map
  chainload.c:38-40). Used by the USB dev loop (§6), not the install path.
- **ADT vs FDT**: ADT = Apple's device tree from iBoot (src/adt.c), stays
  m1n1-internal; payload gets the synthesized FDT. Our kernel parses FDT only.

## §2 Framebuffer handoff → FbInit contract map

- iBoot hands m1n1 `struct boot_args` (src/xnuboot.h:20-47) containing
  `struct boot_video` (xnuboot.h:11-18): `base, display, stride, width,
  height, depth` — all u64. m1n1's own console: 32bpp check
  `(cur_boot_args.video.depth & 0xff) == 32` (src/fb.c:129,138) → **depth
  varies** (30-bit modes exist); fb.stride used in **pixels** in fb.c
  (fb.c:86-102), but the **FDT stride is bytes** (below).
- m1n1 → payload: node **`/chosen/framebuffer`** (src/kboot.c:187), props:
  `reg = {fb_base, fb_size}` (kboot.c:197-202) · node renamed w/ addr
  (kboot.c:205) · `width` = video.width (kboot.c:208) · `height` = fb_height
  (kboot.c:211 — separate var; notch: `apple,notch-height` appended to dcp
  node kboot.c:246-248) · `stride` = video.stride **bytes** (kboot.c:214) ·
  `format` **string** (kboot.c:234 — "x8r8g8b8" family vs 30-bit variant;
  exact strings verify leg 1). m1n1 can reconfigure the display via DCP
  (src/display.c, src/dcp_iboot.c) — `display=` boot var; format/res not
  guaranteed constant across M-series panels.
- **Swap table onto zoneA fw_cfg.c** (read-only contract, current QEMU path:
  FbInit fw_cfg.c:132-153 — fw_find("etc/ramfb"), fb = MAlloc'd, cfg
  width/height/stride pushed TO the device):

  | zoneA today | M1 replacement |
  |---|---|
  | `fb = MAlloc(bytes)` (fw_cfg.c:137) — we allocate, device follows | fb = **given**: FDT /chosen/framebuffer `reg` base; map it, never allocate |
  | `FB_WIDTH/FB_HEIGHT/FB_BPP` compile-time (kernel.h:45-47) | runtime `FbW/FbH/FbStrideB/FbFmt` parsed from FDT props — **IRON: no hardcoded panel dims**; kernel.h constants demote to QEMU-build defaults |
  | `fb_stride_px = FB_WIDTH` (fw_cfg.c:92) | `stride/4` from FDT (bytes→px); **stride ≠ width** on real panels — every row-walk in FbDrawPattern (fw_cfg.c:102-124) switches to runtime stride |
  | XRGB8888 assumed (kernel.h:47) | gate on `format` string; if 30-bit → convert or have m1n1/DCP set 32bpp; garbage-colors risk §7 |
  | `dsb sy` publish (fw_cfg.c:124) | keep — real MMIO-adjacent memory, same discipline; plus MMU mapping must be WC/normal-NC for the fb range |
  | fw_cfg DMA config push (fw_cfg.c:147) | delete — no device to configure; iBoot already lit the panel |
  | `FbBase()/FbWidth()` exports (fw_cfg.c:129-130) | unchanged signatures — zoneC/GaiaPlot consumers never notice |

## §3 Driver-swap table (donor = m1n1 src, read-only)

| component | our touchpoint | donor | LOC (donor) | port est. | key note |
|---|---|---|---|---|---|
| UART | PL011 UART_DR 0x09000000, poll FR bit5 (zoneA/kmain.c:7-14) | Apple s5l: UTRSTAT_TXBE poll + UTXH write (src/uart.c:42-50), base from ADT `/arm-io/uart6/debug-console` or `/arm-io/uart0` (uart.c:22-34); regs src/uart_regs.h (32 LOC, Samsung-style ULCON/UBRDIV uart.c:112) | 187+32 | **S** (~60 LOC) | base must come from FDT (`/chosen/stdout-path`-style or alias) — serial silent until parsed → parse FDT before first puts |
| IRQ ctrl | GICv3 GICD 0x08000000/GICR 0x080A0000, gic_init (kmain.c:163-173) | AIC: compat aic,1/2/3 detect (src/aic.c:159-163), ADT-driven reg geometry (aic.c:47-131) | 212+58 | **S** first light / **M** full | ⭐ first light needs **no AIC at all**: arch-timer fires as **FIQ** on Apple silicon (m1n1 handles timer in exc_fiq, src/exception.c:395-409; clears CNTP/CNTV as "FIQ sources" exception.c:133-138) → reroute our vector table's FIQ slot to the tick handler; AIC deferred to M2 (external IRQs) |
| timer | CNTP cntp_tval_el0/cntp_ctl_el0 (kmain.c:182-187,235) | same generic-timer registers, unchanged (exception.c:134-138,400-409) | 0 | **S** | register code survives; only **delivery** changes (FIQ, above) + EL2-vs-EL1 reg aliasing check at entry |
| block | virtio-blk BlkRW (zoneA/virtio.c:91), fs.c on top | RAM-disk interim §4 | — | **S** | fs.c untouched |
| framebuffer | ramfb fw_cfg (fw_cfg.c:132-153) | boot_args.video → FDT /chosen/framebuffer (§2) | — | **S/M** | the milestone itself |
| (later Macs) | — | dockchannel UART (src/dockchannel_uart.c, 158 LOC) | 158 | M2+ machines only; out of M1 scope |

## §4 RAM-disk interim

- fs.c shape confirmed read-only: RedSea-spirit, sector API only —
  `FsMount` → `BlkRW(SEC_SUPER,...)` (zoneA/fs.c:28-31); all disk contact via
  `BlkRW(u64 sector, void*, int write)` (kernel.h:42, virtio.c:91). **fs.c
  needs zero edits.**
- m1n1 carries our disk.img as the **initramfs payload** (append; detected
  payload.c:246) → handed to the kernel as `/chosen` props
  `linux,initrd-start` / `linux,initrd-end` (kboot.c:275-279, mem-reserved
  kboot.c:282).
- Handoff: boot parses those two FDT props → `RamDiskBase/RamDiskSecs`
  (**IRON: address+size runtime-discovered, defaults only for QEMU build**) →
  `BlkRW` backend swap: memcpy at `RamDiskBase + 512*sector`, bounds-checked.
  Writes live in RAM (volatile until M2 NVMe). Same gaia-os build keeps
  virtio backend under QEMU — backend chosen by FDT probe, not #ifdef where
  avoidable.

## §5 Linker / entry — the 0x40200000 scar goes parametric

- Today: linked at absolute 0x40200000 = QEMU RAM_base+2MB (zoneA/linker.ld:14
  + scar comment linker.ld:1-11; C1: absolute constants vs PC-relative 2MB
  gap). Stack = load+2MB (boot.S:35-37).
- M1 reality: m1n1 places the kernel where **it** decides (kboot allocates;
  M1 DRAM phys base ≠ 0x40000000 — /memory node from dram_min kboot.c:383).
  **No single link address is right.**
- Spec: (a) `LOAD_BASE` becomes a build parameter (IRON — default
  0x40200000 for the QEMU rig); (b) the real fix = **boot-time
  self-relocation**: PC-relative code already survives anywhere (C1 receipt);
  add a reloc pass over absolute literals (compile -fPIE-style, apply
  R_AARCH64_RELATIVE at boot before kmain) — C1 canary law: literal-pointer
  consumers (IAT, fn-ptr tables) are the test. Est **M**.
- mmu_on()/MemInit RAM window + MMIO idmap (kmain.c:65-66 comment: current map
  assumes UART 0x09000000/GICD 0x08000000/virtio) must be driven by FDT
  /memory + discovered device bases — same IRON parameter sweep, one leg.

## §6 Dev loop + install (macOS stays intact)

- **Flash-free iteration = m1n1 proxy mode over USB-C**: live m1n1 idles in
  proxy; host pushes a fresh build each cycle:
  `M1N1DEVICE=/dev/... python3 proxyclient/tools/chainload.py --raw -E <off> gaia-os.img`
  — raw mode + entry offset (default 0x800) proxyclient/tools/chainload.py:9-33
  (`new_base = u.base` :25, SEPFW preserved from ADT :44-47, fb release via
  `iodev_set_usage(IODEV.FB,0)` :39); proxy plumbing = `from m1n1.setup
  import *` (chainload.py:20, proxyclient/m1n1/). Serial console comes back
  over the same cable (uartproxy). Sibling tools: linux.py, run_guest.py,
  shell.py (proxyclient/tools/). Note chainload.py default path expects
  Mach-O (chainload.py:6 "Mach-O loader"); our raw-Image flow uses `--raw`,
  or we wrap gaia-os in the Mach-O shape m1n1.ld demonstrates — leg-1 picks.
- **Install path (dual-boot, non-destructive)** — Asahi-style, UNVERIFIED
  live (no install performed this leg; sources: asahilinux.org/docs +
  github.com/AsahiLinux/docs): installer shrinks the APFS container
  **in place** (data preserved), creates a small stub APFS volume whose
  "kernel" is m1n1 (fuOS), registers it via the sanctioned custom-boot chain
  (1TR recoveryOS: `bputil` Permissive Security scoped **to that volume
  only** + `kmutil configure-boot` style blessing). macOS: untouched, its
  security policy untouched, selectable via power-key-hold boot picker.
  Rollback = delete stub volume + re-expand APFS.
- Loop of record for M1: install m1n1-with-proxy once → all kernel iteration
  over USB, zero reflash, zero macOS risk.

## §7 Risks (top 3) + M1 exit gate

1. **Panel format/stride variance** — depth may be 30-bit (fb.c:129 check
   exists because 32 isn't guaranteed), stride ≠ width*4, notch machines skew
   height (kboot.c:211,246-248). Wrong assumption = garbage colors or sheared
   image. Mitigation: parse `format`+`stride` always, refuse-and-report
   unknown formats over UART (Pleroma-or-black: no wrong-format fallback draw).
2. **Absolute-link scar at unknown base** — C1 proved absolute constants fail
   silently 2MB off; at an arbitrary m1n1 base *everything* absolute is wrong.
   Mitigation: self-reloc pass + literal-pointer canary test in the QEMU rig
   before first hardware push.
3. **Entry-state mismatch (EL2/FIQ/MMU)** — entry EL unverified; timer is a
   FIQ source not a GIC IRQ (exception.c:395); our vectors (boot.S) currently
   treat FIQ as park. A wrong EL assumption = instant silent hang with no
   serial yet. Mitigation: leg 1 = serial-first micro-payload (UART putc only)
   over proxy before any fb/timer code runs.

**M1 exit gate (first-light receipt):** one proxy-loop run, two artifacts,
both committed: (a) photo of the physical panel showing FbDrawPattern's
signature (gradient + red/green/blue bars + white border, fw_cfg.c:102-124);
(b) proxyclient serial transcript showing our banner ("GAIA-OS aarch64 EL1",
kmain.c:828) + the fb line with **runtime-discovered** dims (not 640x480
constants). Both from the same boot. Anything less ≠ first light.

## Leg plan (M1 internal)

1. serial-first payload over proxy (s5l putc, FDT stdout parse) — proves
   entry contract + EL + UART.
2. self-reloc + LOAD_BASE parameter; QEMU rig regression green.
3. FDT parse (tiny flat scanner, ~150 LOC — libfdt not vendored) →
   /chosen/framebuffer + initrd props.
4. FbInit-M1 + FIQ tick reroute → full kmain boots → exit gate run.
