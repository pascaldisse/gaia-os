# GAIA Document (GD) — spec v0 (DRAFT)
§ status → v0 draft 2026-10-19. Donor facts + cites → `FILE-FORMAT.md` (keys TAD1, RFCnnnn, EB, DolDoc paths). Example in §11 parsed + rule-checked with python3 `email` (stdlib); HolyC execution + DolDoc importer + signatures NOT built/tested (§12).
§ thesis → whole doc readable by a model with `cat` + `rg`; no parser needed for the text path; same file = screen object.
§ words → MUST/SHOULD per RFC 2119 sense.

## §1 container
- GD file = RFC 5322 message + MIME (RFC 2045-2049). Ext `.gd`; also opens as `.eml`.
- Top `Content-Type: multipart/related; type="text/markdown"; start="<root-cid>"` (RFC2387 L134). Root = first thing a reader processes.
- Bytes: UTF-8, LF on disk (CRLF accepted on read; hashes over LF form; `gd send` converts to CRLF). No NUL. Lines ≤998 octets (RFC5322 L363). Text parts `Content-Transfer-Encoding: 8bit`; non-text parts `base64` (76-col). CTE `binary` forbidden (RFC2045 L857-859). For SMTP: re-encode 8bit → QP/base64 (transport concern, not stored).
- **No byte-offset index in the file.** Rejected EMLBox head/tail index + 512 B trailer (EB L19-30, L86-96, L109-110): any text edit breaks offsets → violates plain-text-safe. Indexes live outside (cache) or as header-only scan (§8).
- Boundaries are real (readers split on them). Boundary string `gd~<first 8 hex of oid>`; MUST NOT occur in any part (writer checks).

## §2 envelope headers (before first blank line, whole block ≤4096 B so header-only scan works; EB L173-176)
| header | req | meaning |
|---|---|---|
| `MIME-Version: 1.0` | MUST | RFC2045 |
| `Message-ID: <oid@gaia>` | MUST | **object id** of this real object (§4) |
| `Subject:` | MUST | title (RFC2047 if non-ASCII) |
| `Date:` | SHOULD | creation, RFC5322 date-time |
| `X-GD-Version: 0` | MUST | format version (DolDoc has none: FILE-FORMAT §1) |
| `X-GD-Type:` | MUST | `doc` \| `dir` \| `app` (app = doc with `X-GD-Run: onopen` part) |
| `X-Tag:` | MAY ×n | flat tags (EB L146-148) |
| `X-Contains-ID: <oid@gaia>` | dir, ×n | explicit members (EB L150-152) |
| `X-Query:` | dir | dynamic members, `field OP value [AND …]`, fields = any envelope header, OP `== != >= <=` (EB L154-156, L178-180) |
| `From:` / `X-GD-Key:` | MAY | author; `X-GD-Key: ed25519:<pubhex>` (EB L273-277) |
- Store = flat directory of `<oid>.gd`, **no tree**; folders = `dir` objects (TAD graph, WP-BTRON; EB L142-156). Index = header-only scan of every file.

## §3 parts
- Every part (leaf or multipart) MUST have `Content-ID`; ids unique in file and world (RFC2045 L1435).
- Part id = `<{oid}.{local}@gaia>`, `local` = dot-atom, e.g. `root`, `logo`, `hello.r2`. oid prefix ⇒ world-unique without registry, and `cid:` resolves from anywhere.
- Part headers: `Content-Type`, `Content-Transfer-Encoding`, `Content-ID` (MUST); `X-Hash` (SHOULD, §9); `Content-Description` (alt groups MUST, §6); `X-GD-*` live (§7); rev headers (§9).
- Unknown `X-` headers/part types MUST be preserved + skippable (TAD: unknown segment skippable; WP-BTRON "TAD").
- Root part: `text/markdown`. Chosen because model-native + `cid:`/`mid:` are plain link targets. No custom markup in v0.

## §4 object ids  [DECISION]
- **Real-object id = random 128-bit UUID** (text form, lower-case) in `Message-ID: <uuid@gaia>`; RFC5322 msg-id `id-left@id-right` grammar (RFC5322 L1383-1387), `@gaia` fixed right side.
- **Content hash is NOT identity.** It pins a *revision* (`X-Hash`, `#sha256=` pin, §5) and dedups immutable blobs.
- Why uuid:
  1. Virtual objects must survive edits. Hash-id changes on every edit → every referrer breaks. TAD semantics: ref follows the object, not its bytes (WP-BTRON "Real body/pseudo body").
  2. Fixed small int ids capped BTRON at 64 Ki objects/volume (WP-BTRON "Problems"). 128-bit random = no allocator, no central registry, offline creation on any machine.
  3. Hash chain (§9) already gives integrity; id need not.
  4. Hash ids stay for immutable things: part `X-Hash`, pins, `bm`-style dedup keys (bm README: `bm_<sha256 url>`).
