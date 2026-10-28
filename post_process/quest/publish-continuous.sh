#!/usr/bin/env bash
set -euo pipefail
assets=$(realpath "${1:?Usage: publish-continuous.sh RELEASE_ASSETS}")
: "${GH_REPO:?}" "${GITHUB_SHA:?}" "${GITHUB_REF:?}" "${GITHUB_EVENT_NAME:?}"
branch=$(gh api "repos/$GH_REPO" --jq .default_branch)
if [[ "$GITHUB_REF" != "refs/heads/$branch" ]] || [[ "$GITHUB_EVENT_NAME" != push && "$GITHUB_EVENT_NAME" != workflow_dispatch ]]; then
  echo 'Continuous publication requires a trusted default-branch build.' >&2
  exit 1
fi
# An older, slower build must not replace a newer default-branch commit.
head=$(gh api "repos/$GH_REPO/git/ref/heads/$branch" --jq .object.sha)
if [ "$head" != "$GITHUB_SHA" ]; then echo 'Skipping superseded build.'; exit 0; fi
files=(stearlight-quest.apk stearlight-quest.apk.sha256 stearlight-quest-sources.zip BUILD.json SHA256SUMS README.md LICENSE.md)
for name in "${files[@]}"; do test -s "$assets/$name"; done
(cd "$assets" && sha256sum -c SHA256SUMS)
python3 - "$assets/BUILD.json" "$GITHUB_SHA" <<'PY'
import json,sys
assert json.load(open(sys.argv[1]))['commit'] == sys.argv[2], 'APK source commit mismatch'
PY
if gh api "repos/$GH_REPO/git/ref/tags/continuous" >/dev/null 2>&1; then
  gh api "repos/$GH_REPO/git/refs/tags/continuous" --method PATCH --raw-field sha="$GITHUB_SHA" --field force=true >/dev/null
else
  gh api "repos/$GH_REPO/git/refs" --method POST --raw-field ref=refs/tags/continuous --raw-field sha="$GITHUB_SHA" >/dev/null
fi
notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
printf '%s\n' 'Standalone Stearlight OS APK for the Quest platform, supporting Quest 2, Quest 3 and Quest 3S. Download stearlight-quest.apk and sideload it, then open Stearlight OS from the app library.' \
  '' 'Includes the Linux rootfs, SteamVR, KDE, GPU drivers and adapters. No PC launcher is required after installation.' \
  '' "[Build log](https://github.com/$GH_REPO/actions/runs/${GITHUB_RUN_ID:?}) | [Source](https://github.com/$GH_REPO/tree/$GITHUB_SHA)" > "$notes"
if gh release view continuous >/dev/null 2>&1; then
  gh release edit continuous --title 'Continuous build' --notes-file "$notes" --latest
else
  gh release create continuous --title 'Continuous build' --notes-file "$notes" --latest
fi
uploads=(); for name in "${files[@]}"; do uploads+=("$assets/$name"); done
gh release upload continuous "${uploads[@]}" --clobber
# Remove the retired VM download only after all verified APK files uploaded.
gh release view continuous --json assets --jq '.assets[].name' | while IFS= read -r name; do
  case "$name" in
    stearlight-quest.apk|stearlight-quest.apk.sha256|stearlight-quest-sources.zip|BUILD.json|SHA256SUMS|README.md|LICENSE.md) ;;
    *) gh release delete-asset continuous "$name" --yes ;;
  esac
done
