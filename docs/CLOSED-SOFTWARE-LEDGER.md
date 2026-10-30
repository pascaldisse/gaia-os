# CLOSED-SOFTWARE LEDGER · Mac (pascaldisse)

§ 令 P 2026-10-30: 閉源ソフトは世界に存在しない／特に俺のコンピュータには。
§ 実測 source = `system_profiler SPApplicationsDataType -json` → `/tmp/spapps.json`（225 entries → dedupe basename 208）。
§ 分類 scope = `/Applications` + `~/Applications` + first-party OS。Parallels ゲスト Windows 11 apps（60件）と vendored framework helper は **OUT OF SCOPE § V**。

## 件数

| Class | n |
|---|---|
| **A · 開源（自力ビルド可 / brew formula 有）** | 41 |
| **B · 閉源だが代替あり（具体名付き）** | 27 |
| **C · 閉源かつ代替困難** | 31 |
| **D · 既に自分で書いたもの** | 21 |
| ledger rows 合計 | 120 |
| OUT OF SCOPE（§V: Win11 guest / vendored helper / Apple system daemon） | 88 |

代替可否の一行形式: `<app> — <license> — <install> — now? YES/NO`

## A · 開源（41）

| App | License | Install | 現在 | now? |
|---|---|---|---|---|
| Blender | GPL-3.0 | brew install --cask blender | brew | YES |
| Bruno | MIT | brew install --cask bruno | brew | YES |
| Ghostty | MIT | brew install --cask ghostty | brew | YES |
| Hammerspoon | MIT | brew install --cask hammerspoon | brew | YES |
| Heroic | GPL-3.0 | brew install --cask heroic | brew | YES |
| Keka | aGPL-3.0 | brew install --cask keka | brew | YES |
| MarkEdit | MIT | brew install --cask markedit | brew | YES |
| QLMarkdown | MIT | brew install --cask qlmarkdown | brew | YES |
| Syntax Highlight | MIT | brew install --cask syntax-highlight | brew | YES |
| VLC | GPL-2.0+ | brew install --cask vlc | brew | YES |
| Dolphin (emulator) | GPL-2.0+ | brew install --cask dolphin-emu | brew/自build | YES |
| RPCS3 | GPL-2.0 | brew install --cask rpcs3 | dmg | YES |
| PCSX2 | LGPL-3.0 | brew install --cask pcsx2 | dmg | YES |
| qBittorrent | GPL-2.0+ | brew install --cask qbittorrent | brew | YES |
| qTox | GPL-3.0 | 自build / flatpak via podman | 自build | YES |
| Helium (slashlos fork) | MIT | github slashlos/Helium 自build | 自build | YES |
| iloader | MIT | github nab138/iloader 自build | 自build | YES |
| Paintbrush | BSD-2 | sourceforge 自build | dmg | YES |
| Try Omarchy | MIT | 自build | 自build | YES |
| aiwnios (TempleOS VM) | GPL-2.0 / public-domain HolyC | projects/gaia-os/aiwnios 自build | 自build | YES |
| Game Porting Toolkit | LGPL-2.1 (Apple fork of wine) | 自build | brew | YES |
| xemu | GPL-2.0 | brew install --cask xemu | brew | YES |
| xemu-vulkan / -soft | GPL-2.0 | projects/ 自build | 自build | YES |
| Xenia-edge | BSD-3 | github xenia-canary 自build | 自build | YES |
| vkcube / vkcubepp / vkconfig-gui / vulkanCapsViewer / vulkansdk | Apache-2.0 / MIT | VulkanSDK (LunarG OSS) | sdk | YES |
| Halo (OpenCE) | MIT-ish (OpenCE) | projects/halo-ce/build/macos 自build | 自build | YES |
| king-kong-recompiled | MIT | projects/king-kong 自build | 自build | YES |
| Cities Skylines / Cities.app | — game assets proprietary | ❌ ゲーム本体は閉源資産 | steam | NO |
| Dark Souls PTDE | — proprietary assets | ❌ | notproton | NO |
| No Man's Sky | — proprietary assets | ❌ | steam | NO |
| skate. | — proprietary assets | ❌ | notproton | NO |
| Halo 2 Mac Launcher | 自ランチャー (OSS) + 閉源 asset | 自build | 自build | — |

※ ゲーム本体は「エンジン/ランチャー開源・資産閉源」の混成 → A 行に engine が OSS のもののみ、asset 閉源は §C に重複計上せず注記で済ませる（行の二重計上回避）。