- Cost accepted: two copies of a file with same oid = same object (fork = new oid + `X-Fork-Of`, not specced v0).
- Import (§10): oid = first 128 bit of `sha256("gd-import:"+source-id)` formatted as uuid → re-import keeps identity (deterministic, not random).

## §5 virtual objects (kashin)
- vobj = markdown link whose target is an object id, **by id never by order** (fixes TAD1 L964).
  - other object: `[title](mid:<oid>@gaia "view=open h=240")` — `mid:` per RFC2392 L95 (message-id = oid).
  - part in same file: `[t](cid:<oid>.<local>@gaia)`; inline media `![t](cid:…)`.
  - part of other object: `mid:<oid>@gaia/<oid>.<local>@gaia` (RFC2392 L95).
- Link title string = display state, key=value space-separated (TAD1 L981-1012 `view`, `height`, chsz/colors): `view=link|title|open|inline`, `h=<px>`. Unknown keys ignored.
- Pin (optional): `…@gaia#sha256=<hex>` = exact revision; no pin = latest resolved revision (§9).
- Resolution: header-only scan of local store for `Message-ID`; miss → show title, mark unresolved; never fail the doc.
- Survive copy to another machine (draft §3 open Q): ids are machine-free ✓. Target file may be absent → **embed-by-copy**: put snapshot in this file as part `<thisoid>.<local>@gaia` with `X-Source: mid:<oid>@gaia#sha256=<hex>`; `cid:` ref points at the copy; `gd refresh` replaces if source resolvable. (EMLBox `pack` = same idea for dirs, EB README.)
- Graph allowed (cycles ok, TAD). Unreachable-object GC = mark/sweep from root dirs (not refcount; WP-BTRON notes refcount leaks cycles). Not built.

## §6 text-alternative rule (binary parts)  [model-readable]
- *Text-safe* types: `text/*`, `application/json`, `*+json`, `*+xml`. All else = *opaque* (image, audio, sprite, compiled code…).
- **R1** opaque part MUST be the 2nd+ child of a `multipart/alternative` whose **1st child is `text/plain; charset=utf-8`** (the *twin*). Order = plainest first, richest last (RFC2046 L1388-1395, L1408-1409 friendliest to non-MIME viewers).
- **R2** alt group has its own `Content-ID` (= the *thing*; refs point here) and `Content-Description:` one line ≤120 chars (RFC2045 L1463; ASCII/RFC2047). Children have different ids (RFC2046 L1425-1438): `<oid.x@gaia>` group, `<oid.x.txt@gaia>` twin, `<oid.x.bin@gaia>` opaque.
- **R3** twin content by kind: image → caption/scene text; audio → transcript; sprite → tag text + size; chart → data table; compiled → its source. Twin SHOULD be ≤2 KiB.
- **R4** model view = per alt group take first child (`gd cat --text`). Screen view = last child it can render (RFC2046 L1388-1395).
- **R5** unreadable twin = invalid doc; `gd lint` rejects opaque part outside alt group.

## §7 live part (HolyC, zoneC JIT)
- `Content-Type: text/x-holyc; charset=utf-8` — source is the text part (R1 satisfied: text-safe). Compiled cache MAY ride as `multipart/alternative {text/x-holyc, application/x-a64-aot}` (plainest first).
- Headers: `X-GD-Run: manual|onopen|onclick:<cid>` (default `manual`); `X-GD-Entry: <fn>` (default `Main`); `X-GD-Caps: <space list>` declared capabilities (default none), e.g. `console.print gfx.draw ipc.send`.
- Host: HolyC → zoneC front end → A64 JIT (README.md:30-34) loads part body from memory, calls entry. Host MUST enforce caps (EMLBox runner has **no sandbox**, EB L140 — do not copy).
- Trust: `onopen`/`onclick` need valid `X-Sig` (§9) from key in local trust set; else treated `manual` + prompt. DolDoc ran everything unsandboxed (single-user OS); GD travels, so no.
- Edit-in-place = append revision (§9). Editor = shell = help (DolDoc.DD:3) ⇒ `app` type opens root + runs `onopen`.

