#!/usr/bin/env bash
# Builds the distributable ZIP for the current platform.
#
# Usage: scripts/package.sh <plugin-dir> <dist-dir>
#   macOS:   <dist-dir>/IronClip-macOS-universal.zip
#   Windows: <dist-dir>/IronClip-Windows-x64.zip
#
# On macOS the bundles are stripped and, when the signing secrets are present
# in the environment, signed and notarized (see scripts/macos-sign-notarize.sh).
# The signing status is written to <dist-dir>/macos-signing-status.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGINS="$(cd "${1:?usage: package.sh <plugin-dir> <dist-dir>}" && pwd)"
DIST="${2:?usage: package.sh <plugin-dir> <dist-dir>}"
mkdir -p "$DIST"
DIST="$(cd "$DIST" && pwd)"

case "$(uname -s)" in
  Darwin) platform=mac ;;
  MINGW* | MSYS* | CYGWIN*) platform=win ;;
  *) echo "Unsupported host" >&2; exit 1 ;;
esac

STAGE="$DIST/stage/IronClip"
rm -rf "$DIST/stage"
mkdir -p "$STAGE"

copy_docs() {
  cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
  cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.md"
  cp -R "$ROOT/licenses" "$STAGE/licenses"
}

if [[ $platform == mac ]]; then
  for fmt in vst3 clap component; do
    ditto "$PLUGINS/IronClip.$fmt" "$STAGE/IronClip.$fmt"
    strip -x "$STAGE/IronClip.$fmt/Contents/MacOS/IronClip"
  done
  cp "$ROOT/resources/install/INSTALL-macOS.txt" "$STAGE/INSTALL.txt"
  copy_docs

  "$ROOT/scripts/macos-sign-notarize.sh" "$STAGE" "$DIST/macos-signing-status.txt"

  # Signatures live inside the bundles; extended attributes (quarantine,
  # provenance) must not end up in the archive as AppleDouble files.
  xattr -cr "$STAGE"
  zip="$DIST/IronClip-macOS-universal.zip"
  rm -f "$zip"
  ditto -c -k --norsrc --noextattr --keepParent "$STAGE" "$zip"
else
  cp -R "$PLUGINS/IronClip.vst3" "$STAGE/IronClip.vst3"
  cp "$PLUGINS/IronClip.clap" "$STAGE/IronClip.clap"
  cp "$ROOT/resources/install/INSTALL-Windows.txt" "$STAGE/INSTALL.txt"
  copy_docs

  zip="$DIST/IronClip-Windows-x64.zip"
  rm -f "$zip"
  powershell.exe -NoProfile -NonInteractive -Command \
    "Compress-Archive -Path '$(cygpath -w "$STAGE")' -DestinationPath '$(cygpath -w "$zip")' -CompressionLevel Optimal"
fi

rm -rf "$DIST/stage"
echo "Created $zip ($(du -h "$zip" | cut -f1))"
