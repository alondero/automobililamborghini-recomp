#!/usr/bin/env bash
# update-notes.sh — replace the workflow's placeholder notes with the rich body.
#
# Usage: update-notes.sh <version> <notes-file>
#   version:    e.g. v0.4.3
#   notes-file: path to a Markdown file matching references/release-notes-template.md

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <version> <notes-file>" >&2
  exit 2
fi

VERSION="$1"
NOTES_FILE="$2"

if [[ ! -f "${NOTES_FILE}" ]]; then
  echo "error: notes file '${NOTES_FILE}' not found" >&2
  exit 1
fi

if ! [[ "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]]; then
  echo "error: '${VERSION}' does not match v<MAJOR>.<MINOR>.<PATCH>" >&2
  exit 1
fi

# Refuse to push notes that look like the workflow placeholder
if head -n 2 "${NOTES_FILE}" | grep -q "Automated build from commit"; then
  echo "error: notes file appears to be the workflow placeholder." >&2
  echo "       Use a body drafted from references/release-notes-template.md." >&2
  exit 1
fi

echo "→ Updating ${VERSION} notes from ${NOTES_FILE}"
gh release edit "${VERSION}" --notes-file "${NOTES_FILE}"

echo
echo "✓ Notes updated. Verify with:"
echo "    gh release view ${VERSION} --json body --jq '.body' | head -n 30"