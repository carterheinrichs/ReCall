# ReCall

A resident Windows 11 quick-launcher written in C++20 and native Win32. Search applications, recall text and images from clipboard history, and insert Unicode 16 emoji from a single keyboard-driven palette.

## Build and run

Requires Windows 11 22H2+, Visual Studio 2022 with **Desktop development with C++**, CMake 3.25+, and the Windows SDK (22621 or newer). Use a Developer PowerShell with CMake on `PATH`.

```powershell
cmake --preset windows-x64
cmake --build --preset release
ctest --preset release
.\out\build\Release\ReCall.exe
```

The first configure downloads the pinned SQLite 3.50.4 amalgamation and verifies its SHA-256. SQLite and the MSVC runtime are statically linked; the executable uses only Windows-provided DLLs at runtime. No Windows App SDK, .NET, Python, or separately installed database is required by the app. The emoji dictionary is embedded in the executable.

ReCall starts hidden with a notification-area icon. Press **Win+Space** or click the icon to open it. A second invocation toggles the existing instance.

| Input | Action |
| --- | --- |
| `code` | Fuzzy-search applications |
| `> meeting` | Filter clipboard history, newest first |
| `:` or `:smile` | Browse/search emoji names and category tags |
| Up / Down, Page Up / Page Down | Move selection |
| Enter | Launch or copy and paste the selection |
| Escape, focus loss | Hide the palette |

Click a result to select it; double-click to execute. Images have inline thumbnails and a larger preview. Clipboard text has a multiline preview. The tray menu provides configuration, application-index refresh, and Exit.

## Configuration and storage

Created automatically on first launch:

- **MSIX:** `%LOCALAPPDATA%\Packages\<PackageFamilyName>\LocalState\`
- **Unpackaged:** `%LOCALAPPDATA%\ReCall\`

```json
{
  "hotkey": {
    "modifiers": ["win"],
    "key": "SPACE",
    "hook_fallback": true
  },
  "prefixes": { "clipboard": ">", "emoji": ":" },
  "auto_paste": true,
  "history_limit": 1000
}
```

Restart ReCall after editing `config.json`. Modifiers are `win`, `ctrl`, `alt`, and `shift`; keys are `SPACE`, `A`–`Z`, `0`–`9`, or `F1`–`F24`. Prefixes can contain multiple characters but must be nonempty and cannot prefix one another. Invalid configuration produces a startup error rather than silently resetting settings.

Win+Space is also used by the Windows input-language switcher. ReCall first tries `RegisterHotKey` with repeat suppression. If registration fails and `hook_fallback` is enabled, a low-level keyboard hook consumes the exact configured combination. Choose, for example, Ctrl+Alt+Space if you want to retain the OS shortcut.

`recall.db` stores history in SQLite WAL mode using the specified `clipboard_items` schema. Images are PNG files in `clips_media`. The most recent `history_limit` entries are retained; expired images are deleted and crash-orphaned PNGs are reclaimed at next startup. Text character counts use Unicode code points. Clipboard writes made by ReCall are excluded from capture using the clipboard sequence number.

The listener captures changes while ReCall is running. Worker jobs coalesce superseded updates; it is not an archival capture of every transient clipboard state. Text captures are limited to 16 MiB, decoded images to 128 MiB, search input to 256 UTF-16 code units, and visible search results to 100. Clipboard fuzzy filtering uses the first 8,192 UTF-16 code units of each entry; the complete text is retained and restored. Images are normalized to PNG and restored as 32-bit CF_DIB; original clipboard metadata and alternate formats are not retained.

## Architecture

| Component | Location |
| --- | --- |
| Hidden window, notification icon, hotkeys, keyboard UI, preview painting, activation/paste | `src/app.cpp` |
| UTF-8 JSON configuration and packaged/unpackaged state paths | `src/config.cpp`, `src/json.h` |
| SQLite RAII, schema, retention, PNG cleanup | `src/store.cpp` |
| Clipboard text/DIB/bitmap capture and PNG restoration | `src/clipboard.cpp` |
| Known-folder enumeration, shell-link inspection, 32/64-bit App Paths enumeration | `src/catalog.cpp` |
| Prefix routing and proximity/word-boundary fuzzy scoring | `src/search.cpp` |
| Embedded Unicode 16 emoji data | `assets/emoji.json` |

The palette is created once, hidden, and reused. It is a borderless topmost tool window with per-monitor DPI handling, double-buffered native drawing, Windows 11 rounded corners, and DWM dark-mode/Mica backdrop requests. The opaque dark content surface is intentional; this is a custom Win32 renderer rather than WinUI controls. DWM material appearance depends on system composition settings.

A single background worker owns indexing, clipboard capture, database work, fuzzy search, and bounded preview generation. Stale search generations are discarded. The foreground show path does not wait for indexing or SQLite. Applications are enumerated at startup and on tray-menu refresh; links are inspected using `IShellLinkW` and launched through their original `.lnk` so arguments, working directories, and packaged/advertised targets are preserved.

For clipboard/emoji execution, ReCall populates the clipboard, hides, restores the previously active window, and schedules Ctrl+V after at least 20 ms. It waits for held modifiers/Enter to be released and checks the foreground window again before input synthesis. Set `auto_paste` to `false` for copy-only behavior. Windows foreground restrictions and integrity-level boundaries can prevent focus restoration or input injection; the item still remains available for manual paste.

**Performance:** the persistent window avoids cold-start costs, but the sub-10ms show target is not yet benchmarked or guaranteed. End-to-end paste, OS hotkey conflicts, mixed-DPI displays, and visual material behavior should be checked interactively on target systems.

## MSIX packaging and local signing

```powershell
# Unsigned package (also validates the manifest with MakeAppx)
.\tools\Package.ps1

