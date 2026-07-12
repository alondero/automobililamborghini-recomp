#!/usr/bin/env bash
# list-changes.sh — print merged PRs, closed issues, and the merge-commit at HEAD
# since <base-tag>. Output is structured Markdown for the release-notes draft.
#
# Usage: list-changes.sh <base-tag> [head-ref]
#   base-tag: e.g. v0.4.2
#   head-ref: defaults to HEAD (use 'origin/main' to read remote state)

set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <base-tag> [head-ref]" >&2
  exit 2
fi

BASE_TAG="$1"
HEAD_REF="${2:-HEAD}"

# Sanity: base tag must exist
if ! git rev-parse -q --verify "refs/tags/${BASE_TAG}" >/dev/null; then
  echo "error: base tag '${BASE_TAG}' does not exist locally" >&2
  exit 1
fi

BASE_DATE="$(git log -1 --format='%ai' "${BASE_TAG}")"
HEAD_SHA="$(git rev-parse "${HEAD_REF}")"
HEAD_SHORT="$(git rev-parse --short "${HEAD_SHA}")"
HEAD_SUBJECT="$(git log -1 --format='%s' "${HEAD_REF}")"
HEAD_DATE="$(git log -1 --format='%ai' "${HEAD_REF}")"

echo "## Change summary: ${BASE_TAG} → ${HEAD_REF}"
echo
echo "**Base:** \`${BASE_TAG}\` (${BASE_DATE})"
echo "**Head:** \`${HEAD_SHORT}\` — ${HEAD_SUBJECT} (${HEAD_DATE})"
echo

# Commits since base
echo "### Commits since ${BASE_TAG}"
echo
echo '```'
git log --no-merges --oneline "${BASE_TAG}..${HEAD_REF}" || true
echo '```'
echo

# Merged PRs since base (uses gh)
echo "### Merged PRs since ${BASE_TAG}"
echo
PRS="$(gh pr list --state merged --base main --search "merged:>=${BASE_DATE%% *}" \
       --json number,title,mergedAt,author \
       --jq '.[] | "- **PR #\(.number)** (\(.mergedAt[:10])) — \(.title) — _@\(.author.login)_"')"
if [[ -n "${PRS}" ]]; then
  echo "${PRS}"
else
  echo "_None found via `gh pr list` — try `gh pr list --state merged --base main --limit 30` and filter manually._"
fi
echo

# Closed issues since base
echo "### Closed issues since ${BASE_TAG}"
echo
ISSUES="$(gh issue list --state closed --search "closed:>=${BASE_DATE%% *}" \
          --json number,title,closedAt \
          --jq '.[] | "- **#\(.number)** (\(.closedAt[:10])) — \(.title)"')"
if [[ -n "${ISSUES}" ]]; then
  echo "${ISSUES}"
else
  echo "_None closed since base tag._"
fi
echo

# Tag target recommendation
echo "### Tag target recommendation"
echo
# Find the most recent merge commit on main reachable from HEAD
TAG_TARGET="$(git log --merges --first-parent -n 1 --format='%H %s' "${HEAD_REF}")"
echo "Most recent merge commit on \`${HEAD_REF}\`:"
echo
echo '```'
echo "${TAG_TARGET}"
echo '```'
echo
echo "Tag \`<new-version>\` should point at \`$(echo "${TAG_TARGET}" | awk '{print $1}' | cut -c1-7)\`."