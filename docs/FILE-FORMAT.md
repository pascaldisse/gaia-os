# GAIA-OS file format — one document for everything (DESIGN, not built)
§ origin → Discord 2026-10-19 13:43-13:48: Pascal (codejunky) + Lain17 (WM author, github kostyk348). Lain17: 'delve more into BTRON concepts beside just TempleOS', 'I like TAD the most', '.eml kinda too since it has MIME sections'. Pascal: 'merge them into the ultimate file format'.
§ status → idea + donor survey only. Donor facts below = Lampas from training memory, UNVERIFIED against primary specs → verify before building.

## §1 donors
| | TAD (BTRON, 1980s→) | DolDoc (TempleOS) | MIME / .eml (RFC 2045-2049, 5322) |
|---|---|---|---|
| unit | 実身 jisshin = real object; 仮身 kashin = virtual object (link/embed of a real object, n× anywhere) | DolDoc entry (text, link, sprite, macro, tree, code) | MIME part (Content-Type + body) |
| structure | network, no folder tree; any doc embeds any doc | linear doc of typed entries; hyperlinks to file/fn/line | tree: multipart/{mixed,alternative,related} nesting |
| data | one data bus (text, figure, image, table) every app reads+writes | text+graphics+live code in one file; source files are DolDoc | any type; text-safe transport (base64/QP), headers per part |
| strength | universal interop: every app edits every doc | doc = program; editor = shell = help | survives any text pipe; self-describing parts; alternatives (same content, n renderings) |
| weakness | closed/niche (TRON killed for schools by US Super 301 pressure ~1989 → Chōkanji, Personal Media) | single machine, x86, no transport/versioning | flat semantics, no live content, bulky encoding |
- TRON character code → multilingual set without CJK unification → relevant for §2 text layer.
- TempleOS links: `$LK,...$`-style inline commands in plain text (UNVERIFIED exact syntax → read shrine/ or zealos/ donor tree before spec).

## §2 merge sketch (GAIA document, working name)
- **container** ← MIME: headered parts, Content-Type per part, `multipart/alternative` = same object in n renderings (text for model, pixels for screen). Plain-text safe → readable by a model + grep-able without a parser.
- **object model** ← TAD: every part = real object with stable id; any doc embeds any part by id (virtual object), no copy. Folder tree = optional view, not the store.
- **live content** ← DolDoc: parts may be HolyC (zoneC JIT) → doc runs; editor = shell = help.
- **agent fit** (GAIA-OS thesis: whole OS fits one context window) → text-first parts are model-readable; binary parts carry a text alternative.

## §3 open
- id scheme for real objects (content hash vs uuid) · versioning · how virtual-object links survive copy to another machine · charset: UTF-8 vs TRON-code ideas · relation to RedSea fs in zoneA · DolDoc compat (read existing .DD/.HC unchanged?).
- next → verify donors against sources (TAD spec from Personal Media / TRON Forum; TempleOS DolDoc in shrine/zealos trees; RFC 2045-2049) → then a spec.
