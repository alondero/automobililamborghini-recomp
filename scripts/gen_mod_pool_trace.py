"""Generate a diagnostic-only recompiler config or summarize its scenario run.

The normal game build uses lamborghini.us.toml. This config adds pre-call printf
hooks for the three audited pool-reset callers and keeps all paths absolute.
--summarize reads the artifacts produced by scenarios/mod-pool-callers.json.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[1]
SITES = {0x800062D8: "boot_pad_apply_calibration",
         0x80037E1C: "func_800389A4", 0x8003C1CC: "func_8003CD84"}


def trace_config(source):
    source = Path(source).resolve()
    text = source.read_text(encoding="utf-8")
    inputs = tomllib.loads(text)["input"]
    for field in ("output_func_path", "symbols_file_path", "rom_file_path"):
        path = (source.parent / inputs[field]).resolve().as_posix()
        text, count = re.subn(rf"(?m)^{field}\s*=.*$",
                             lambda _: f"{field} = {json.dumps(path)}", text, count=1)
        if count != 1:
            raise ValueError(f"missing input path: {field}")
    for pc, function in SITES.items():
        hook = ('{ extern int printf(const char*, ...); printf("[mod-pool-trace] caller='
                + hex(pc) + r' state=%d\n", (int)MEM_H(0, (gpr)(int32_t)0x800CE6ACu)); }')
        text += (f'\n[[patches.hook]]\nfunc = "{function}"\nbefore_vram = {pc:#x}\n'
                 f'text = {json.dumps(hook)}\n')
    return text


def summarize_run(artifact):
    runner = json.loads((artifact / "runner-result.json").read_text(encoding="utf-8"))
    if runner["outcome"] != "passed" or runner["scenario"] != "mod-pool-callers":
        raise ValueError("expected a successful mod-pool-callers scenario")
    result = json.loads((artifact / "harness-result.json").read_text(encoding="utf-8"))
    if result["exit_code"] != 0 or result["outcome"] != "passed" or not result["replay"]["complete"]:
        raise ValueError("scenario did not complete successfully")
    log = (artifact / "stdout.log").read_text(encoding="utf-8")
    counts = Counter((site, int(state)) for site, state in re.findall(
        r"\[mod-pool-trace\] caller=(0x[0-9a-f]+) state=(\d+)", log))
    if {int(site, 16) for site, _ in counts} != SITES.keys():
        raise ValueError("trace must include every audited caller; use the diagnostic build")
    return {
        "status": "confirmed measurement with diagnostic pre-call hooks",
        "rom_region": "USA",
        "rom_sha256": "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c",
        "recording": "recordings/menu-to-race-smoke.jsonl", "headless": True,
        "replay_frames": result["replay"]["frames_consumed"], "vis": result["vis"],
        "swaps": result["swaps"], "final_state": result["final_state"],
        "reset_calls": [{"caller": site, "state": state, "count": count}
                        for (site, state), count in sorted(counts.items())],
        "race_state_8_resets": sum(count for (_, state), count in counts.items() if state == 8),
        "scope": "Direct-call inventory and one menu-to-race run; not every menu choice or arbitrary mod/native/indirect call.",
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "lamborghini.us.toml")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summarize", type=Path, help="scenario artifact directory to summarize as JSON")
    args = parser.parse_args(argv)
    if args.output.resolve() == args.source.resolve():
        parser.error("diagnostic output must not overwrite the canonical config")
    try:
        text = (json.dumps(summarize_run(args.summarize), indent=2) + "\n"
                if args.summarize else trace_config(args.source))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8", newline="\n")
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
