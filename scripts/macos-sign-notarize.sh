#!/usr/bin/env bash
# Code-signs, notarizes and staples the macOS plug-in bundles in a directory.
#
# Usage: scripts/macos-sign-notarize.sh <bundle-dir> <status-file>
#
# Every step is optional and driven by environment variables (GitHub secrets).
# Without them the bundles are left untouched and the status is "unsigned".
#
# Signing (all three required):
#   MACOS_CERTIFICATE_P12_BASE64   base64 of a .p12 with the "Developer ID
#                                  Application" certificate and private key
#   MACOS_CERTIFICATE_PASSWORD     password of that .p12
#   MACOS_SIGNING_IDENTITY         e.g. "Developer ID Application: Name (TEAMID)"
#
# Notarization (after signing), either an App Store Connect API key:
#   MACOS_NOTARY_KEY_P8_BASE64     base64 of the AuthKey_XXXXXXXXXX.p8 file
#   MACOS_NOTARY_KEY_ID            key ID, e.g. XXXXXXXXXX
#   MACOS_NOTARY_ISSUER_ID         issuer UUID
# or an Apple ID with an app-specific password:
#   MACOS_NOTARY_APPLE_ID, MACOS_NOTARY_TEAM_ID, MACOS_NOTARY_PASSWORD
#
# Writes "unsigned", "signed" or "notarized" to <status-file>.
set -euo pipefail
set +x # never trace: secrets pass through this script

DIR="$(cd "${1:?usage: macos-sign-notarize.sh <bundle-dir> <status-file>}" && pwd)"
STATUS_FILE="${2:?usage: macos-sign-notarize.sh <bundle-dir> <status-file>}"

log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

if [[ -z "${MACOS_CERTIFICATE_P12_BASE64:-}" || -z "${MACOS_CERTIFICATE_PASSWORD:-}" || -z "${MACOS_SIGNING_IDENTITY:-}" ]]; then
  log "No signing certificate configured: leaving the bundles unsigned"
  echo unsigned > "$STATUS_FILE"
  exit 0
fi

WORK="$(mktemp -d)"
KEYCHAIN="$WORK/ironclip-signing.keychain-db"
ORIGINAL_KEYCHAINS="$(security list-keychains -d user | tr -d '"')"

cleanup() {
  # shellcheck disable=SC2086
  security list-keychains -d user -s $ORIGINAL_KEYCHAINS >/dev/null 2>&1 || true
  security delete-keychain "$KEYCHAIN" >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT

log "Importing the signing certificate into a temporary keychain"
KEYCHAIN_PASSWORD="$(openssl rand -hex 24)"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 3600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
(
  umask 077
  printf '%s' "$MACOS_CERTIFICATE_P12_BASE64" | base64 --decode > "$WORK/certificate.p12"
)
security import "$WORK/certificate.p12" -k "$KEYCHAIN" -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign >/dev/null
rm -f "$WORK/certificate.p12"
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
# shellcheck disable=SC2086
security list-keychains -d user -s "$KEYCHAIN" $ORIGINAL_KEYCHAINS

bundles=()
for b in "$DIR"/*.vst3 "$DIR"/*.clap "$DIR"/*.component; do
  [[ -e "$b" ]] && bundles+=("$b")
done

for b in "${bundles[@]}"; do
  log "Signing $(basename "$b")"
  codesign --force --timestamp --options runtime --keychain "$KEYCHAIN" --sign "$MACOS_SIGNING_IDENTITY" "$b"
  codesign --verify --strict --verbose=2 "$b"
done

notary_args=()
if [[ -n "${MACOS_NOTARY_KEY_P8_BASE64:-}" && -n "${MACOS_NOTARY_KEY_ID:-}" && -n "${MACOS_NOTARY_ISSUER_ID:-}" ]]; then
  (
    umask 077
    printf '%s' "$MACOS_NOTARY_KEY_P8_BASE64" | base64 --decode > "$WORK/notary-key.p8"
  )
  notary_args=(--key "$WORK/notary-key.p8" --key-id "$MACOS_NOTARY_KEY_ID" --issuer "$MACOS_NOTARY_ISSUER_ID")
elif [[ -n "${MACOS_NOTARY_APPLE_ID:-}" && -n "${MACOS_NOTARY_TEAM_ID:-}" && -n "${MACOS_NOTARY_PASSWORD:-}" ]]; then
  notary_args=(--apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" --password "$MACOS_NOTARY_PASSWORD")
fi

if [[ ${#notary_args[@]} -eq 0 ]]; then
  log "No notarization credentials configured: bundles are signed but not notarized"
  echo signed > "$STATUS_FILE"
  exit 0
fi

log "Submitting for notarization"
mkdir -p "$WORK/submit"
for b in "${bundles[@]}"; do
  ditto "$b" "$WORK/submit/$(basename "$b")"
done
ditto -c -k --sequesterRsrc --keepParent "$WORK/submit" "$WORK/notarize.zip"
xcrun notarytool submit "$WORK/notarize.zip" "${notary_args[@]}" --wait --timeout 45m --output-format json > "$WORK/result.json"
status="$(plutil -extract status raw -o - "$WORK/result.json" 2>/dev/null || echo unknown)"
submission="$(plutil -extract id raw -o - "$WORK/result.json" 2>/dev/null || echo unknown)"
echo "Notarization submission $submission: $status"
if [[ "$status" != "Accepted" ]]; then
  xcrun notarytool log "$submission" "${notary_args[@]}" || true
  echo "Notarization failed" >&2
  exit 1
fi

for b in "${bundles[@]}"; do
  log "Stapling $(basename "$b")"
  xcrun stapler staple "$b"
  xcrun stapler validate "$b"
done

echo notarized > "$STATUS_FILE"
