# GAIA-OS file format — one document for everything (DESIGN, not built)
§ origin → Discord 2026-10-19 13:43-13:48: Pascal (codejunky) + Lain17 (WM author, github kostyk348 = Konstantin). Lain17: 'delve more into BTRON concepts beside just TempleOS', 'I like TAD the most', '.eml kinda too since it has MIME sections'. Pascal: 'merge them into the ultimate file format'.
§ status → donor survey VERIFIED 2026-10-19 against primary sources (cites below). Spec v0 → `FILE-FORMAT-SPEC.md`.
§ cite keys →
- `TAD1/2/3` = https://www.chokanji.com/developer/doc/btron3/shared_data/tad{1,2,3}.html (Personal Media BTRON3 spec, ISO-2022-JP; `L` = line in UTF-8 transcode of tad1)
- `WP-BTRON` = https://en.wikipedia.org/wiki/BTRON · `WP-TRON` = https://en.wikipedia.org/wiki/TRON_project (raw wikitext line) · `WP-TRONC` = https://en.wikipedia.org/wiki/TRON_(encoding)
- `RFCnnnn Lnn` = https://www.rfc-editor.org/rfc/rfcnnnn.txt line nn
- DolDoc paths relative to repo root `~/projects/gaia-os`: `shrine/` (Terry donor), `aiwnios/`, `zealos/`
- `EB` = https://github.com/kostyk348/mime-os @3b924e7 (2026-10-16), `docs/FORMAT.md:Lnn`

