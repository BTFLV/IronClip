#!/usr/bin/env bash
# Runs the plug-in validators against a build. Exits non-zero on any failure.
#
# Usage: scripts/validate.sh <plugin-dir> [tools-dir]
#   <plugin-dir> holds IronClip.vst3, IronClip.clap (and IronClip.component).
#   Fetch the tools first with scripts/fetch-validators.sh.
#
# On macOS the AU check (auval and pluginval) needs the component in
# ~/Library/Audio/Plug-Ins/Components; it is installed there only when
# IRONCLIP_INSTALL_AU=1 (set in CI), otherwise the AU checks are skipped.
# pluginval's editor tests need a desktop session; enable them with
# IRONCLIP_PLUGINVAL_GUI=1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLUGINS="$(cd "${1:?usage: validate.sh <plugin-dir> [tools-dir]}" && pwd)"
TOOLS="$(cd "${2:-$ROOT/build/tools}" && pwd)"

case "$(uname -s)" in
  Darwin) platform=mac ;;
  MINGW* | MSYS* | CYGWIN*) platform=win ;;
  *) echo "Unsupported host" >&2; exit 1 ;;
esac

pluginval_args=(--strictness-level 10 --validate-in-process)
[[ "${IRONCLIP_PLUGINVAL_GUI:-0}" == 1 ]] || pluginval_args+=(--skip-gui-tests)

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

LOGS="$ROOT/build/validation-logs"
mkdir -p "$LOGS"

# run <name> <summary-lines> <command...>: keeps the full output in a log file,
# prints a short summary, and dumps the whole log when the command fails.
run() {
  local name="$1" lines="$2"
  shift 2
  log "$name"
  local logfile
  logfile="$LOGS/$(echo "$name" | tr -c '[:alnum:]' '-').log"
  if "$@" >"$logfile" 2>&1; then
    tail -n "$lines" "$logfile"
  else
    cat "$logfile"
    echo "FAILED: $name (log: $logfile)" >&2
    exit 1
  fi
}

if [[ $platform == mac ]]; then
  VST3_VALIDATOR="$TOOLS/vst3-validator/validator"
  CLAP_VALIDATOR="$TOOLS/clap-validator/clap-validator"
  PLUGINVAL="$TOOLS/pluginval/pluginval.app/Contents/MacOS/pluginval"
else
  VST3_VALIDATOR="$TOOLS/vst3-validator/validator.exe"
  CLAP_VALIDATOR="$TOOLS/clap-validator/clap-validator.exe"
  PLUGINVAL="$TOOLS/pluginval/pluginval.exe"
fi

run "Steinberg VST3 validator (extensive)" 2 "$VST3_VALIDATOR" -e "$PLUGINS/IronClip.vst3"
run "clap-validator" 1 "$CLAP_VALIDATOR" validate "$PLUGINS/IronClip.clap"
run "pluginval VST3 (strictness 10)" 1 "$PLUGINVAL" "${pluginval_args[@]}" --validate "$PLUGINS/IronClip.vst3"

if [[ $platform == mac ]]; then
  if [[ "${IRONCLIP_INSTALL_AU:-0}" == 1 ]]; then
    components="$HOME/Library/Audio/Plug-Ins/Components"
    mkdir -p "$components"
    rm -rf "$components/IronClip.component"
    cp -R "$PLUGINS/IronClip.component" "$components/"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    sleep 2

    run "auval" 3 auval -v aufx IrCl Btfl
    run "pluginval AU (strictness 10)" 1 "$PLUGINVAL" "${pluginval_args[@]}" --validate "$components/IronClip.component"
  else
    log "Skipping AU checks (set IRONCLIP_INSTALL_AU=1 to install and validate the component)"
  fi
fi

log "All validators passed"
