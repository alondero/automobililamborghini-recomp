#!/usr/bin/env bash
# verify-release.sh — sanity-check a release is properly bound and complete.
# Runs five checks and prints pass/fail per check; exits non-zero if any fails.
#
# Usage: verify-release.sh <version>
#   version: e.g. v0.4.3

set -uo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>" >&2
  exit 2
fi

VERSION="$1"
EXPECTED_ASSETS=(
  "lamborghini-recomp-linux-x64.zip"
  "lamborghini-recomp-windows-x64.zip"
)

PASS=0
FAIL=0

note_pass() { echo "  ✓ $1"; PASS=$((PASS+1)); }
note_fail() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }

echo "Verifying ${VERSION}:"
echo

# Check 1: git tag on origin
if git ls-remote --tags origin "${VERSION}" 2>/dev/null | grep -q "${VERSION}"; then
  TAG_SHA="$(git ls-remote --tags origin "${VERSION}" | awk '{print $1}')"
  note_pass "tag ${VERSION} exists on origin (${TAG_SHA:0:7})"
else
  note_fail "tag ${VERSION} NOT on origin"
fi

# Check 2: release accessible via tag-keyed API
RELEASE_JSON="$(gh api "repos/alondero/automobililamborghini-recomp/releases/tags/${VERSION}" 2>&1)" || true
if echo "${RELEASE_JSON}" | grep -q '"message":"Not Found"'; then
  note_fail "release not found via /releases/tags/${VERSION} (workflow may not have finished)"
elif [[ -z "${RELEASE_JSON}" ]]; then
  note_fail "release API call returned empty"
else
  note_pass "release accessible via tag-keyed API"
fi

# Check 3: html_url is properly tag-bound (not the untagged-<id> synthetic URL)
HTML_URL="$(echo "${RELEASE_JSON}" | grep -o '"html_url":"[^"]*"' | head -n 1 | cut -d'"' -f4)"
if [[ "${HTML_URL}" == *"releases/tag/${VERSION}" ]]; then
  note_pass "html_url is properly tag-bound: ${HTML_URL}"
elif [[ "${HTML_URL}" == *"untagged-"* ]]; then
  note_fail "html_url is the synthetic 'untagged-<id>' pattern — the --draft trap"
  note_fail "  url: ${HTML_URL}"
  echo "         See references/gotchas.md — release was created as a draft."
else
  note_fail "html_url unexpected: ${HTML_URL:-<empty>}"
fi

# Check 4: draft and prerelease flags
IS_DRAFT="$(echo "${RELEASE_JSON}" | grep -o '"draft":[a-z]*' | head -n 1 | cut -d: -f2)"
IS_PRERELEASE="$(echo "${RELEASE_JSON}" | grep -o '"prerelease":[a-z]*' | head -n 1 | cut -d: -f2)"
if [[ "${IS_DRAFT}" == "false" ]]; then
  note_pass "draft: false"
else
  note_fail "draft: ${IS_DRAFT:-<unset>} (expected false to match prior releases)"
fi
if [[ "${IS_PRERELEASE}" == "false" ]]; then
  note_pass "prerelease: false"
else
  note_fail "prerelease: ${IS_PRERELEASE:-<unset>} (expected false — 'Pre-release quality' is body copy only)"
fi

# Check 5: assets uploaded — use a fresh gh call that returns parseable JSON
ASSETS_JSON="$(gh api "repos/alondero/automobililamborghini-recomp/releases/tags/${VERSION}" \
               --jq '.assets // []' 2>/dev/null || echo '[]')"
for expected in "${EXPECTED_ASSETS[@]}"; do
  ASSET_LINE="$(echo "${ASSETS_JSON}" \
                | grep -o "\"name\":\"${expected}\"[^}]*\"size\":[0-9]*" \
                | head -n 1 || true)"
  if [[ -n "${ASSET_LINE}" ]]; then
    SIZE="$(echo "${ASSET_LINE}" | grep -o '"size":[0-9]*' | cut -d: -f2)"
    SIZE_MB="$(awk "BEGIN { printf \"%.0f\", ${SIZE:-0}/1024/1024 }")"
    note_pass "asset uploaded: ${expected} (${SIZE_MB} MB)"
  else
    note_fail "asset missing: ${expected}"
  fi
done

echo
echo "Result: ${PASS} passed, ${FAIL} failed"

if [[ ${FAIL} -gt 0 ]]; then
  exit 1
fi
exit 0