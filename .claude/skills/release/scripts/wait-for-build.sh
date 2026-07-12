#!/usr/bin/env bash
# wait-for-build.sh — block until a Build & Release workflow run finishes.
# Exits 0 on success, non-zero on failure/cancellation.
#
# Usage: wait-for-build.sh <run-id>
#   run-id: the GitHub Actions database ID (e.g. 29203966918)

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <run-id>" >&2
  exit 2
fi

RUN_ID="$1"

if ! [[ "${RUN_ID}" =~ ^[0-9]+$ ]]; then
  echo "error: '${RUN_ID}' does not look like a run ID (digits only)." >&2
  exit 1
fi

echo "→ Watching run ${RUN_ID} (Ctrl-C to detach; the run will keep going)"
echo

# --exit-status makes gh return non-zero on any job failure, which is what
# we want — it propagates a failed build as a script failure.
gh run watch "${RUN_ID}" --exit-status --interval 30

# After a successful run, print a one-liner summary so the caller can verify
# which commit/SHA actually got built (workflows run on whatever HEAD was at
# dispatch time, which may differ from a tag you created earlier).
echo
echo "✓ Run ${RUN_ID} succeeded."
echo
gh run view "${RUN_ID}" --json headSha,headBranch,conclusion \
   --jq '"  HEAD:      \(.headSha)\n  Branch:    \(.headBranch)\n  Conclusion: \(.conclusion)"'