# Releasing IronClip

IronClip publishes a single rolling GitHub release named
**Latest IronClip Release** on the tag `rolling`. It always represents the most
recent revision of `main` that was built, tested and validated on both
platforms.

## Publishing a release

Every push to `main` publishes automatically. To re-publish without a new
commit, open **Actions → Release → Run workflow** and run it on `main`.

The workflow:

1. builds macOS (universal) and Windows (x64), runs the DSP test suite and the
   VST3 validator, clap-validator, pluginval and (macOS) `auval`;
2. signs and notarizes the macOS build if the secrets below are configured;
3. only if every job succeeded, uploads the new ZIPs and `SHA256SUMS.txt`
   under temporary names, moves the `rolling` tag to the built commit,
   updates the release notes and swaps the new assets in.

A failed build or upload leaves the existing release untouched. The release is
a normal release (not a pre-release) and is marked as latest. Asset names are
stable, so these links always point to the newest build:

- `https://github.com/BTFLV/IronClip/releases/download/rolling/IronClip-macOS-universal.zip`
- `https://github.com/BTFLV/IronClip/releases/download/rolling/IronClip-Windows-x64.zip`

While the repository is private, these downloads require a signed-in account
with access to the repository; they become public when the repository does.

Moving the tag uses the workflow's `GITHUB_TOKEN`, which does not trigger
other workflows, and no workflow runs on tag pushes, so publishing cannot
start a loop.

## Version numbers

The plug-in version comes from `project(IronClip VERSION x.y.z)` in
`CMakeLists.txt` and is embedded in all formats (Info.plist, AU version
integer `0xMMMMmmbb`, VST3 and CLAP descriptors). The rolling release does
not change it; bump it when the plug-in changes in a way users should see.
The short commit hash is shown next to the version in the editor footer.

## macOS signing and notarization

Signing is optional. Without the secrets, CI produces unsigned builds and the
release notes say so. To sign and notarize, add these **repository secrets**
(Settings → Secrets and variables → Actions):

| Secret | Format |
| --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | Base64 of a `.p12` export of your **Developer ID Application** certificate including its private key: `base64 -i certificate.p12 \| pbcopy` |
| `MACOS_CERTIFICATE_PASSWORD` | The password you set when exporting the `.p12` |
| `MACOS_SIGNING_IDENTITY` | The certificate's common name, e.g. `Developer ID Application: Jane Doe (AB12CD34EF)` (see `security find-identity -v -p codesigning`) |

With only these three, builds are signed but not notarized. For notarization,
add **either** an App Store Connect API key (recommended):

| Secret | Format |
| --- | --- |
| `MACOS_NOTARY_KEY_P8_BASE64` | Base64 of the `AuthKey_XXXXXXXXXX.p8` file: `base64 -i AuthKey_XXXXXXXXXX.p8 \| pbcopy` |
| `MACOS_NOTARY_KEY_ID` | The key ID (10 characters) |
| `MACOS_NOTARY_ISSUER_ID` | The issuer ID (UUID) shown on the App Store Connect *Integrations* page |

**or** an Apple ID with an app-specific password:

| Secret | Format |
| --- | --- |
| `MACOS_NOTARY_APPLE_ID` | Apple ID e-mail address |
| `MACOS_NOTARY_TEAM_ID` | 10-character Team ID |
| `MACOS_NOTARY_PASSWORD` | App-specific password from appleid.apple.com |

The secrets are passed only to the packaging step of release builds. The
script ([`scripts/macos-sign-notarize.sh`](../scripts/macos-sign-notarize.sh))
imports the certificate into a temporary keychain that is deleted afterwards,
signs each bundle with the hardened runtime and a secure timestamp, submits
them with `notarytool`, and staples the tickets. Nothing secret is printed or
uploaded.

Windows code signing is not set up.
