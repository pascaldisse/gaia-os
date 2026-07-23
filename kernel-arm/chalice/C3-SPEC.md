# CHALICE C3-SPEC — GPU road: Venus/virtio-gpu for Teardown(GL)+MXO(D3D9)

RECON + SPEC ONLY. No kernel edits, no QEMU boots. Supersedes the prior
software-Mesa-as-PE ruling recorded in this file's git history (commits
`91a8d88` C3-SPEC graphics recon + `1f7304e` C3-GPU-0 Mesa PE staging) — Pascal
now rules the **Venus protocol → patched-QEMU (KosmicKrisp) → one Vulkan floor →
DXVK + Zink** road. Old lavapipe/llvmpipe-PE recon retrievable from git if the
software path is ever wanted as a fallback; not carried forward here.

## Provenance (fresh this pass)

FRESH-READ donor:
- `kernel-arm/zoneA/build.sh:212-217` — live QEMU rig line.
- `kernel-arm/zoneA/virtio.c` (whole) — the ONLY virtio transport we own.
- `quarry/venus-protocol` — cloned read-only this pass
  (`gitlab.freedesktop.org/virgl/venus-protocol`, depth-1, gitignored):
  `docs/VK_EXT_command_serialization.txt` (stream encoding),
  `include/vulkan/vk_icd.h` (loader↔ICD contract).

FRESH-WEB verified (given road, confirmed):
- `github.com/startergo/homebrew-qemu-virgl-kosmickrisp` — "Venus support:
  Modern virtio-gpu Vulkan transport … includes KosmicKrisp, a Vulkan-to-Metal
  layered driver for Apple Silicon." ✓ tap name, KosmicKrisp = Mesa vk→Metal ICD.
- `github.com/startergo/homebrew-virglrenderer` — Venus via KosmicKrisp; host
  renderer = virglrenderer HEAD (MIT). ✓
- akihikodaki gist `87df4149…` (Linux desktop on Apple Silicon) — "Fixed it by
  switching from **virtio-gpu-gl-pci to virtio-gpu-pci**." ✓ akihikodaki QEMU
  patches; the venus device is `virtio-gpu-pci` (NOT `-gl-pci`).
- `docs.mesa3d.org/drivers/venus.html` — "Venus is a Virtio-GPU protocol for
  Vulkan command serialization. Protocol + codegen at venus-protocol; renderer
  at virglrenderer. … experimental." ✓

CITE-AT-IMPLEMENTATION (not clone-verified this pass, marked ⚑): full Mesa
`src/virtio/vulkan/` (vn_ring, vn_renderer_virtgpu, generated vn_protocol) — huge,
Linux/drm-coupled, NOT cloned; virtio-gpu device spec §5.7 (blob/context-init).
Every ⚑ below = re-derive against a real clone at that rung, same discipline as
HANDOFF's pe6 stock-ntdll walk.

⚠ Version/host skew: KosmicKrisp = macOS 15+ host only; venus+virglrenderer both
"experimental" upstream. Whole road = QEMU-rig-only; bare-metal Apple AGX = C5
throne road, OUT OF SCOPE here.

---

## 1 — Minimal guest-side Venus driver contract

The guest (GAIA-OS) must itself BE the venus front-end: no Linux, no Mesa drm,
no `/dev/dri`. All existing venus implementations are Mesa-on-Linux → this is
OUR from-scratch work. Minimal chain to a first triangle:

**(a) Device discovery.** Tap ships `-device virtio-gpu-pci,vulkan=on` (akihikodaki
road ✓). virtio-gpu = virtio device-id **16** (`VIRTIO_ID_GPU`). On PCI it is a
virtio-modern PCIe function (vendor `0x1af4`, device `0x1050`) on the `virt` GPEX
host bridge. ⚑ TRANSPORT FORK — decide at R0:
- (i) `virtio-gpu-pci` = PCIe → needs PCIe ECAM enumeration + virtio-pci
  capability walk (common/notify/isr/device-cfg caps in BARs) + BAR mapping. We
  have NONE of this (see §2).
- (ii) `virtio-gpu-device` = virtio-**mmio** variant → rides the transport
  `virtio.c` ALREADY drives (device-id match at `0x0a000000+i*0x200`). ⚑ MUST
  test whether `vulkan=on`/venus is offered over the mmio variant on this tap —
  if yes, PCI is avoided entirely and R0 collapses to a device-class add.

