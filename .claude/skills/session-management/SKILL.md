---
name: session-management
description: Close the current work session — run tests, write a lean handoff, and reflect progress in the relevant GitHub issue — so the next session has a clear, grounded starting point.
---

## Overview
End-of-session workflow: summarize changes, validate, and prepare the hand-off.

## Workflow
1.  **Summarize changes:** `git diff --stat HEAD` to identify modified files.
2.  **Validate:** run the tests and record the result.
3.  **Context review:** read the current `nextsessionprompt.md` (backlog lives in GitHub issues).
4.  **User input:** ask "What should the next session start with, and any blockers to capture?"
5.  **Update docs:**
    - **`nextsessionprompt.md`:** rewrite (don't append) with the date, accomplishments
      (exact files/functions), blockers (with symptoms), and the GROUNDED next *area* to
      investigate — a theory/area, NOT an implementation plan or "exact first move".
    - **GitHub issue:** reflect progress on the relevant issue — close it if done, or
      `gh issue comment <n>` with a one-paragraph status (what landed + what's blocking).
6.  **Summary:** ≤8 lines — changes, test status, next starting point.

## Rules
- **Method line per load-bearing claim.** Each carries how it was measured
  (`[method: live-ares | gdb-info-breakpoints | static-asm | recompiled-C]`) and a
  `[re-verify: <≤5-min command>]`. A claim with no method line is unverified input, not a
  premise — the next session will (correctly) re-derive it, so an unverified handoff wastes work.
- **Keep the handoff tight.** It is a per-session hand-off, not a running log — rewrite it lean.
  State the grounded next area, not a step-by-step plan.
- **Pre-flight ground-truth probe.** If the handoff sets up a multi-session investigation, name
  the cheap probe (one state-counter / dispatch-trace run) that confirms the game is actually in
  the assumed state BEFORE any instrumentation is trusted.
- **Specificity.** Accomplishments include exact file paths and function names.