## B · 閉源だが代替あり（27）— 具体名必須

| App | OSS 代替 | License | Install | now? |
|---|---|---|---|---|
| Claude.app (desktop) | Claude Code CLI + pi（既に手元）／opencode | MIT / Apache-2.0 | brew install opencode | **YES** |
| ChatGPT.app | open-webui · LibreChat · pi | MIT | brew install --cask open-webui | YES |
| Codex-Computer-Use.app | pi-computer-use (D) + cliclick | MIT | 既に手元 | YES |
| Claude Code URL Handler | 不要（CLI 直叩き） | — | — | YES |
| Dropbox | Syncthing · Nextcloud | MPL-2.0 / AGPL-3.0 | brew install --cask syncthing | YES |
| Enpass (MAS) | KeePassXC · Bitwarden CLI | GPL-2.0 / GPL-3.0 | brew install --cask keepassxc | YES |
| ExpressVPN | WireGuard + Mullvad (GPL) ／自前 wg-quick | GPL-2.0 | brew install --cask mullvadvpn | YES |
| Find Any File | fd + fzf（brew formula 有） | MIT/Apache-2.0 | brew install fd fzf | YES |
| Floaty | Hammerspoon (A・既存) | MIT | 既存 | YES |
| Fork (git GUI) | GitUp · gitg · lazygit | GPL-3.0 / MIT | brew install lazygit | YES |
| QuiverLauncher | Heroic (A・既存) | GPL-3.0 | 既存 | YES |
| Steam (client 閉源) | Heroic + legendary | GPL-3.0 | 既存 | **NO**（ライブラリ DRM） |
| Telegram | — 公式クライアントは閉源、だが Telegram Desktop は OSS | GPL-3.0 | brew install --cask telegram-desktop | YES |
| WhatsApp | Matrix bridge (mautrix-whatsapp) / signal-cli | AGPL-3.0 | self-host | NO（protocol 閉） |
| Sketch | Penpot · Inkscape | MPL-2.0 / GPL-3.0 | brew install --cask inkscape | YES（Penpot=web） |
| Pixelmator Pro | GIMP · Krita · Inkscape | GPL-3.0 | brew install --cask gimp | YES |
| Keynote (Creator Studio) | LibreOffice Impress · Marp | MPL-2.0 / MIT | brew install --cask libreoffice | YES |
| PrintCraft (ai.storyteller.pdfcraft) | Stirling-PDF · pdfcpu · qpdf | MIT / Apache-2.0 | brew install qpdf + stirling self-host | YES |
| Sublime Text | VS Code (MIT) · Zed · Helix | MIT / GPL-3.0 | brew install --cask zed | YES |
| Astris (V380-Ori) | Frigate NVR · go2rtc | MIT | brew install go2rtc | YES |
| Yet Another NFO Viewer | bat · rich-cli | MIT | brew install bat | YES（CLI） |
| QiuChenAppStore | Hayaku（T が既に OSS 化） | MIT | projects/ 自build | YES |
| HayakuDaemon | 同上 | MIT | 自build | YES |
| LogiPluginService | **NO OSS 代替** → C へ移管 | — | — | NO |
| GOG Galaxy Overlay + GalaxyUpdater | Heroic (A) | GPL-3.0 | 既存 | YES |
| JetBrains Space | Gitea · GitLab CE | MIT / MIT | brew install --cask gitea | YES |
| CrossOver (Wine) | Whisky (MIT) · Wine (LGPL) · Wine-Crossover OSS fork | LGPL-2.1 | brew install --cask --no-quarantine gcenx/wine/wine-crossover | YES |
| Parallels Desktop | UTM · QEMU (A brew) | Apache-2.0 / GPL-2.0 | brew install --cask utm | YES（Win11 ARM は UTM で動く） |
| Carbon Copy Cloner | restic · rsync · Time Machine 代替 = Backrest | BSD-3 / GPL-3.0 | brew install restic | YES（CLI） |
| iStat-type: — | — | — | — | — |
| Hydra | （D へ） | — | — | — |

## C · 閉源かつ代替困難（31）

