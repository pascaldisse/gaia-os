# PHONE-VERDICT — GAIA-OS on iPhone 14,4 (A15, iOS 26.6.2)

recon only · 2026-10-30 18:45 CET · target device: iPhone 14,4 = iPhone 13 mini (A15 / t8110, 5c GPU)
read-only on the OS. no code. no optimism.

§0 verdict first
▸ **NO.** GAIA-OS does not go on this phone. not in 30 min, not in our lifetime.
▸ reason chain: no A15 SecureROM bug → no unsigned iBoot → no unsigned kernelcache → and even with iBoot, SPTM (GL2) + TXM + SEP deny it.

---

## 1 · BootROM — A15 state

**findings:**
▸ usbliter8 (Paradigm Shift, 2026-06-22) → **A12 + A13 + S4/S5 only.** full stop.
▸ A14+ fix is structural: SecureROM configures **DART correctly**. the DMA underflow primitive (`DWC2` 4th-setup-packet pointer rollback → SRAM OOB write) cannot reach what matters.
▸ A11 is *also* immune to this bug for a different reason (its USB driver resets the DMA address per packet) — so usbliter8 ≠ checkm8 extension, orthogonal bugs.
▸ Apple, on record to SecurityWeek: *"iPhone, iPad and Watch models with A14/S6 or newer chips are not affected, and neither are any Mac devices."*
▸ checkm8 (2019) → A5–A11. unpatched-because-unpatchable. irrelevant to us.

**A15 SecureROM CVE: none. not partial, not pending, not theoretical-public. zero.**

**citation:**
- https://www.securityweek.com/new-exploit-bypasses-apples-boot-defenses-affects-millions-of-iphones/ (Apple statement verbatim)
- https://www.infosecurity-magazine.com/news/apple-bootrom-exploit-a12-a13/ ("A14 and later chips appear to configure DART correctly in SecureROM, making the same route unexploitable")

**kill / confirm of the brief:**
▸ ✔ confirmed: checkm8 ≤A11. ✔ confirmed: usbliter8 stops at A13. ✔ confirmed: A14+ DART fixed.
▸ ✘ **KILLED — the derived conclusion that everything on iOS 26 is dead.** it is stale by ~3 months. see §1b.

### 1b · THE BRIEF WAS WRONG — correction (state changed 2026-08-07, before today)
▸ **Dopamine 3.0**, released 2026-08-07 by opa334, = **first jailbreak ever for iOS 26.0 / 26.0.1**, semi-untethered, rootless.
▸ but: **A12/A13 only.** `iOS 15.0 - 17.3.1 (A14 - A17 / M1 - M2)` and `iOS 15.0 - 18.7.1 / 26.0 - 26.0.1 (A8 - A13)` ← gap: **A14+ on 26.x: nothing.**
▸ our A15 tops out at **iOS 16.5** for Dopamine (jailbreakly A15 table; 16.5.1+ = none).
▸ and we're on **26.6.2** — 4 minor versions past the 26.0.1 ceiling anyway, on the wrong SPTM generation.
▸ `applefirmwares.com/blog/ios-jailbreak-status` (2026-10-07) is **materially wrong** for A12/A13-on-26.0.x — do not cite it again.

**citations:**
- https://ellekit.space/dopamine/ (canonical support matrix)
- https://github.com/opa334/dopamine (README: "rootless semi-untethered … iOS 15.0 - 17.3.1 (arm64e), iOS 15.0 - 18.7.1, 26.0 - 26.0.1 (A12/A13) and iOS 15.0 - 18.7.1 (arm64)")
- https://www.macrumors.com/2026/08/07/ios-26-dopamine-jailbreak/ ("326 days")
- https://appledb.dev/jailbreak/Dopamine (device list; version range 15.0–26.0.1)
- https://jailbreakly.com/device/iphone-13 (A15: Dopamine 15.0–16.5; none above)

---

## 2 · SEP / SPTM — what exactly must break

**one sentence:** you must defeat **two** things, not one: (a) the **iBoot Image4 signature check** so an unsigned kernelcache can be loaded at all, and (b) **SPTM** — Apple's GL2 *Secure Page Table Monitor*, which from **iOS 17 on A15+ silicon** replaced PPL and owns *every* page-table modification, memory retyping and (as of iOS 26) stage-2/VMID virtualization — so the kernel cannot mark memory executable; **TXM** then makes the code-signing decision at a lower privilege than the kernel, and **SEP** keeps refusing to hand over the Data-Protection class keys derived from UID + passcode.