# Package and sign using a generated development certificate
.\tools\Package.ps1 -Sign

# Or use an existing CurrentUser\My certificate
.\tools\Package.ps1 -Sign -CertificateThumbprint YOUR_CERTIFICATE_THUMBPRINT
```

Artifacts are `out\ReCall.msix` and, when signing, `out\ReCall.Development.cer`. The certificate subject must match `Publisher` in `packaging\AppxManifest.xml` (default: `CN=ReCall Development`). The signing script creates a development certificate only when `-Sign` is supplied without a thumbprint. It does not install the certificate or the app.

For local installation, trust the exported development certificate in **Local Machine → Trusted People** (administrator action), then install the signed package:

```powershell
# Elevated PowerShell, for a certificate you generated and trust
Import-Certificate -FilePath .\out\ReCall.Development.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
Add-AppxPackage .\out\ReCall.msix
```

The MSIX targets x64 Windows 11 22H2+ and declares `runFullTrust`. Development artwork is generated during packaging. Startup-on-login is not enabled automatically; start ReCall from its Start menu entry for each session.

## Checks and data regeneration

```powershell
ctest --preset release
.\tools\Smoke-Test.ps1
.\tools\Update-Emoji.ps1
```

`core_tests` covers fuzzy alignment/ranking, prefix routing, JSON parsing, and Unicode escapes. `storage_tests` exercises real SQLite persistence, WAL, ordering, Unicode text, character counts, retention, and image-file cleanup in an isolated temporary directory. The smoke script launches the actual GUI, verifies hidden startup, opens the palette, submits an emoji query, navigates, dismisses with Escape, and exits. It needs an interactive desktop and no already-running instance; it does not execute a result or alter the clipboard. Clipboard format round trips and auto-paste still require interactive validation.

The checked-in emoji JSON contains 3,781 fully-qualified Unicode 16 sequences. `Update-Emoji.ps1` regenerates it from Unicode's `emoji-test.txt`; shortcodes are normalized descriptive names and tags include names, groups, and subgroups. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