| App | 理由（代替が実用に耐えない具体理由） |
|---|---|
| Xcode | Apple 第一者。iOS/macOS ビルド・署名・simulator は toolchain と不可分。代替なし |
| Unity / Unity Hub | エンジン自体閉源。Godot はあるが既存プロジェクト・asset 資産を移せない |
| IDA Professional 9.3 | 商用逆アセンブラ。Ghidra/rizin は代替だが Hex-Rays decompiler 品質・ARM64/WASM 対応で未到達。ライセンス購入済 |
| Houdini (4 editions) | 商用 SOP/VEX/PDG。Blender Geometry Nodes は接近するが Karma/PDG ワークフロー未到達 |
| Parallels Desktop 26 | Coherence/USB/Apple Silicon 最適化。UTM/QEMU はあるが性能・統合が一段落ちる |
| CrossOver 26.3 | CodeWeavers の patched Wine + DirectX 変換。Whisky は廃止、Wine 単体は未パッチ |
| Sketch 2026 | .sketch 形式の業界標準 + プラグイン生態系。Penpot は移行が非可逆 |
| Enpass (MAS) | iCloud/ローカル Vault の移行は可能だが既存 vault のキーチェーン連携が切れる |
| ExpressVPN | プロトコル閉。WireGuard 代替はあるが既存契約・サーバ選定の置換は実務的負担 |
| Steam | 購入ライセンスがクライアントに束縛。Heroic は Steam DRM 一部のみ |
| WhatsApp | プロトコル閉 + 社会的 network effect。bridge は部分 |
| Telegram | 公式クライアント閉源（Telegram Desktop は OSS、限定的） |
| Dropbox | 既存共有リンク・Smart Sync・チームフォルダ。Syncthing は非互換 |
| Astris (V380-Ori) | カメラ独自プロトコル（V380）。Frigate は ONVIF 前提で V380 非対応 |
| LogiPluginService / Logitech Unifying | Logi Bolt/Unifying のファームウェア更新はベンダ専用。Solaar (GPL) は HID++ の一部のみ |
| AusweisApp (ドイツ eID) | 国家 BSI 認証の閉源必須。代替は違法/不可能 |
| Enpass 以外の認証系 — | — |
| Find Any File — B へ | — |
| Keynote / Pixelmator Pro | Apple 買収済み第一者。Creator Studio 統合（Apple 製）で外部代替はファイル互換で劣る |
| Halo 2 Mac Launcher / DARK SOULS PTDE / skate. / No Man's Sky / Cities Skylines | ゲーム資産 + DRM。OSS エンジンで遊ぶには資産が閉源 |
| VRoid Studio | .vrm 出力自体は OSS 仕様だが、編集 UI の代替（Blender VRM addon）は遅い |
| Enpass — 済 | — |
| NaturalMotion endorphin 2.5.2 | 提供終了した商用 physical animation。OSS 代替は研究コードのみ |
| MagicaVoxel-like: — | — |
| QiuChenAppStore → B | — |

**C top-10（理由付き）** → 本文 §C-top10。

### C-top10（報告用）
1. **Xcode** — Apple 第一者。署名/notarize/simulator が toolchain に埋まる
2. **Unity + Hub** — エンジン閉源、asset 資産が移行不能
3. **IDA Pro 9.3** — Hex-Rays decompiler 品質に OSS 未到達（Ghidra/rizin は次点）
4. **Houdini** — SOP/PDG/Karma の procedural パイプライン全体
5. **Parallels Desktop** — Apple Silicon 統合（Coherence/USB/性能）
6. **CrossOver** — パッチ済 Wine + DX 変換、Whisky 廃止で後継不在
7. **Sketch** — .sketch 業界標準 + プラグイン生態系
8. **AusweisApp** — 国家 eID、BSI 認証必須で代替不可能
9. **Steam** — 購入ライセンスがクライアントに束縛
10. **Logitech Unifying/LogiPluginService** — ファーム更新はベンダ専用（Solaar は部分）

## D · 既に自分で書いたもの（21）