**notes that matter:**
▸ usbliter8 and checkm8 both give **AP only** — "usbliter8 does not directly compromise the Secure Enclave". SEP keeps running its own sepOS, untouched.
▸ A SEPROM exploit class exists but is **dead man walking**: `blackbird` → A8/A9/A10/T2 *only*. the only public tool, checkra1n, is A10/T2 + downgrade-only (replay), and Apple added **BPR** (Boot Progress Register) on A10/A10X/A11 from iOS 14 beta 1 to detect USB-booted state, killing passcode-in-DFU. **A11 already exempt.** A15 is 4 generations past the last public SEPROM bug.
▸ Platform Security since A13/M1: **Secure Enclave Boot Monitor** — SEPROM verifies sepOS *and* keeps monitoring; the old "SEPROM verifies then jumps" model is gone. (stack.int.mov anatomy; corroborated.)

**citations:**
- https://github.com/cloverfield11/ios26-hypervisor-research — SPTM from iOS 17 on A15+, GL2/GXF, `has-virtualization`, `sptm_guest_enter`, EL0/EL1/GL1/GL2 map, iOS 26.4.1 iPhone 14 Pro IPSW. **primary-ish (static RE of signed IPSW).**
- https://theapplewiki.com/wiki/Blackbird_Exploit (A8/A9/A10/T2 only; A10 replay-only) — mirror: https://www.theiphonewiki.com/wiki/Blackbird_Exploit
- https://idevicecentral.com/ios-guide/how-to-set-password-on-iphone-8-x-jailbroken-with-checkra1n/ (BPR)
- https://stack.int.mov/a-reverse-engineers-anatomy-of-the-macos-boot-chain-security-architecture/ (SEP Boot Monitor, A13/M1+)
- https://code-gsm.com/home/page?article=127&id=26 (SEP boundary summary — secondary, repair-shop tier, use only for framing)

---

## 3 · iBoot signature bypass on iOS 26

**none found. zero public reporting of an iBoot Image4 bypass, for any A-series chip on iOS 26.**

what *does* exist, and why it does not help:
▸ `Leeksov/usbliter8ra1n` — tethered A12/A13 toolkit: iBoot patcher + SPTM/TXM bypass + kernel patchfinder + SSH ramdisk. table lists iOS 26.5 / 27.0-beta, and carries the note: **"A15+ has SPTM/TXM on iOS 26.5, but A12/A13 do not."** ← that is precisely our wall, stated by the people writing the exploit. 
▸ ⚠ `github.com/Leeksov/usbliter8ra1n` returned **HTTP 404** on direct fetch (2026-10-30 18:46) — content seen only via search index. **treat as unverified / possibly pulled.**
▸ Ghost Wolf Lab (2026-06) iBoot **SMMU/DMA bypass** — M1/M2-class **Macs via PCIe**; needs a physical PCIe device slot. not an iPhone attack surface.
▸ `inferno-ios26` (QEMU/ChefKiss) — emulation of T8030. iOS 26 on A13 = "PPL-only, no SPTM/TXM (SPTM is A15+/M2+)". confirms the generational break in Apple's own binaries. **×2 independent** with the SPTM date.
▸ no CVE, no PoC, no conference talk, no repo claiming an iOS 26 iBoot Image4 bypass on A15.

**sources:** sigreturn.com/blog/ios-chain-of-trust/ · sigreturn.com/blog/apple-security-stack/ · x.com/ptdbugs/status/2072959778972660164 · github.com/20kiwy/inferno-ios26

---

## 4 · Prior art — non-Apple OS on an iPhone (3 cases)

| # | year | what | precondition |
|---|---|---|---|
| 1 | **2008–2010** | **openiboot / bootlace** → Android + Linux on **iPhone 2G / 3G** | SecureROM code-exec on S5L8900 (A4-era; the **iBoot exploit** era). userspace pwn, no signature trust model worth speaking of |
| 2 | **2016–2018** | **Android on iPhone 7** ("**checkra1n'd**") + **Corellium `projectsandcastle`** → Android 10 with **LCD + touch working** | **checkm8** (A11 bootROM = SoC, unpatchable) → pongoOS → arbitrary loader. AP-only; SEP/Data-Protection intact |
| 3 | **2019–2020** | **postmarketOS / Ubuntu Touch booting on iPhone 7** (mainline Linux, not just a ramdisk) | same checkm8 + pongoOS, via **checkra1n + Corellium tools**. tethered every boot |

