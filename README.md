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
| Ctrl+Backspace | Delete the previous word or the selected text |
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

# Package and sign (automatically reuses your existing signing certificate)
.\tools\Package.ps1 -Sign

# Or select the signing identity using its public .cer file
.\tools\Package.ps1 -Sign -CertificateFile .\my-signing-certificate.cer

# Explicit thumbprints are still supported
.\tools\Package.ps1 -Sign -CertificateThumbprint YOUR_CERTIFICATE_THUMBPRINT
```

Artifacts are `out\ReCall.msix` and, when signing, `out\ReCall.Development.cer`. The certificate subject must match `Publisher` in `packaging\AppxManifest.xml` (default: `CN=ReCall Development`). With `-Sign`, certificate selection is automatic:

1. Use an explicitly supplied `-CertificateFile` or `-CertificateThumbprint`, if provided.
2. Otherwise read the thumbprint from the existing `out\ReCall.Development.cer` and find its matching private-key certificate in `CurrentUser\My`.
3. If that file does not exist, discover a valid code-signing certificate matching the manifest publisher in `CurrentUser\My` (newest issuance first).
4. Only if neither a certificate file nor an explicit thumbprint pins the identity, and no usable certificate exists, generate a development certificate for the default development publisher.

Rebuilding with `-Sign` reuses the same signing identity, so you do not need to trust a new certificate after each build. A `.cer` contains the **public certificate only**: the corresponding private key must still be present on the signing computer. If the pinned certificate is missing, expired, unsuitable for code signing, or lacks its private key, packaging fails with an explanation rather than silently replacing the signing identity. To deliberately switch certificates, pass an explicit file or thumbprint. The script does not install the certificate or the app.

For local installation, trust the exported development certificate in **Local Machine → Trusted People** (administrator action), then install the signed package:

```powershell
# Elevated PowerShell, for a certificate you generated and trust
Import-Certificate -FilePath .\out\ReCall.Development.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople
Add-AppxPackage .\out\ReCall.msix
```

The MSIX targets x64 Windows 11 22H2+ and declares `runFullTrust`. Development artwork is generated during packaging. Startup-on-login is not enabled automatically; start ReCall from its Start menu entry for each session.

## GitHub Actions builds and packages

The workflow in `.github/workflows/build.yml` runs on **Windows Server 2022 with Visual Studio 2022**, matching the `windows-x64` CMake preset. It runs on pushes and pull requests targeting `main`, and can also be started from **Actions → Windows build and MSIX → Run workflow**.

Every successful build configures CMake, builds Release x64, runs the C++ and certificate-selection tests, validates/packages the MSIX with the Windows SDK, and uploads **`ReCall-windows-x64-unsigned`**. This artifact contains `ReCall.exe`, an unsigned `ReCall.msix`, the README, and third-party notices. GUI smoke tests are not run on hosted CI runners.

Download artifacts from the workflow run's **Artifacts** section (GitHub provides them as ZIP files). They are retained for 14 days. The workflow does not publish GitHub Releases. If you already added GitHub's Ubuntu CMake starter workflow, replace it with this workflow; ReCall needs Windows tools.

### Enable signed MSIX artifacts

GitHub-hosted runners start without your signing key. To reuse the certificate already trusted by your school computer, export that same certificate **with its private key** to a password-protected PFX on your development computer:

```powershell
. .\tools\Signing.ps1
$thumbprint = Get-ReCallCertificateFileThumbprint -Path .\out\ReCall.Development.cer
$password = Read-Host 'Choose a PFX export password' -AsSecureString
Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$thumbprint" `
  -FilePath .\out\ReCall.Signing.pfx -Password $password
$pfxPath = (Resolve-Path .\out\ReCall.Signing.pfx).Path
[IO.File]::WriteAllText("$pfxPath.base64", [Convert]::ToBase64String([IO.File]::ReadAllBytes($pfxPath)))
```

In your GitHub repository, open **Settings → Secrets and variables → Actions → New repository secret** and add:

| Secret | Value |
| --- | --- |
| `RECALL_SIGNING_PFX_BASE64` | The entire contents of `out\ReCall.Signing.pfx.base64` |
| `RECALL_SIGNING_PFX_PASSWORD` | The password you entered when exporting the PFX |

The PFX and its base64 representation contain your private signing key; keep them out of commits and public artifacts. Both file types are gitignored. A public `.cer` alone cannot sign on GitHub. Export requires an exportable private key; if Windows refuses the export, that key cannot be transferred using this workflow.

On successful **`main` pushes or manual runs on `main`**, a separate job downloads the tested executable, imports the PFX on its ephemeral runner, repackages/signs using that exact certificate, and uploads **`ReCall-windows-x64-signed`**. This artifact includes the signed MSIX and matching public `.cer`; users who already trust the certificate do not need to import a new one for each build. The temporary PFX and newly imported certificate entries are removed afterward, and the hosted runner is discarded.

Without either secret, signing is skipped and the unsigned artifact is still available. If only one secret is configured, or the certificate is invalid, signing fails with an error. Pull requests never run the signing job. As with local packaging, the certificate subject must match the manifest publisher. The MSIX version comes from `packaging/AppxManifest.xml`; increase its four-part `Version` before distributing an update to an installed package.

To enable the workflow, commit these changes and push them to GitHub. No extra build secrets are needed for unsigned builds.

## Checks and data regeneration

```powershell
ctest --preset release
.\tests\signing_tests.ps1
.\tools\Smoke-Test.ps1
.\tools\Update-Emoji.ps1
```

`core_tests` covers fuzzy alignment/ranking, prefix routing, JSON parsing, Unicode escapes, and word-deletion boundaries. `storage_tests` exercises real SQLite persistence, WAL, ordering, Unicode text, character counts, retention, and image-file cleanup in an isolated temporary directory. `signing_tests.ps1` checks certificate discovery, CER-based identity reuse, and invalid-certificate rejection using temporary in-memory certificates without modifying the certificate store (PowerShell 7). The smoke script launches the actual GUI, verifies hidden startup, opens the palette, submits an emoji query, navigates, dismisses with Escape, and exits. It needs an interactive desktop and no already-running instance; it does not execute a result or alter the clipboard. Clipboard format round trips and auto-paste still require interactive validation.

The checked-in emoji JSON contains 3,781 fully-qualified Unicode 16 sequences. `Update-Emoji.ps1` regenerates it from Unicode's `emoji-test.txt`; shortcodes are normalized descriptive names and tags include names, groups, and subgroups. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