## §1 donors (all cells VERIFIED unless marked)
| | TAD (BTRON) | DolDoc (TempleOS) | MIME / .eml | EMLBox (Lain17) |
|---|---|---|---|---|
| unit | 実身 jisshin = real object = file holding typed records (TAD1 L58-75; WP-BTRON "Real body/pseudo body"). 仮身 kashin = virtual object = **reference** segment `TS_VOBJ 0xE6` embedded in text/figure TAD (TAD1 L242, L959-964); target content is a separate *link record* (record type 0, TAD1 L75). **Pairing = order of appearance, not id** (TAD1 L964) | entry = `CDocEntry`, 43 types `DOCT_TEXT..DOCT_ERROR` (shrine/Kernel/KernelA.HH:904-948; class :1191). Two-letter cmds: `TX CR SR TB PB CU MK PT CL PL LM RM HD FO ID FG BG FD BD WW HL BK IV UL SX SY CM AN LK BT DA CB LS MA MU HX TR SP IB IS SO HC ER` (shrine/Adam/DolDoc/DocInit.HC:9-12; same list aiwnios/Src/DocInit.HC:9-12, zealos/src/System/DolDoc/DocInit.ZC:9-12). **No "code" entry type** — code is plain text; `MA` = text sent to shell on click (shrine/Doc/DolDocOverview.DD:149-152) | part = headers + body (RFC2045); message = RFC5322 header fields + body | one `.eml` = one entity: sections + KV + append-only delta log (EB L3-5). Envelope `X-Entity-ID`, `X-EML-Type`, `X-EML-Version` (EB L34-44) |
| structure | directed graph, no folder tree; delete by refcount, cycles leak (WP-BTRON "Real body/pseudo body") | linear queue of entries (shrine/Doc/DolDoc.DD:1). Links address by *path / needle / line / symbol*, not id: `FI FA FF FL MN PI PF PL BF DN HI AD`, in-memory `AI AA AF AL` (shrine/Adam/DolDoc/DocLink.HC:3-27; DocInit.HC:19-20); `::/` = boot-drive-absolute (DolDocOverview.DD:109) | tree: `multipart/{mixed,alternative,related,…}` nested (RFC2046 L1185, L1308) | flat dir of containers; "folder" = container `X-EML-Type: System/Directory` with `X-Contains-ID:` and/or `X-Query:` (EB L150-156). Tags = repeated `X-Tag:` (EB L146-148) |
| data | segments: text (TRON chars), figure (vector), image (raster), vobj, 付箋 attrs (TAD1 L242-246). **No table segment** in core TAD id list. Binary, big-endian, even-length, large-segment form ≥65 536 B (TAD1 L203-219). WP-BTRON: shipped impls use little-endian "quasi-TAD"; images uncompressed except MH | text with `$CMD+FLAGS,args$`; literal `$` is `$$` (DolDocOverview.DD:5-9; DocFile.HC:137). Binary (sprites) = `CDocBin{num,flags,size,use_cnt,data}` records appended **after a NUL byte** after the text (DocFile.HC:153-165; KernelA.HH:1118-1127); entries bind via `BI=num` (DolDocOverview.DD:167-175). File may be `.Z` compressed (Kernel/BlkDev/DskStrA.HC:22, Kernel/Compress.HC:281) | any `Content-Type`; 7bit/8bit lines ≤998 octets (RFC5322 L363; RFC2045 L360); `binary` CTE not valid in Internet mail (RFC2045 L857-859); base64 ≈+33% (RFC2045 L1300) | parts `X-Encoding: raw\|deflate\|aes` (EB L253-258); head index JSON `{id,ct,name,off,len,enc}` (EB L66-67) |
| strength | length-prefixed segments → unknown segment skippable (WP-BTRON "TAD"); vobj carries display state: view rect, open height, title size/colors (TAD1 L981-1012); n vobj → 1 real object | doc = program: lexer reads entries directly (shrine/Compiler/Lex.HC:123-131); only TEXT/TAB/INS_BIN/INS_BIN_SIZE count as source (Lex.HC:322-323); editor = viewer = browser = cmd line (DolDoc.DD:3); help = `LK` | self-describing; `multipart/alternative` = same info, n versions, **plainest first, preferred last** (RFC2046 L1330-1345, L1388-1395; reverse allowed L1408-1409); `Content-ID` world-unique (RFC2045 L1435); `cid:` = body part of same message (RFC2392 L66-68, L93); `mid:<msg-id>/<content-id>` = part of another message (RFC2392 L95); `multipart/related` root via `start=` (RFC2387 L134); `Content-Description` per body (RFC2045 L1463; ASCII, RFC2047 for non-ASCII) | hash-chain: `X-Base-Hash` → block `X-Prev-Hash` → trailer `X-Tail-Hash`, sha256 (EB L28-30, L73-76; src/format.rs:101-102); per-writer chains + LWW + RGA lists (EB L187-217, L287-294); ed25519 `X-Writer-Key`/`X-Signature` (EB L273-277); header-only scan ≤4096 B (EB L173-176); `X-Query` (EB L154-156, L178-180); pack/unpack rejects abs/`..` paths (README, src/pack.rs:65-78) |
| weakness | niche. Spec is public (chokanji.com developer docs). 16-bit file ID → ≤64 Ki objects/volume (WP-BTRON "Problems"). No time axis, no general compression, not all apps use TAD (WP-BTRON "TAD"). Not text-safe | no magic/version in file (DocSave writes entries straight, DocFile.HC:56-170). Binary tail after NUL → not grep/model-safe. Absolute `::/` paths → not portable. **Not x86-only**: aiwnios x86_64/aarch64/RISC-V/WASM (aiwnios/README.MD:4); text format is machine-independent | multipart boundary scan; `cid:` scoped to one message (RFC2392 L66-68); no link semantics beyond cid/mid; no live content | byte offsets break on any text edit/EOL change → needs `reindex` (EB L109-110) ✗ vs plain-text-safe; fixed 512 B trailer (EB L86-96); runner runs python3 **unsandboxed** (EB L140); `X-Encoding` private, not RFC2045 CTE; v0.x single author; "46 tests green" (README L36) = author claim, UNVERIFIED by me |