bonus 4th, same family: **iPhone X / iPhone 8** Linux ports via checkm8 + pongoOS (postmarketOS).

**the pattern, stated once:**
▸ **every single successful case, ever, sits on a bootROM-level code-exec primitive.**
▸ **every such primitive ends at A13 (usbliter8) and last usefully at A11 (checkm8).**
▸ **A14+ has never been reached. A15 has never been reached. no exception in 18 years.**
▸ note also: all three are *dual-boot / tethered* — nothing replaced iOS's stored state, and SEP kept the user data. "non-Apple OS" here means *booted alongside*, never *installed over*.

**sources:** hackaday.com/tag/openiboot/ · gbatemp.net openiboot threads · github.com/corellium/projectsandcastle · tuxphones.com/iphone-7-now-boots-postmarketos-linux/ · linux.com/news/linux-on-iphone-you-can-now-run-postmarketos-on-iphone-7/ · howtogeek.com/mobile-linux-distro-postmarketos-gets-iphone-x-port-more-improvements/

---

## 5 · What we'd have to build from zero (brutal)

assume §1–3 stay empty. this is the actual bill.

**A. A15 SecureROM bug — the entry ticket**
▸ work: full static RE of `secrom.t8110` out of a signed IPSW (`ipsw img4 im4p extract`), fuzz DWC2/USB-DFU state machine, PCIe/ANS/NVMe, DART, boot-trampoline. then hardware fault injection.
▸ equipment: **Raspberry Pi Pico 2 / RP2350** (proven usbliter8 rig — the *cheap* part, ~€10), USB protocol analyzer, **ChipWhisperer-Husky or Riscure Inspector + EM probe station** (€30k–€150k), **FIB + SEM + decap** work (€100k+ or rent a lab), thermal/cold-spray voltage-glitch rig, 10–30 **spare A15 donor boards**.
▸ people: 2–4 senior bootROM RE specialists. the actual humans who ship this are a **dozen globally** (Pangu/Starbug/axi0mX/Luca Todesco/opa334/linushenze-class). not hireable; you don't buy this, you recruit it.
▸ time: checkm8 took ~5 months of *axi0mX's* full attention **after** years of the scene's accumulated tooling. usbliter8 is a **commercial firm** (Paradigm Shift) with a business model in forensics.
▸ P(success): the DART bug was fixed *structurally* in silicon. you are fuzzing for a **second, independent** class of bug in an IC designed in 2021 by a team of hundreds. **not zero. call it low single-digit % over 2 years.**

**B. iBoot Image4 bypass on iOS 26** (only needed if A fails or is partial)
▸ work: Image4/TSS personalization RE, `ApNonce`/`ECID` handling, **boot-manifest/IM4M** forgery, iBoot heap/task-state attacks.
▸ requires: signed-IPSW RE at scale (`blacktop/ipsw`, Ghidra), plus an **iBoot memory-corruption primitive**. nothing public exists on A15/26.

**C. SPTM + TXM + GXF**
▸ work: this is the real cliff. SPTM is a **proprietary ISA extension (GL2/GXF, `GENTER` instruction)**. no public documentation. no emulator. no open disassembler semantics. you need a **bug in SPTM** to retype pages, *and* a **bug in TXM** to get a CS (code-signing) slot, *and* you must survive SPTM auditing page tables live.
▸ prior art: Alfie CG's **Trigon** (iOS 17.3.1 class) got through PPL/CSM by chaining — and that was on a generation SPTM didn't fully cover. On A15/26 the equivalent chain starts from nothing.

**D. SEP**
▸ only needed if you want the *data* or a clean boot. for a bare "Linux boots tethered to a ramdisk" you can **bypass SEP**, as checkra1n did on A10/A11 — but you *cannot* then touch Data-Protection class files, passcode, Face ID data, or set a passcode while untethered. A15 also has the SEP Boot Monitor. P(public A15 SEPROM bug): **~0.**

