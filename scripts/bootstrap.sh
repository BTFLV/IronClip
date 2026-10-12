#!/usr/bin/env bash
# Fetches the pinned build dependencies of IronClip into external/iPlug2:
# the iPlug2 submodule, the VST3 and CLAP SDKs and iPlug2's prebuilt Skia.
#
# Usage: scripts/bootstrap.sh [mac|win]     (defaults to the host platform)
# Works on macOS and in Git Bash on Windows. Safe to run repeatedly.
set -euo pipefail

VST3_SDK_TAG="v3.8.1_build_84"
CLAP_SDK_TAG="1.2.10"
CLAP_HELPERS_REV="29389e43489b1af44b654ef5a7b4ee0af4bbc13b"
IPLUG2_DEPS_URL="https://github.com/iPlug2/iPlug2/releases/download/v1.0.0-beta"
IPLUG2_DEPS_MAC_SHA256="9bcff0d33ac6e9aea9e1b69a6834e04b277d084ffb616e923cbbd11f7a5f67f9"
IPLUG2_DEPS_WIN_SHA256="90c91ac8a487f10c848a580aab1895def42f7ad4193eaa491a4896f577d25e31"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IPLUG2="$ROOT/external/iPlug2"
SDKS="$IPLUG2/Dependencies/IPlug"
BUILD_DEPS="$IPLUG2/Dependencies/Build"

platform="${1:-}"
if [[ -z "$platform" ]]; then
  case "$(uname -s)" in
    Darwin) platform=mac ;;
    MINGW* | MSYS* | CYGWIN*) platform=win ;;
    *) echo "Unsupported host; pass 'mac' or 'win'." >&2; exit 1 ;;
  esac
fi

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

# --- iPlug2 -------------------------------------------------------------------
if [[ ! -f "$IPLUG2/iPlug2.cmake" ]]; then
  log "Fetching the iPlug2 submodule"
  git -C "$ROOT" submodule update --init --depth 1 external/iPlug2
fi

# --- VST3 SDK -----------------------------------------------------------------
if [[ "$(cat "$SDKS/VST3_SDK/.ironclip-version" 2>/dev/null)" != "$VST3_SDK_TAG" ]]; then
  log "Fetching VST3 SDK $VST3_SDK_TAG"
  rm -rf "$SDKS/VST3_SDK"
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$VST3_SDK_TAG" https://github.com/steinbergmedia/vst3sdk.git "$SDKS/VST3_SDK"
  git -C "$SDKS/VST3_SDK" submodule update --quiet --init --depth 1 base pluginterfaces public.sdk cmake
  rm -rf "$SDKS/VST3_SDK/public.sdk/samples" "$SDKS/VST3_SDK"/.git* "$SDKS/VST3_SDK"/*/.git*
  git -C "$IPLUG2" checkout -- Dependencies/IPlug/VST3_SDK/README.md 2>/dev/null || true
  echo "$VST3_SDK_TAG" > "$SDKS/VST3_SDK/.ironclip-version"
fi

# --- CLAP SDK and helpers -------------------------------------------------------
if [[ "$(cat "$SDKS/CLAP_SDK/.ironclip-version" 2>/dev/null)" != "$CLAP_SDK_TAG" ]]; then
  log "Fetching CLAP SDK $CLAP_SDK_TAG"
  rm -rf "$SDKS/CLAP_SDK"
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$CLAP_SDK_TAG" https://github.com/free-audio/clap.git "$SDKS/CLAP_SDK"
  rm -rf "$SDKS/CLAP_SDK/.git"
  git -C "$IPLUG2" checkout -- Dependencies/IPlug/CLAP_SDK/readme.txt 2>/dev/null || true
  echo "$CLAP_SDK_TAG" > "$SDKS/CLAP_SDK/.ironclip-version"
fi

if [[ "$(cat "$SDKS/CLAP_HELPERS/.ironclip-version" 2>/dev/null)" != "$CLAP_HELPERS_REV" ]]; then
  log "Fetching CLAP helpers ${CLAP_HELPERS_REV:0:12}"
  rm -rf "$SDKS/CLAP_HELPERS"
  git init --quiet "$SDKS/CLAP_HELPERS"
  git -C "$SDKS/CLAP_HELPERS" fetch --quiet --depth 1 https://github.com/free-audio/clap-helpers.git "$CLAP_HELPERS_REV"
  git -C "$SDKS/CLAP_HELPERS" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
  rm -rf "$SDKS/CLAP_HELPERS/.git"
  git -C "$IPLUG2" checkout -- Dependencies/IPlug/CLAP_HELPERS/readme.txt 2>/dev/null || true
  echo "$CLAP_HELPERS_REV" > "$SDKS/CLAP_HELPERS/.ironclip-version"
fi

# --- Prebuilt Skia (iPlug2 release archive) -------------------------------------
if [[ "$platform" == mac ]]; then
  archive=IPLUG2_DEPS_MAC; expected="$IPLUG2_DEPS_MAC_SHA256"
else
  archive=IPLUG2_DEPS_WIN; expected="$IPLUG2_DEPS_WIN_SHA256"
fi

if [[ "$(cat "$BUILD_DEPS/.ironclip-$platform" 2>/dev/null)" != "$expected" ]]; then
  log "Fetching prebuilt Skia ($archive)"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl --fail --silent --show-error --location --retry 3 -o "$tmp/$archive.zip" "$IPLUG2_DEPS_URL/$archive.zip"
  actual="$(sha256 "$tmp/$archive.zip")"
  if [[ "$actual" != "$expected" ]]; then
    echo "Checksum mismatch for $archive.zip: expected $expected, got $actual" >&2
    exit 1
  fi
  if command -v unzip >/dev/null 2>&1; then
    unzip -q -o "$tmp/$archive.zip" -d "$tmp"
  else
    # Git Bash without unzip: let PowerShell extract the archive.
    powershell.exe -NoProfile -NonInteractive -Command \
      "Expand-Archive -Force -Path '$(cygpath -w "$tmp/$archive.zip")' -DestinationPath '$(cygpath -w "$tmp")'"
  fi
  mkdir -p "$BUILD_DEPS"
  rm -rf "${BUILD_DEPS:?}/$platform" "${BUILD_DEPS:?}/src"
  mv "$tmp/$archive"/* "$BUILD_DEPS/"
  echo "$expected" > "$BUILD_DEPS/.ironclip-$platform"
fi

log "Dependencies are ready"
