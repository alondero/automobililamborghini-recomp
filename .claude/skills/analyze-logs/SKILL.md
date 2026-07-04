---
name: analyze-logs
description: Token Efficient summarisation of large trace and build logs to identify the first crash, recurring stubs, and RSP command patterns.
---

## Overview
This skill provides a high-signal summary of large log files (e.g., `logs_stderr.txt`, `trace.txt`) using the `tools/trace_analyst.py` utility. Use this to avoid reading voluminous text files directly into the context history.

## Workflow
1.  **Initial Scan:** Execute `python tools/trace_analyst.py <log_file>`.
2.  **Analyze First Error:** Focus exclusively on the `[!] FIRST CRASH/ERROR DETECTED` output to find the root cause.
3.  **Pattern Check:** Use the `Top 5 Most Frequent Lines` to identify infinite loops or repetitive noise.
4.  **Targeted Reading:** Only read raw lines from the log once a specific range or function has been identified in the summary.

## Rules
- **The 20-Line Rule:** NEVER read more than 20 lines of a raw log file directly. If a log is larger, always summarize it first.
- **Repetition Clause:** For RSP/RDP traces or any output with recurring stubs, use this skill regardless of length. Repetition is a primary cause of context bloat and reasoning distraction.
- **Surgical Access:** Only use `read_file` for specific 5-10 line windows (e.g., `start_line: 450, end_line: 460`) once the analyst tool has pinpointed the exact failure address.
