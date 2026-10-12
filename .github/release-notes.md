IronClip — a simple, powerful, no-nonsense hard clipper.

This rolling release always holds the latest validated build of the `main` branch.

| | |
| --- | --- |
| Version | @VERSION@ |
| Commit | [`@COMMIT_SHORT@`](https://github.com/@REPOSITORY@/commit/@COMMIT@) |
| Built | @DATE@ |

## Downloads

| Platform | Formats | Download |
| --- | --- | --- |
| macOS 11+ (Apple Silicon and Intel) | VST3, CLAP, AU | [IronClip-macOS-universal.zip](https://github.com/@REPOSITORY@/releases/download/rolling/IronClip-macOS-universal.zip) |
| Windows 10/11 (x64) | VST3, CLAP | [IronClip-Windows-x64.zip](https://github.com/@REPOSITORY@/releases/download/rolling/IronClip-Windows-x64.zip) |

SHA-256 checksums are listed in `SHA256SUMS.txt`.

**macOS signing:** @MACOS_SIGNING@

**Windows signing:** the Windows plug-ins are not code-signed.

## Installation

Unzip the download and copy the plug-ins into your plug-in folders, then rescan plug-ins in your host. Each ZIP contains an `INSTALL.txt` with the details.

- **macOS:** `IronClip.vst3` → `~/Library/Audio/Plug-Ins/VST3/`, `IronClip.clap` → `~/Library/Audio/Plug-Ins/CLAP/`, `IronClip.component` → `~/Library/Audio/Plug-Ins/Components/`
- **Windows:** `IronClip.vst3` (the whole folder) → `C:\Program Files\Common Files\VST3\`, `IronClip.clap` → `C:\Program Files\Common Files\CLAP\`

Every build in this release passed the DSP test suite and the Steinberg VST3 validator, clap-validator and pluginval (plus `auval` for the Audio Unit) in CI.

See the [README](https://github.com/@REPOSITORY@#readme) for the full documentation.