**(b) Virtqueue setup.** virtio-gpu exposes **controlq (0)** + **cursorq (1)**;
venus adds per-context command submission. `virtio.c`'s handshake
(`build.sh` handshake at `virtio.c:65-75`: ACK→DRIVER→FEATURES_OK→DRIVER_OK) +
split-ring layout (`VqDesc desc/avail/used`, `virtio.c:38-49,77-83`) is the
correct SHAPE — but is hardcoded single-queue, `QSZ=8` (`virtio.c:53-55`),
blk-specific static `vq`. Venus needs: multi-queue, larger rings, and feature
negotiation of **`VIRTIO_GPU_F_VIRGL`(0) + `VIRTIO_GPU_F_CONTEXT_INIT`(4)**
(virtio.c today accepts only `VIRTIO_F_VERSION_1`, `virtio.c:68-72` — must widen).

**(c) Context + capset.** Venus needs a 3D context bound to the venus capset:
⚑ `VIRTIO_GPU_CMD_GET_CAPSET_INFO` → find `VIRTIO_GPU_CAPSET_VENUS` (=4);
`VIRTIO_GPU_CMD_CTX_CREATE` with `context_init` carrying `capset_id=VENUS`.

**(d) Venus command serialization** — the real contract, donor-exact from
`quarry/venus-protocol/docs/VK_EXT_command_serialization.txt`:
- Command stream = variable-length records: `DW0=VkCommandTypeEXT`,
  `DW1=VkCommandFlagsEXT`, `DW2..N = params serialized in order`. Length is
  IMPLICIT per command type (cannot skip unknown types).
- Reply stream (when `VK_COMMAND_GENERATE_REPLY_BIT_EXT` set): `DW0=type`,
  then return value + non-const out-params in order.
- Scalars little-endian, padded to 32-bit; floats reinterpret-cast; enums as
  `int32_t`; `size_t` as `uint64_t`; object handles as `uint64_t` object-id
  (0 = NULL/`VK_NULL_HANDLE`).
- ⚑ Transport of the stream to host: Mesa maps a shared **ring** in a host3d
  **blob resource** (`RESOURCE_CREATE_BLOB` + `RESOURCE_MAP_BLOB` into a guest
  window) and submits execbuffers; on Linux via `DRM_IOCTL_VIRTGPU_EXECBUFFER`.
  We have no drm → we speak the raw virtio-gpu control command
  `VIRTIO_GPU_CMD_SUBMIT_3D` (context cmd buffer) directly on our own queue.
  Re-derive exact blob/ring mechanics vs raw SUBMIT_3D against Mesa
  `src/virtio/vulkan/vn_renderer_virtgpu.c` + virtio-gpu spec §5.7 at R1.

**(e) First-triangle subset** (what R1/R2 actually serialize): `vkCreateInstance`,
`vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties` (R1 probe);
then `vkCreateDevice`, `vkCreateShaderModule`×2, `vkCreateRenderPass`,
`vkCreateGraphicsPipelines`, `vkCreateImage`+`AllocateMemory`+`BindImageMemory`
(offscreen RT), `vkCreateFramebuffer`, `vkAllocateCommandBuffers`,
`Begin/CmdBeginRenderPass/CmdBindPipeline/CmdDraw(3)/EndRenderPass/End`,
`vkQueueSubmit`, `vkWaitForFences` (poll a venus fence), then readback. A small
but non-trivial slice of the venus command universe — NOT the whole codegen.

---

## 2 — Kernel floor: have vs need (cite zoneA, read-only)

HAVE (`kernel-arm/zoneA/virtio.c`, whole; `build.sh:212-217`):
- virtio-**mmio** modern-v2 discovery loop, base `0x0a000000` stride `0x200` ×32
  (`virtio.c:5-7,59-64`); magic/devid/version gate (`virtio.c:62`).
- Full status handshake + FEATURES_OK check (`virtio.c:65-75`).
- Split-virtqueue: desc/avail/used, queue-select + desc/avail/used addr program
  + QREADY (`virtio.c:38-49,77-83`), cooperative **poll** (NO_INTERRUPT,
  `virtio.c:85,106`).
- `MMIO32` accessor; `dsb sy` barriers; `mem.c` phys alloc; flat-map MMU.
- ramfb 2D scanout (`fw_cfg.c`, `kmain.c FbInit`) — 2D ONLY, no command queue.

