#!/usr/bin/env bash
# tag-and-dispatch.sh — tag the merge commit at HEAD on main, push the tag,
# and dispatch the Build & Release workflow with --prerelease=false --draft=false
# (the values that produce a properly-bound release).
#
# Usage: tag-and-dispatch.sh <version>
#   version: e.g. v0.4.3 (must match v<MAJOR>.<MINOR>.<PATCH>)

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>" >&2
  exit 2
fi

VERSION="$1"

# Validate version shape
if ! [[ "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]]; then
  echo "error: '${VERSION}' does not match v<MAJOR>.<MINOR>.<PATCH>" >&2
  exit 1
fi

# Make sure we're on main and HEAD is a merge commit (the convention
# past releases follow — v0.4.0=5e07da1, v0.4.1=1f489fb, v0.4.2=03e533a).
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [[ "${CURRENT_BRANCH}" != "main" ]]; then
  echo "error: must be on 'main' (currently on '${CURRENT_BRANCH}')." >&2
  echo "       Switch with: git checkout main" >&2
  exit 1
fi

HEAD_SHA="$(git rev-parse HEAD)"
if [[ -z "$(git log --merges --first-parent -n 1 --format='%H' HEAD | grep -F "${HEAD_SHA}" || true)" ]]; then
  echo "warning: HEAD (${HEAD_SHA}) is not a merge commit." >&2
  echo "         Past releases tag at the merge commit (e.g. v0.4.2 = 03e533a)." >&2
  echo "         Re-run after a PR lands, or override with:" >&2
  echo "           git tag ${VERSION} <desired-sha> && git push origin ${VERSION}" >&2
  exit 1
fi

# Refuse if tag already exists locally OR on origin
if git rev-parse -q --verify "refs/tags/${VERSION}" >/dev/null; then
  echo "error: tag '${VERSION}' already exists locally." >&2
  echo "       If you want to re-cut, delete first: git tag -d ${VERSION}" >&2
  exit 1
fi
if git ls-remote --tags origin "${VERSION}" 2>/dev/null | grep -q "${VERSION}"; then
  echo "error: tag '${VERSION}' already exists on origin." >&2
  exit 1
fi

echo "→ Tagging ${VERSION} at ${HEAD_SHA}"
git tag "${VERSION}" "${HEAD_SHA}"

echo "→ Pushing ${VERSION} to origin"
git push origin "${VERSION}"

echo "→ Dispatching Build & Release workflow (prerelease=false, draft=false)"
# Capture the run ID — the dispatch returns immediately and prints nothing
# useful, so we fetch the most recent in_progress run afterwards.
gh workflow run "Build & Release" --ref main \
  -f "tag=${VERSION}" \
  -f "prerelease=false" \
  -f "draft=false"

# Brief settle before listing runs (gh workflow run is fire-and-forget)
sleep 3

RUN_ID="$(gh run list --workflow="Build & Release" --limit 1 --json databaseId,status \
          --jq '.[] | select(.status=="in_progress" or .status=="queued") | .databaseId' \
          | head -n 1)"

if [[ -z "${RUN_ID}" ]]; then
  echo "warning: could not find the dispatched run. List with: gh run list --workflow='Build & Release' --limit 3" >&2
  exit 0
fi

echo
echo "✓ Workflow dispatched."
echo "  Version:    ${VERSION}"
echo "  Tag SHA:    ${HEAD_SHA}"
echo "  Run ID:     ${RUN_ID}"
echo
echo "Next: ./scripts/wait-for-build.sh ${RUN_ID}"