**E. The rest of the OS (the part people forget)**
▸ A15 hardware you must write/driver from scratch: **t8110 GPU** (no open driver — this is why Project Sandcastle's iPhone 7 was CPU-only and painfully slow), ANS/ISP cameras, PMIC/t8010, touch, baseband, WiFi/BT firmware, DCP-class display, USB-PD charging.
▸ GAIA-OS on top: kernel-arm tree, chalice, the Win32 layer, clipboard primitive. — none of this is the problem. **the bootloader is the problem.**

**F. timeline / money, honest:**
▸ 1 senior RE + 1 silicon engineer minimum, 18–36 months to a *first* boot, **budget €250k–€1.5M**, and the most likely outcome is **no boot at all**. this is Corellium-tier, i.e. a **funded company with a lab**, and Corellium still stopped at checkm8 chips.

---

## 6 · The alternative that works today — Xcode Personal Team on the 13 mini

**hard limits, from Apple's own page (verbatim):**
▸ "You can register up to **10 App IDs**, which expire after **7 days**."
▸ "You can register up to **3 devices**, which expire after 7 days."
▸ "You can install up to **3 apps per device**. Provisioning profiles … **expire 7 days from issuance**. You'll need to rebuild and reinstall your app to your device after expiration."
▸ iOS 16+ requires **Developer Mode** toggled on the device.
▸ **citation:** https://developer.apple.com/support/compare-memberships/ (Developer account overview → "Enable a personal team in Xcode")

**what a sandboxed app CAN actually do — no jailbreak, no entitlements:**
▸ **clipboard** ✔ — `UIPasteboard.general`. read = allowed, but triggers the **paste notification** banner since iOS 14/16 (user can deny). metadata-only sniffing via `detectPatterns(for:inItemSet:completionHandler:)` (`UIPasteboardDetectionPatternProbableWebURL` / `...Number` / `...ProbableWebSearch`) is banner-free but **only those three patterns**. → **this is exactly what our `clipboard primitive (clip.c)` already targets.** it works here.
▸ **local model** ✔/⚠ — **CoreML / MLX-Swift / llama.cpp-on-device** are ordinary framework usage, fully allowed. ceiling is **RAM + jetsam**: a normal foreground app gets a few GB on an A15 with 4 GB total, and anything long-running can be jetsammed. no JIT (`MAP_JIT` needs `com.apple.security.cs.allow-jit` → **not available to Personal Team**).
▸ **files** ⚠ — app **container only** (`Documents/`, `Library/`), plus **user-driven** import via `UIDocumentPickerViewController` / Share Sheet / `UTType` open-in-place. no arbitrary path access to the OS. **iCloud Drive / Files integration is fine; `/var` is not.**
▸ **background** ✘ — no unscheduled background compute. `BGTaskScheduler` gives you opportunistic Windows minutes, iOS decides. an always-on local daemon is impossible in-app.
▸ **screen capture** ⚠ — ReplayKit (`RPBroadcastSampleHandler`) works, but **broadcast-extension jetsam ceiling ≈50 MB** — documented repeatedly on Apple's forums, and `replayd` itself gets jetsammed under load. kills anything ambitious.
▸ **things you will NOT get:** no root, no launchd, no kernel extensions, no driver access, no `/private/var`, no process inspection of other apps, no JIT, no hypervisor entitlement (`com.apple.private.hypervisor` — see §2).

**so the 13 mini gives us:** a **clipboard-aware, file-aware, on-device-model front end**, reinstalled every 7 days, 3 apps max. that is a *client*, not an OS.

---

## 7 · One line

> **We cannot put GAIA-OS on this phone — no public A15 bootROM bug exists, and even with one, SPTM+TXM+SEP on iOS 26/A15 block an unsigned kernel and access to user data; the 13 mini stays a sandboxed 7-day-signed app client, nothing more.**

---

## 8 · Open items / do not trust
▸ `applefirmwares.com` iOS-status page is **wrong** for A12/A13 on 26.0.x → drop as a source.
▸ `github.com/Leeksov/usbliter8ra1n` → **404 on direct fetch**; its A15+ SPTM note is *corroborated* by `inferno-ios26` and by the hypervisor paper, both independent. cite the corroboration, not the 404.
▸ `theapplewiki.com` / `theiphonewiki.com` block automated fetch (403) — content above taken from search-index snippets only. **needs a manual browser check before any external citation.**
▸ no primary vendor/Apple security document was found naming SPTM on A15/iOS 26 in a *release* note; the SPTM claim rests on **static RE of signed IPSWs** (two independent parties). strong, not Apple-confirmed.