NEED (none present):
- **PCIe substrate** (IF R0 picks `virtio-gpu-pci`): GPEX ECAM enumeration
  (virt ECAM window), virtio-pci capability parse (common/notify/ISR/device-cfg
  BAR caps), BAR sizing/mapping, optional MSI-X. ZERO PCI code anywhere in zoneA
  (grep clean). Biggest single add on the PCI branch; the parallel C4 lane does
  not provide it. AVOIDABLE if R0 proves mmio-venus works.
- **Generic multi-queue virtqueue allocator** — current `vq` is one static
  blk-shaped ring; venus needs controlq+cursorq+bigger rings, dynamically sized.
- **Host-visible blob / shared-memory mapping** — venus rings + host-mapped
  memory live in `RESOURCE_MAP_BLOB` regions (PCI SHM cap or a device window);
  MMU must map them; virtio.c has no notion of device-side shared memory.
- **virtio-gpu device class** — control-command encode/decode
  (GET_CAPSET_INFO, CTX_CREATE, RESOURCE_CREATE_BLOB, SUBMIT_3D, fences),
  feature bits `VIRGL`/`CONTEXT_INIT` (widen `virtio.c:68-72`).
- **Guest→device syscall seam** for the ICD (see §3): a private Nt-path the
  guest-user Venus ICD calls to hand command streams to the kernel driver.
- Poll model is fine for a rig (fences via `Yield` loop, like `BlkRW`).

---

## 3 — Wine-side chain: winevulkan → our Venus ICD → DXVK/Zink

Key correction to the naive "winevulkan" framing: **winevulkan is a Wine
UNIX-side thunk** (`winevulkan.dll`→`winevulkan.so`→host `libvulkan`). GAIA-OS
has NO Unix side (HANDOFF: single-address-space, direct-call). So winevulkan's
role is REPLACED, exactly as the prior C3-GPU-0 leg already established for the
software-Mesa ICD: a genuine **PE Vulkan ICD** discovered by the WINDOWS Vulkan
loader (`vulkan-1.dll`) via an ICD manifest JSON + registry — same
`PeMapImage`/manifest mechanism already staged for lavapipe
(`stock-gpu/lvp_icd.aarch64.json`, C3-GPU-0). Contract donor-exact from
`quarry/venus-protocol/include/vulkan/vk_icd.h`: our ICD PE must export
`vk_icdNegotiateLoaderICDInterfaceVersion` (loader↔ICD version, current=**7**,
`vk_icd.h:39`) + `vk_icdGetInstanceProcAddr` (`vk_icd.h:52-54`).

Chain, per game:
```
MXO(D3D9,32-bit) → DXVK d3d9.dll → vulkan-1.dll(32) → OUR Venus ICD(32) → NtGpu* → virtio-gpu(venus) → QEMU/virglrenderer → KosmicKrisp → Metal
Teardown(GL,x86-64) → Zink opengl32.dll → vulkan-1.dll(64) → OUR Venus ICD(64) → NtGpu* → (same host stack)
vk-probe(native aarch64) → vulkan-1.dll → OUR Venus ICD → NtGpu* → (same)
```
- Our Venus ICD = the §1 driver, packaged as a PE DLL. Its backend serializes
  venus streams (§1d) and hands them to the kernel virtio-gpu driver via the §2
  `NtGpu*` seam (cleaner than mmapping the blob into user for a first triangle).
- **DXVK** (MXO): prebuilt `d3d9.dll`, loaded like `stock/` DLLs; 8800GTX-spoof
  DXVK config already in Pascal's hand. Needs a **32-bit** DXVK + 32-bit
  `vulkan-1` loader + **32-bit build of our ICD** — and rides the FEX x86 door
  (C4), which per HANDOFF C4-32 has NOT yet executed a guest x86 instruction.
- **Zink** (Teardown): Mesa GL-on-Vulkan as `opengl32.dll` (mmozeiko build-mesa
  `mesa-zink-arm64` asset class — the C3-GPU-0 leg noted it un-staged; stage it
  for this branch). Teardown x86-64 → also rides C4's 64-bit path.
- Only the **native-aarch64 vk-probe/triangle (R1/R2)** is reachable BEFORE the
  C4 x86 door lands; R3/R4 gate on C4.

---

## 4 — Rung ladder (serial, each gated)

**R0 — transport decision + device bring-up (NO Vulkan yet).** On the tap QEMU:
determine venus transport (⚑ mmio `virtio-gpu-device,vulkan=on` vs PCI
`virtio-gpu-pci,vulkan=on`). Kernel enumerates the virtio-gpu device (devid 16),
completes status handshake, negotiates `VIRGL`+`CONTEXT_INIT`, reads capsets,
finds `CAPSET_VENUS`. If PCI required → sub-rung **R0b**: minimal PCIe ECAM +
virtio-pci cap parse + BAR map. GATE: serial log "venus capset found, ctx
created", no fault.

