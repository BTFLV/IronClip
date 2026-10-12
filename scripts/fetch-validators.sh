#!/usr/bin/env bash
# Downloads clap-validator and pluginval and builds Steinberg's VST3 validator
# into a tools directory (default: build/tools).
#
# Usage: scripts/fetch-validators.sh [tools-dir]
# Works on macOS and in Git Bash on Windows. Safe to run repeatedly.
set -euo pipefail

CLAP_VALIDATOR_URL="https://github.com/free-audio/clap-validator/releases/download/0.4.1"
CLAP_VALIDATOR_MAC="clap-validator-0.4.1-127-g152b982-macos-universal.zip"
CLAP_VALIDATOR_MAC_SHA256="bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1"
CLAP_VALIDATOR_WIN="clap-validator-0.4.1-127-g152b982-windows.zip"
CLAP_VALIDATOR_WIN_SHA256="d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10"
PLUGINVAL_URL="https://github.com/Tracktion/pluginval/releases/download/v1.0.4"
PLUGINVAL_MAC_SHA256="3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f"
PLUGINVAL_WIN_SHA256="c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab"
VST3_SDK_TAG="v3.8.1_build_84"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="${1:-$ROOT/build/tools}"
mkdir -p "$TOOLS"
TOOLS="$(cd "$TOOLS" && pwd)"

case "$(uname -s)" in
  Darwin) platform=mac ;;
  MINGW* | MSYS* | CYGWIN*) platform=win ;;
  *) echo "Unsupported host" >&2; exit 1 ;;
esac

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

download() { # url file sha256
  curl --fail --silent --show-error --location --retry 3 -o "$2" "$1"
  local actual
  actual="$(sha256 "$2")"
  if [[ "$actual" != "$3" ]]; then
    echo "Checksum mismatch for $1: expected $3, got $actual" >&2
    exit 1
  fi
}

extract() { # zip-archive dest
  mkdir -p "$2"
  if command -v unzip >/dev/null 2>&1; then
    unzip -q -o "$1" -d "$2"
  else
    powershell.exe -NoProfile -NonInteractive -Command \
      "Expand-Archive -Force -Path '$(cygpath -w "$1")' -DestinationPath '$(cygpath -w "$2")'"
  fi
}

log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

# --- clap-validator -----------------------------------------------------------------
if [[ ! -e "$TOOLS/clap-validator/.done" ]]; then
  log "Fetching clap-validator"
  rm -rf "$TOOLS/clap-validator"
  mkdir -p "$TOOLS/clap-validator"
  if [[ $platform == mac ]]; then
    download "$CLAP_VALIDATOR_URL/$CLAP_VALIDATOR_MAC" "$TOOLS/clap-validator/dl.zip" "$CLAP_VALIDATOR_MAC_SHA256"
    extract "$TOOLS/clap-validator/dl.zip" "$TOOLS/clap-validator"
    tar -xzf "$TOOLS"/clap-validator/*.tar.gz -C "$TOOLS/clap-validator"
    xattr -dr com.apple.quarantine "$TOOLS/clap-validator" 2>/dev/null || true
  else
    download "$CLAP_VALIDATOR_URL/$CLAP_VALIDATOR_WIN" "$TOOLS/clap-validator/dl.zip" "$CLAP_VALIDATOR_WIN_SHA256"
    extract "$TOOLS/clap-validator/dl.zip" "$TOOLS/clap-validator"
  fi
  # Archive layouts differ between platforms; move the binary to a fixed place.
  bin="$(find "$TOOLS/clap-validator" -type f \( -name clap-validator -o -name clap-validator.exe \) | head -n 1)"
  [[ -n "$bin" ]] || { echo "clap-validator binary not found in archive" >&2; exit 1; }
  [[ "$bin" == "$TOOLS/clap-validator/$(basename "$bin")" ]] || mv "$bin" "$TOOLS/clap-validator/"
  chmod +x "$TOOLS/clap-validator/$(basename "$bin")"
  touch "$TOOLS/clap-validator/.done"
fi

# --- pluginval ------------------------------------------------------------------------
if [[ ! -e "$TOOLS/pluginval/.done" ]]; then
  log "Fetching pluginval"
  rm -rf "$TOOLS/pluginval"
  mkdir -p "$TOOLS/pluginval"
  if [[ $platform == mac ]]; then
    download "$PLUGINVAL_URL/pluginval_macOS.zip" "$TOOLS/pluginval/dl.zip" "$PLUGINVAL_MAC_SHA256"
  else
    download "$PLUGINVAL_URL/pluginval_Windows.zip" "$TOOLS/pluginval/dl.zip" "$PLUGINVAL_WIN_SHA256"
  fi
  extract "$TOOLS/pluginval/dl.zip" "$TOOLS/pluginval"
  xattr -dr com.apple.quarantine "$TOOLS/pluginval" 2>/dev/null || true
  touch "$TOOLS/pluginval/.done"
fi

# --- Steinberg VST3 validator (built from the SDK) ------------------------------------
if [[ ! -e "$TOOLS/vst3-validator/.done" ]]; then
  log "Building the VST3 validator ($VST3_SDK_TAG)"
  rm -rf "$TOOLS/vst3-validator"
  src="$TOOLS/vst3-validator/sdk"
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$VST3_SDK_TAG" https://github.com/steinbergmedia/vst3sdk.git "$src"
  git -C "$src" submodule update --quiet --init --depth 1 base pluginterfaces public.sdk cmake
  args=(-DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_CREATE_PLUGIN_LINK=OFF -DSMTG_RUN_VST_VALIDATOR=OFF)
  if [[ $platform == mac ]]; then
    # The SDK's CMake insists on a full Xcode; the command line tools suffice.
    if ! xcodebuild -version >/dev/null 2>&1; then
      export XCODE_VERSION=16
      args+=(-DXCODE_VERSION=16)
    fi
    cmake -S "$src" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${args[@]}" >/dev/null
    cmake --build "$src/build" --target validator >/dev/null
    cp "$src/build/bin/Release/validator" "$TOOLS/vst3-validator/validator"
  else
    cmake -S "$src" -B "$src/build" -A x64 "${args[@]}" >/dev/null
    cmake --build "$src/build" --target validator --config Release >/dev/null
    cp "$src/build/bin/Release/validator.exe" "$TOOLS/vst3-validator/validator.exe"
  fi
  rm -rf "$src"
  touch "$TOOLS/vst3-validator/.done"
fi

log "Validators are in $TOOLS"