## §8 header-only scan / queries
- Reader stops at first blank line (≤4096 B). Gives: oid, title, type, tags, dir membership. `X-Query` evaluates on these only (EB L173-180). Broken file = 1 skipped record.

## §9 revisions + integrity (EMLBox steal, simplified)
- `X-Hash: sha256:<hex>` over part body as stored (LF, excl. newline before next boundary).
- Append-only edit: new part `Content-ID: <oid.<local>.r<N>@gaia>`, `X-Rev-Of: <orig-cid>`, `X-Rev: N`, `X-Prev-Hash: <X-Hash of previous revision>`, own `X-Hash`. Insert before closing `--boundary--`; no offsets touched. Resolve cid → highest `X-Rev` in an unbroken chain (EB L28-30, L73-76 chain idea; ids stay unique per RFC2045 L1435).
- `gd compact` drops superseded parts (EB L267-271).
- Signature (SHOULD for live parts): `X-Sig: ed25519:<pubhex>:<sighex>` over UTF-8 `"<Content-ID> <X-Hash>"` (binds part to its id; EB signs body only, L273-277).
- Multi-writer merge (EB L187-217: per-writer chains, LWW) = v1, not v0.

## §10 DolDoc import (`.DD`, `.HC`, `.Z`)
Pipeline: `.Z` → TempleOS arc-decompress (`ExpandBuf`, Kernel/Compress.HC; suffix test DskStrA.HC:22) → split text at first NUL, remainder = `CDocBin` records (DocFile.HC:153-165; KernelA.HH:1118-1127: `u32 num,flags,size,use_cnt` + data) → tokenize `$…$` (`$$`→`$`; DolDocOverview.DD:5-9).
Always emit part `<oid.src@gaia>` = original `text/x-doldoc` (verbatim, NUL tail removed) as lossless fallback.
| DolDoc entry (DocInit.HC:9-12) | → GD |
|---|---|
| `TX CR SR TB CU` text/newline/tab | root markdown text (SR/CU dropped) |
| `FG BG FD BD WW HL BK IV UL SX SY CM LM RM HD FO PL PB PT CL ID MK IS ER` | dropped (presentation); `ID` indent + `TR` → nested list; warning list in `X-GD-Import-Lost:` |
| `AN` anchor | `{#name}` heading/anchor; target of FA links |
| `LK` `A="FI:path"` / bare path | path in import set → `[tag](mid:<oid>@gaia)` via path→oid map; else keep `[tag](doldoc:FI:path)` unresolved |
| `LK FF/FA/FL` | as FI + `#find=…` / `#anchor` / `#line=N` fragment |
| `LK MN:sym`, `HI:idx`, `DN`, `BF`, `AD` | `doldoc:<TYPE>:<arg>` (HI → also `X-Query` link if tags mapped) |
| `SP` + `IB` + CDocBin `num` | alt group (§6): twin = `SP` tag text or `sprite <num> (<w>x<h>)`; opaque = `application/x-templeos-sprite` raw bytes base64; ref `![tag](cid:…)`. Optional later: also render `image/png` as richer alt |
| `MA` (`LM="…"`) | live part `text/x-holyc`, `X-GD-Run: onclick:<cid>`, `X-GD-Input: shell` (LM text fed to shell as typed; may not be pure code), label = tag |
| `SO` song | `text/x-templeos-song` (aux_str note text = text-safe) |
| `HC` html | `text/html` part |
| `BT MU DA CB LS HX` forms/widgets | v0: verbatim in `src` only + inline code span; forms = open Q |
| `.HC` source file | single `text/x-holyc` part; inline `$…$` in comments left verbatim (Lex.HC:322 only TEXT/TAB entries are code) |