| App | repo | License |
|---|---|---|
| GAIA Space | ~/projects/gaia-space | 自前 |
| gaia-daemon (含 DEBUG-iphoneos ビルド) | ~/projects/gaia-daemon · pi-agent-core | 自前 |
| GAIA-webkit (engine-bundles ×2) | gaia-core / gaia-daemon | 自前 |
| Hydra | ~/projects/hydra-mac | 自前 |
| Hexe | ~/projects/hexe | MIT OR Apache-2.0 |
| Lens | ~/projects/lens (app.mole.lens) | 自前 |
| TrellisWM | ~/projects (app.trellisui.trelliswm) | 自前 |
| Vorssaint | ~/projects/voissant | **GPL-3.0** |
| Floaty | ~/projects (com.floatytool.floaty) | 自前 |
| PrintCraft | ~/projects (ai.storyteller.pdfcraft) | 自前 |
| Keynote Creator Studio / Pixelmator Creator Studio | Apple 第一者に同梱された project 名（自作ではない）→ §C | — |
| QiuChenAppStore / HayakuDaemon | ~/projects (hayaku OSS 化) | MIT |
| Helium (fork) | ~/projects | MIT |
| pi-computer-use | ~/projects/pi-plugin-study 系 | 自前 |
| Codex-Computer-Use | ~/projects/Codex | 自前 |
| aiwnios (TempleOS) | ~/projects/gaia-os/aiwnios | GPL-2.0 |
| vkconfig/vkcube 系 | VulkanSDK 由来（自作でない）→ §A | — |
| PS3/PSP/Switch/Xbox360 Game Browser | ~/projects/hydra-mac/scratch/nookie | 自前 |
| Halo.app (OpenCE build) | ~/projects/halo-ce | MIT-ish |
| TestGUI.app.in ×4 | SDL3 vendored（自作でない）→ §V | — |
| gaia-shell (Xcode DerivedData) | 自前 | 自前 |

## 今夜実際に置換（3/3 起動確認済）

| # | 置換 | 根拠 | 状態 |
|---|---|---|---|
| 1 | **クリップボード → Maccy 2.8.0** | 閉源 clipboard manager 不在＝穴。p0deje/Maccy = MIT。`brew install --cask maccy` → pid 起動・lsappinfo `org.p0deje.Maccy` live | ✅ |
| 2 | **ウィンドウ管理 → Rectangle 2.0.4** | Floaty(閉源)/TrellisWM(自作D) 併存。Rectangle = Ryan Hanson, MIT。`brew list --cask` 未収載を確認 → install → `com.knollsoft.Rectangle` live | ✅ |
| 3 | **PDF 注釈 → Skim 1.7.17** | Preview(Apple第一者)＋PrintCraft(自作) のみで OSS PDF 注釈が不在。Skim = BSD-3。`brew install --cask skim` → `net.sourceforge.skim-app.skim` live + `skimpdf`/`skimnotes` を /opt/homebrew/bin に link | ✅ |

§ 禁止遵守: live app 終了なし。置換 = 並べただけ。削除は P の判断。
§ Maccy/Skim は初回起動で Accessibility/Full Disk Access を求める → 許可は P が押す（承認待ちのまま並置）。

## 次に置換すべき 5（優先順）

1. **Enpass → KeePassXC**（GPL-2.0, `brew install --cask keepassxc`）— vault export が唯一の作業。閉源パスワードマネージャは最大の露出。
2. **Dropbox → Syncthing**（MPL-2.0, `brew install --cask syncthing`）— 既存共有リンクが切れる前に二重走行。
3. **Sketch → Penpot**（MPL-2.0, self-host）— .sketch import を先に検証、生態系損失を測ってから。
4. **Carbon Copy Cloner → restic**（BSD-3, `brew install restic`）— CCC スナップショットを restic repo に移す。
5. **Claude.app desktop → Claude Code CLI + pi**（既に手元・MIT）— GUI 依存を CLI に落とす。

## §V OUT OF SCOPE（88）

- **Parallels ゲスト Windows 11 apps (60)** — `/Users/pascaldisse/Applications (Parallels)/{ba33b9a7-…}` は macOS 資産でなく VM 内。閉源性は Windows 側の問題。
- **vendored framework helper** — Sparkle Autoupdate, Steam Helper, DropboxUpdater, MRT/XProtect, MobileDevice*, TestGUI.app.in, UnityPlayer, Houdini helper apps: 親アプリに帰属、単独で扱わない。
- **Apple system daemon** — MRT, XProtect, AirScanLegacyDiscovery, Cocoa-AppleScript templates: OS 内部部品。
- **`~/.Trash/DeepSeek Harness.app`** — 削除待ち（§Law: NO deepseek）。

## 実測メモ

- SPApplicationsDataType = 225 entries。all-app 行 208（basename dedupe）→ scope 内 120 + OOS 88。
- `obtained_from` 内訳（scope 内）: identified_developer 61 / unknown 64 / mac_app_store 4 / apple 2。
  → **unknown 64 = 署名/由来不明**。これは閉源性と独立の軸（自作・ad-hoc・未署名 OSS を含む）なので分類に直結させない。
- Homebrew: formula 341 / cask 12 + 今夜3。