**R1 — vk-probe exe.** Guest exe: load `vulkan-1.dll`, `vkCreateInstance`,
`vkEnumeratePhysicalDevices`, print device name. Exercises loader→ICD
negotiation (`vk_icd.h`) + instance/physdev venus streams round-trip. GATE:
prints the KosmicKrisp/venus GPU name; `ret=0`. (Native aarch64 first — no C4.)

**R2 — triangle.** Headless `vkCreateDevice`→pipeline→`CmdDraw(3)` into an
offscreen image; readback checksum OR scanout onto ramfb/virtio-gpu. GATE:
known-pixel check (extend `ramfb_gate.py` style) of the triangle.

**R3 — DXVK d3d9 clear.** 32-bit exe: `Direct3DCreate9`→`CreateDevice`→
`Clear(color)`→`Present` via DXVK. GATE: cleared color on screen. DEPENDS ON C4
(32-bit guest execution) + 32-bit ICD/DXVK/vulkan-1.

**R4 — MXO menu.** MXO 32-bit under FEX(C4)+DXVK+our ICD → reach main menu
render. GATE: menu frame presented. DEPENDS ON C4 fully (HANDOFF C4-32: not yet).

**Teardown branch (after R2/R3).** Stage `mesa-zink` PE as `opengl32.dll`;
Teardown x86-64 GL → Zink → our ICD. GATE: Teardown first frame. DEPENDS ON C4
(64-bit x86 door).

---

## 5 — Sizes / risks per rung

| Rung | Size (loc, rough) | Risk |
|---|---|---|
| R0 (mmio branch) | ~300-500 (gpu device class over existing virtio.c) | LOW-MED: reuses transport |
| R0b (PCI branch, only if forced) | +600-1000 (ECAM + virtio-pci caps + BAR/MSI-X) | HIGH: all-new floor |
| R1 | ~1-3k (hand venus subset: negotiate + instance/physdev + ring/blob transport + NtGpu seam) — OR much more if porting Mesa vn_protocol | HIGH: the from-scratch venus front-end, largest unknown |
| R2 | +1-2k (device/pipeline/renderpass/cmdbuf/fence command types) | MED-HIGH: wide command surface |
| R3 | ~0 new (DXVK prebuilt) + 32-bit ICD/loader build | HIGH: gated on C4 32-bit door (not done) + 32-bit Vulkan subset depth |
| R4 | config/integration | HIGH: full MXO + C4 + DXVK stack |
| Teardown | stage zink PE + opengl32 wiring | MED: gated on C4 64-bit + Zink Vulkan feature demands |

QEMU-rig-only caveat: entire ladder assumes the patched KosmicKrisp QEMU tap on
a macOS-15+ host. Bare-metal Apple AGX (a native guest Vulkan→AGX driver) = C5
throne road, explicitly OUT OF SCOPE.

---

## Top-3 risks

1. **The guest Venus driver is OUR from-scratch work.** Every venus
   implementation is Mesa-on-Linux, coupled to drm/virtio
   (`vn_renderer_virtgpu`→`DRM_IOCTL_VIRTGPU_EXECBUFFER`, dma-buf, gem) and to
   Mesa's generated `vn_protocol` codegen. On a bare NT-on-virtio floor with no
   drm we must either reimplement the ring/blob/fence transport by hand or
   heavily de-Linux Mesa. This (R1) is likely the whole slice's weight.

2. **PCI transport gap.** The tap ships `virtio-gpu-pci`; the kernel has ZERO
   PCIe (only virtio-mmio, `virtio.c`). If venus won't ride the mmio variant
   (R0 ⚑ fork), we must add PCIe ECAM + virtio-pci cap parsing + BAR/MSI-X — a
   substantial new floor the parallel C4 lane doesn't provide. R0 must settle
   this before R1 can move.

3. **32-bit / C4 dependency.** MXO(32) + Teardown(x86-64) both ride the FEX x86
   door, which per HANDOFF C4-32 has not yet executed a single guest x86
   instruction. R3/R4 + Teardown are BLOCKED on C4. Only native-aarch64 R1/R2
   are reachable now — and R3 additionally needs a full 32-bit DXVK/vulkan-1/ICD
   stack. Plus host lock-in: KosmicKrisp is macOS-15+-only, whole road
   QEMU-rig-only.
