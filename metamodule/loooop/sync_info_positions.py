#!/usr/bin/env python3
"""Position-sync metamodule/loooop/*_info.hh headers and the VCV display
rects from the canonical res/*.svg panels at the repo root.

The headers are HAND-MAINTAINED: element names, order, structs, defaults, and
menu-only alt-params are a contract with the matching *Core.cc. Header
element coordinates are synced by the generic tool
~/Dev/vcv-panel-gen/mm_sync.py (strict mode, name-matched through each
module's sync-map-*.yaml); this script then
patches the waveform display widget's rect in the matching src/loooop/*.cpp
from the SVG's SCREEN rect — the one coordinate neither mm_sync nor the
build-install.sh coord sync covers. Each module's cpp is validated before its
header is touched, and on any mismatch that module's files are left unwritten
and the run exits nonzero (other modules are still processed independently).

Usage: python3 metamodule/loooop/sync_info_positions.py      # all modules
       python3 metamodule/loooop/sync_info_positions.py --module Lop \
           --header PATH --svg PATH --cpp PATH [--map PATH]   # single (tests)
"""
import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
# https://github.com/gabriel-roth/vcv-panel-gen, cloned as a sibling by
# default; override with VCV_PANEL_GEN_DIR if it lives elsewhere.
PANEL_GEN = os.environ.get(
    "VCV_PANEL_GEN_DIR", os.path.expanduser("~/Dev/vcv-panel-gen"))

# HERE is metamodule/loooop/; the repo root is two levels up. The canonical
# panel SVGs live at res/ (vcv/res is a build-time copy) and the module
# sources with the display rect at src/loooop/.
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

MODULES = [
    {
        "name": "Loooop",
        "header": os.path.join(HERE, "Loooop_info.hh"),
        "svg": os.path.join(ROOT, "res", "Loooop.svg"),
        "cpp": os.path.join(ROOT, "src", "loooop", "Loooop.cpp"),
        "map": os.path.join(HERE, "sync-map-loooop.yaml"),
    },
    {
        "name": "Lop",
        "header": os.path.join(HERE, "Lop_info.hh"),
        "svg": os.path.join(ROOT, "res", "Lop.svg"),
        "cpp": os.path.join(ROOT, "src", "loooop", "Lop.cpp"),
        "map": os.path.join(HERE, "sync-map-lop.yaml"),
    },
]

# The VCV waveform display widget's rect in src/loooop/*.cpp — the one panel
# coordinate the build-install.sh coord sync doesn't cover.
CPP_POS_RE = re.compile(
    r"^(\s*display->box\.pos = mm2px\(Vec\()(-?\d+\.?\d*),\s*(-?\d+\.?\d*)(\)\);.*)$")
CPP_SIZE_RE = re.compile(
    r"^(\s*display->box\.size = mm2px\(Vec\()(-?\d+\.?\d*),\s*(-?\d+\.?\d*)(\)\);.*)$")


def fail(name, msg):
    sys.stderr.write(f"sync_info_positions: {name}: {msg}\n")
    sys.exit(1)


def screen_rect(name, svg_path):
    for node in ET.parse(svg_path).getroot().iter():
        if node.get("id", "").partition("#")[0] == "SCREEN":
            if node.tag.split("}")[-1] != "rect":
                fail(name, "SCREEN must be a <rect>")
            return (float(node.get("x")), float(node.get("y")),
                    float(node.get("width")), float(node.get("height")))
    fail(name, svg_path + ": no SCREEN component")


def sync_module(name, header_path, svg_path, cpp_path, map_path):
    # Validate the cpp and locate the SCREEN rect BEFORE mm_sync writes the
    # header, so a cpp/SVG mismatch can't leave header and widget out of step.
    cpp_lines = open(cpp_path).read().splitlines()
    pos_hits = [i for i, l in enumerate(cpp_lines) if CPP_POS_RE.match(l)]
    size_hits = [i for i, l in enumerate(cpp_lines) if CPP_SIZE_RE.match(l)]
    if len(pos_hits) != 1 or len(size_hits) != 1:
        fail(name, f"{cpp_path}: expected exactly one display->box.pos and "
             f"one display->box.size line, found {len(pos_hits)}/{len(size_hits)}")
    sx, sy, sw, sh = screen_rect(name, svg_path)

    python = os.path.join(PANEL_GEN, ".venv", "bin", "python")
    if not os.path.exists(python):
        python = sys.executable
    r = subprocess.run([python, os.path.join(PANEL_GEN, "mm_sync.py"),
                        "--strict", "--header", header_path,
                        "--svg", svg_path, "--map", map_path],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        print(f"{name}: {line}")
    if r.returncode != 0:
        for line in r.stderr.splitlines():
            sys.stderr.write(f"sync_info_positions: {name}: {line}\n")
        sys.exit(1)          # mm_sync never writes on failure

    pm = CPP_POS_RE.match(cpp_lines[pos_hits[0]])
    cpp_lines[pos_hits[0]] = f"{pm.group(1)}{sx:.3f}, {sy:.3f}{pm.group(4)}"
    sm = CPP_SIZE_RE.match(cpp_lines[size_hits[0]])
    cpp_lines[size_hits[0]] = f"{sm.group(1)}{sw:.3f}, {sh:.3f}{sm.group(4)}"
    open(cpp_path, "w").write("\n".join(cpp_lines) + "\n")
    print(f"{name}: VCV display rect synced")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", help="single-module override: header path")
    ap.add_argument("--svg", help="single-module override: SVG path")
    ap.add_argument("--cpp", help="single-module override: VCV cpp path")
    ap.add_argument("--map", help="single-module override: mm_sync map path")
    ap.add_argument("--module", choices=sorted(m["name"] for m in MODULES),
                    default="Loooop",
                    help="which module's sync map to use with "
                         "--header/--svg/--cpp (default: Loooop, for "
                         "backward-compatible test invocation)")
    args = ap.parse_args()

    by_name = {m["name"]: m for m in MODULES}
    if args.header or args.svg or args.cpp or args.map:
        # Single-module mode (used by tests): all three must be given.
        if not (args.header and args.svg and args.cpp):
            fail(args.module, "--header/--svg/--cpp must be given together")
        sync_module(args.module, args.header, args.svg, args.cpp,
                    args.map or by_name[args.module]["map"])
        return

    failed = []
    for mod in MODULES:
        try:
            sync_module(mod["name"], mod["header"], mod["svg"], mod["cpp"],
                        mod["map"])
        except SystemExit:
            # fail()/mm_sync already reported the error; contain it to this
            # module so the remaining modules still get processed.
            failed.append(mod["name"])
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
