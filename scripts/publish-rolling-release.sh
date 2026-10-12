#!/usr/bin/env bash
# Publishes the rolling GitHub release "Latest IronClip Release" (tag: rolling).
#
# Usage: scripts/publish-rolling-release.sh <asset-dir> <notes-file> <asset>...
# Environment: GH_TOKEN, GITHUB_REPOSITORY, GITHUB_SHA (as set in Actions).
#
# The existing release stays intact until every new asset has been uploaded:
# assets go up under temporary names first, then the tag is moved, the notes
# are updated and the new assets replace the old ones by rename. Assets that
# are no longer produced are removed, so the release never accumulates files.
set -euo pipefail

TAG="rolling"
TITLE="Latest IronClip Release"
DIR="$(cd "${1:?usage: publish-rolling-release.sh <asset-dir> <notes-file> <asset>...}" && pwd)"
NOTES="${2:?missing notes file}"
shift 2
ASSETS=("$@")
REPO="${GITHUB_REPOSITORY:?}"
SHA="${GITHUB_SHA:?}"
SUFFIX=".uploading"

[[ ${#ASSETS[@]} -gt 0 ]] || { echo "No assets given" >&2; exit 1; }
for a in "${ASSETS[@]}"; do
  [[ -f "$DIR/$a" ]] || { echo "Missing asset: $a" >&2; exit 1; }
done

log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

asset_ids() { # prints "<id>\t<name>" for every asset of the release
  gh api "repos/$REPO/releases/tags/$TAG" --jq '.assets[] | [.id, .name] | @tsv'
}

# --- Make sure the tag and the release exist -----------------------------------
if ! gh api "repos/$REPO/git/ref/tags/$TAG" >/dev/null 2>&1; then
  log "Creating tag $TAG at ${SHA:0:7}"
  gh api -X POST "repos/$REPO/git/refs" -f ref="refs/tags/$TAG" -f sha="$SHA" >/dev/null
fi

if ! gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  log "Creating release \"$TITLE\""
  gh release create "$TAG" --repo "$REPO" --verify-tag --title "$TITLE" --notes-file "$NOTES" --latest
fi

# --- Upload the new assets under temporary names --------------------------------
staging="$(mktemp -d)"
uploaded=0
cleanup() {
  rm -rf "$staging"
  if [[ $uploaded -eq 0 ]]; then
    # Remove partial uploads; the published assets were never touched.
    asset_ids 2>/dev/null | while IFS=$'\t' read -r id name; do
      [[ "$name" == *"$SUFFIX" ]] && gh api -X DELETE "repos/$REPO/releases/assets/$id" >/dev/null || true
    done
  fi
}
trap cleanup EXIT

# Leftovers from an earlier interrupted run
asset_ids | while IFS=$'\t' read -r id name; do
  [[ "$name" == *"$SUFFIX" ]] && gh api -X DELETE "repos/$REPO/releases/assets/$id" >/dev/null || true
done

for a in "${ASSETS[@]}"; do
  cp "$DIR/$a" "$staging/$a$SUFFIX"
  log "Uploading $a"
  gh release upload "$TAG" --repo "$REPO" "$staging/$a$SUFFIX"
done

# --- Point the release at the new build ------------------------------------------
log "Moving tag $TAG to ${SHA:0:7}"
gh api -X PATCH "repos/$REPO/git/refs/tags/$TAG" -f sha="$SHA" -F force=true >/dev/null

log "Updating release notes"
gh release edit "$TAG" --repo "$REPO" --title "$TITLE" --notes-file "$NOTES" --draft=false --prerelease=false --latest >/dev/null

log "Replacing assets"
declare -A wanted=()
for a in "${ASSETS[@]}"; do wanted["$a"]=1; done
while IFS=$'\t' read -r id name; do
  if [[ "$name" != *"$SUFFIX" ]]; then
    # Old version of a current asset, or an asset that is no longer produced.
    gh api -X DELETE "repos/$REPO/releases/assets/$id" >/dev/null
  fi
done < <(asset_ids)
while IFS=$'\t' read -r id name; do
  if [[ "$name" == *"$SUFFIX" && -n "${wanted[${name%"$SUFFIX"}]:-}" ]]; then
    gh api -X PATCH "repos/$REPO/releases/assets/$id" -f name="${name%"$SUFFIX"}" >/dev/null
  fi
done < <(asset_ids)
uploaded=1

log "Published: https://github.com/$REPO/releases/tag/$TAG"
asset_ids | cut -f2
