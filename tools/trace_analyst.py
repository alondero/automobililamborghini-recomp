import sys
import re
from collections import Counter

def analyze_trace(file_path, limit=20):
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception as e:
        return f"Error reading log: {e}"

    # Patterns for N64Recomp, mupen64plus, and project-specific logs
    patterns = {
        'CRASH': re.compile(r'CRASH|SIGSEGV|Segmentation fault|Assertion.*failed|CORE Error|Exception \(PC=|TLB Miss|Invalid opcode', re.I),
        'STUB': re.compile(r'STUB: ([\w_]+)'),
        'RSP': re.compile(r'\[RSP\] ([\w_]+)'),
        'TRI1': re.compile(r'\[TRI1_EX2\]'),
        'MOVEWORD': re.compile(r'\[MOVEWORD\]'),
        'FUNC_CALL': re.compile(r'(func_8[0-9A-F]{7})'),
        'FRAME_SUMMARY': re.compile(r'\[FRAME_SUMMARY\]'),
    }

    results = {k: [] for k in patterns}
    line_counts = Counter()
    first_crash = None

    for i, line in enumerate(lines):
        clean_line = line.strip()
        line_counts[clean_line] += 1
        
        for name, pattern in patterns.items():
            match = pattern.search(line)
            if match:
                if name == 'CRASH' and first_crash is None:
                    # Capture crash with context
                    start = max(0, i-2)
                    end = min(len(lines), i+3)
                    first_crash = lines[start:end]
                
                if name == 'STUB' or name == 'RSP' or name == 'FUNC_CALL':
                    results[name].append(match.group(1))
                else:
                    results[name].append(line)

    # Summarize results
    output = [f"--- Trace Analysis of {file_path} ---"]
    output.append(f"Total lines: {len(lines)}")
    
    if first_crash:
        output.append("\n[!] FIRST CRASH/ERROR DETECTED:")
        output.extend([f"  {l.strip()}" for l in first_crash])
    
    # Frequency Analysis
    output.append("\nTop 5 Most Frequent Lines:")
    for line, count in line_counts.most_common(5):
        if count > 1:
            output.append(f"  ({count}x) {line[:100]}...")

    # Unique Stubs/RSP Commands
    for key in ['STUB', 'RSP', 'FUNC_CALL']:
        unique_items = Counter(results[key]).most_common(10)
        if unique_items:
            output.append(f"\nUnique {key} (Top 10):")
            for item, count in unique_items:
                output.append(f"  - {item} ({count}x)")

    # Status of key markers
    output.append("\nMarker Status:")
    output.append(f"  - TRI1_EX2 (Triangles): {'PRESENT (' + str(len(results['TRI1'])) + 'x)' if results['TRI1'] else 'ABSENT'}")
    output.append(f"  - MOVEWORD (Segments): {'PRESENT (' + str(len(results['MOVEWORD'])) + 'x)' if results['MOVEWORD'] else 'ABSENT'}")
    output.append(f"  - FRAME_SUMMARY (PC port): {'PRESENT (' + str(len(results['FRAME_SUMMARY'])) + 'x)' if results['FRAME_SUMMARY'] else 'ABSENT'}")
    # Show last FRAME_SUMMARY for quick geometry check
    if results['FRAME_SUMMARY']:
        last_frame_lines = [l for l in lines if '[FRAME_SUMMARY]' in l]
        if last_frame_lines:
            output.append(f"  Last frame: {last_frame_lines[-1].strip()}")

    return "\n".join(output)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python tools/trace_analyst.py <log_file>")
        sys.exit(1)
    
    print(analyze_trace(sys.argv[1]))
