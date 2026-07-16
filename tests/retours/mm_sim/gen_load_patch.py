#!/usr/bin/env python3
"""Generate an N-instance Retours-only patch (no cabling needed -- the patch
player runs process() on every loaded module regardless of cabling) for CPU
load-test amplification. A single instance's cost is too small to resolve
above timer noise on a fast host; N copies scale up the signal N x so the
per-instance cost can be recovered by dividing the total measured time by N.

Copied/adapted from tests/yellowjacket/mm_sim/gen_load_patch.py
(worktree-wasp) -- same technique, different module.

IMPORTANT: module_id 0 is hardcoded by patch_player.hh as the Hub/panel
module (`modules[0] = ModuleFactory::create(PanelDef::typeID)`, loop over
user modules starts at i=1) -- whatever is in module_slugs[0] is silently
ignored. Real modules must be numbered 1..N.
"""
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 16
out_path = sys.argv[2] if len(sys.argv) > 2 else f"retours_load_{N}.yml"

lines = []
lines.append("PatchData:")
lines.append(f"  patch_name: retours_load_{N}")
lines.append(f"  description: Retours CPU load test, {N} instances")
lines.append("  module_slugs:")
lines.append("    0: '4msCompany:HubMedium'")
for i in range(1, N + 1):
    lines.append(f"    {i}: 'RobotBoy:Retours'")
lines.append("  int_cables: []")
lines.append("  mapped_ins: []")
lines.append("  mapped_outs: []")
lines.append("  static_knobs: []")
lines.append("  vcvModuleStates: []")
lines.append("  bypassed_modules: []")

with open(out_path, "w") as f:
    f.write("\n".join(lines) + "\n")
print(f"wrote {out_path}: {N} instances (module_id 1..{N})")