## §11 worked example (`hello.gd`, 2510 B)
Checked with python3 `email` + rule script (§12). Shows: envelope, markdown root w/ vobj (`mid:`, `view=open h=240`), alt group w/ twin + PNG, live HolyC part + revision r2.
```
MIME-Version: 1.0
Message-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f@gaia>
Subject: Hello GAIA
Date: Mon, 19 Oct 2026 14:30:00 +0200
X-GD-Version: 0
X-GD-Type: doc
X-Tag: example
X-Tag: holyc
Content-Type: multipart/related; boundary="gd~3f9c2a7e";
 type="text/markdown"; start="<3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.root@gaia>"

--gd~3f9c2a7e
Content-Type: text/markdown; charset=utf-8
Content-Transfer-Encoding: 8bit
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.root@gaia>
X-Hash: sha256:28866d466c38a87dcb66735481b32a45a795afb8d9a66d6c3f535d43f2f42b12

# Hello GAIA

A document that is also a program.

Sprite: ![logo](cid:3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.logo@gaia)

See also [the shrine notes](mid:c1d2e3f4-0a1b-4c2d-8e3f-9a8b7c6d5e4f@gaia "view=open h=240") and run [hello](cid:3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.hello@gaia).
--gd~3f9c2a7e
Content-Type: multipart/alternative; boundary="gd~3f9c2a7e.a"
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.logo@gaia>
Content-Description: GAIA logo (1x1 PNG placeholder)

--gd~3f9c2a7e.a
Content-Type: text/plain; charset=utf-8
Content-Transfer-Encoding: 8bit
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.logo.txt@gaia>
X-Hash: sha256:f722d9c557c7e4fdfe7efa461cf3e20326bc6bac7412bf7be2987d4ad239a42f

GAIA logo, one white pixel on transparent ground (placeholder).
--gd~3f9c2a7e.a
Content-Type: image/png
Content-Transfer-Encoding: base64
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.logo.bin@gaia>
X-Hash: sha256:cf500a429bb5ec4d005ecfba8eacdd8cf8ba0c34178967e5131a75376359ed53

iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmM
IQAAAABJRU5ErkJggg==
--gd~3f9c2a7e.a--
--gd~3f9c2a7e
Content-Type: text/x-holyc; charset=utf-8
Content-Transfer-Encoding: 8bit
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.hello@gaia>
X-GD-Run: manual
X-GD-Entry: Main
X-GD-Caps: console.print
X-Hash: sha256:e9509db28702a4696bfa476858d7eb33f6faf2536f522c209406f558e955eb32

U0 Main()
{
  Print("hello from a document\n");
}
--gd~3f9c2a7e
Content-Type: text/x-holyc; charset=utf-8
Content-Transfer-Encoding: 8bit
Content-ID: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.hello.r2@gaia>
X-Rev-Of: <3f9c2a7e-5b1d-4c8a-9e60-1a2b3c4d5e6f.hello@gaia>
X-Rev: 2
X-Prev-Hash: sha256:e9509db28702a4696bfa476858d7eb33f6faf2536f522c209406f558e955eb32
X-GD-Run: manual
X-GD-Entry: Main
X-GD-Caps: console.print
X-Hash: sha256:c5739a0ca2c8166a10dd49f40abd4b5535cf425b4a41b5695821bf5bcdc7e2ad

U0 Main()
{
  Print("hello again\n");
}
--gd~3f9c2a7e--
```

## §12 tested / UNVERIFIED
- TESTED 2026-10-19 (python3 stdlib, /tmp script, not committed): example parses as `multipart/related`; 6 Content-IDs unique + oid-prefixed; `start=`/`type=` resolve; every `cid:` in root resolves; opaque PNG decodes (`89504e47`, 70 B); every `X-Hash` matches; rev chain r2 resolves + `X-Prev-Hash` links; header block ≤4096 B; model view = twin text. Negative: edited body → `hash mismatch`; twin removed → `twin must be first`.
- UNVERIFIED: zoneC loading a part body + `Print`; caps enforcement; `X-Sig` (ed25519 never run); DolDoc importer (mapping table is design from source reading, no code); `mid:` resolver / store scan; boundary-collision writer; SMTP round-trip (8bit → QP); `gd` CLI does not exist.

## §13 open questions
1. Root markup: markdown ok, or keep DolDoc-ish `$CMD$` for widgets (forms need it)?
2. Charset: UTF-8 only vs TRON-code ideas (no Han unification, WP-TRONC). v0 = UTF-8.
3. GD vs RedSea fs (zoneA): store as files on RedSea, or `.gd` = native object and fs = flat oid dir?
4. Live-part trust model: who is in local trust set; revoke; caps vocabulary.
5. Fork/merge: `X-Fork-Of`, multi-writer (EB per-writer chains + CRDT) → v1?
6. Doc-level Merkle root (`X-Root-Hash`) so one hash pins a whole doc.
7. GC for unreachable objects (cycles) — mark/sweep from which roots?
8. Big opaque parts (video): base64 +33% (RFC2045 L1300); external-body (`message/external-body`, Content-ID mandatory RFC2045 L1441) vs sidecar file.
9. Compat: should `.gd` be allowed to be DolDoc-superset so TempleOS/Shrine reads it? (no — importer only.)
10. Lain17 collab: align header names with emlbox (`X-EML-Type` ↔ `X-GD-Type`, `X-Entity-ID` ↔ `Message-ID`) so `emlbox tagdb`/`fs` can scan `.gd`?
