#!/usr/bin/env python3
"""Runs `engine --microbench N` and checks the invariants it reports (cache correctness,
undo round trip, at-most-once world recompute). Prints the timings."""
import json, subprocess, sys
engine = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
out = subprocess.run([engine, "--headless", "--microbench", str(n)], capture_output=True, text=True,
                     stdin=subprocess.DEVNULL, timeout=300).stdout
line = next(l for l in out.splitlines() if l.startswith("MICROBENCH "))
m = json.loads(line.split(" ", 1)[1])
print(json.dumps(m, indent=1, sort_keys=True))
bad = []
if m["world_cache_mismatches"] != 0: bad.append("world cache != from-scratch")
if m["world_1pct_moving_recomputed_per_frame"] > n * 0.03: bad.append("recomputes more than the moved subtree")
if not (m["history_undo_restored"] and m["history_redo_restored"]): bad.append("history undo/redo")
if m["history_undone_steps"] != 64: bad.append("history steps")
if m["history_64_steps_bytes"] * 50 > m["history_64_steps_old_style_bytes"]: bad.append("delta history not small")
print("RESULT:", "ALL PASS" if not bad else bad)
sys.exit(1 if bad else 0)