### §1b side facts
- TRON character code: no Han unification, ~150 M code points (WP-TRONC). TAD text = 1-2 B chars, `0xFE…` language code, `0xFF` escape = variable segment (TAD1 L172-176, L203-206). Spec v0 picks UTF-8 (open Q).
- History: Apr 1989 USTR report calls BTRON a trade barrier, asks Japan not to standardize in schools; TRON on Super-301 list, removed after USTR visit May 1989; Jun 1989 Japan accepts → school standard ends (WP-TRON raw L26-30).
- Product chain: Personal Media B-right/V = BTRON3 impl; Chōkanji = product containing it (WP-BTRON intro).
- Lain17 other repos (READMEs; pushed 2026-10-16/17):
  - `eml-linux` @24c765a: "everything is a message"; units/log/packages/IPC = `.eml`; log events carry `X-Emllog-Prev-Hash`/`X-Emllog-Hash`; filename `<seq>.<id>.msg.eml`; EML-IPC compatible (README §Формат).
  - `capsuleos`: each note = RFC 5322 `.eml`: `Message-ID`, `In-Reply-To`, `X-Capsule-Tags`, SHA-256 sums (README §Zero-DB); `[[wikilinks]]` backlinks; **file holds JSON blocks + HTML fallback + attachments** (README §Vault).
  - `chronicle-engine`: `Message-ID`, `In-Reply-To`, `X-TTRPG-Round/HP`, `X-TTRPG-Checksum-SHA256` (README L18).
  - `eml-scada`: historian = tagdb records, alarms = hash-chain deltas, bus = `.eml` (README table).
  - `backup`/`bm`/`cash`: apps on emlbox; `bm` record id = `bm_<sha256 url>` (bm README §Свойства) = content-addressed dedup key.
  - `DSO-TRON`: μITRON-4.0-derived RTOS **kernel** (README L10) — no file-format content → not a format donor.

## §2 corrections vs dddfbb0 draft
| draft | now | cite |
|---|---|---|
| vobj = "link/embed of a real object" | **reference only**; content in separate link record; paired **by order of appearance** (fragile) | TAD1 L959-964 |
| TAD bus = "text, figure, image, table" | text, figure, image, vobj, 付箋; **no table** | TAD1 L242-246 |
| "every app reads+writes" | intent yes; real apps partly bypass TAD | WP-BTRON "TAD" |
| TRON "killed by US Super 301 ~1989" | USTR Apr-89 report → school standard dropped Jun-89; Super-301 listing itself removed May-89 | WP-TRON raw L26-30 |
| DolDoc entries "text, link, sprite, macro, tree, code" | 43 types; no `code`; code = text | KernelA.HH:904-948 |
| DolDoc "single machine, x86" | text is machine-independent; aiwnios aarch64/RISC-V/WASM. Real weakness: no version header, abs paths, binary tail | aiwnios/README.MD:4; DocFile.HC:56-170 |
| `$LK,...$`-style (UNVERIFIED) | `$LK,"tag",A="TYPE:args"$`; types FI FA FF FL MN PI PF PL BF DN HI AD (+AI AA AF AL) | DocLink.HC:3-27; DolDocOverview.DD:102-120 |
| DolDoc "source files are DolDoc" | `.HC/.DD` lexed as docs; `.Z` = compressed | Lex.HC:123; DskStrA.HC:22 |
| MIME "flat semantics" | **tree**; weak point = *link* semantics (cid local to message) | RFC2046 L1185; RFC2392 L66 |
| MIME "bulky encoding" | base64 +33%; 7bit/8bit/QP avoid it | RFC2045 L1300 |
| MIME alt = "same content, n renderings" | ✓ + order rule: plainest first, richest last | RFC2046 L1388-1395 |
| 3 donors | **4th donor: EMLBox** | EB |

## §3 merge sketch → `FILE-FORMAT-SPEC.md`
- container ← MIME (+ EMLBox: hash-chain, X-Tag/X-Query, ed25519, per-writer revisions)
- object model ← TAD (vobj by **id**, not order) via `Content-ID` / `mid:`
- live ← DolDoc idea: HolyC part (zoneC JIT + AOT, README.md:30-34)
- agent fit ← alternative rule + `Content-Description`

## §4 open → spec §10
