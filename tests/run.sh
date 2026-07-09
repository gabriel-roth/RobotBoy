#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
fail=0
for d in mf20 loooop particules; do
  [ -d "$d" ] || continue
  for t in "$d"/test_*.cpp; do
    [ -e "$t" ] || continue
    out="/tmp/robotboy_$(basename "$t" .cpp)"
    echo "== building $t =="
    # Some tests need non-header-only .cpp sources from src/ linked in
    # alongside the test file (e.g. LoopEngine.cpp). If a sibling
    # "<test>.cpp.extra" file exists, each line in it is an extra source
    # path (relative to this tests/ dir) appended to the g++ invocation.
    extra=()
    extra_file="$t.extra"
    if [ -f "$extra_file" ]; then
      while IFS= read -r line; do
        [ -n "$line" ] && extra+=("$line")
      done < "$extra_file"
    fi
    g++ -std=c++20 -O2 -I../src -I../src/loooop -I../src/mf20 -I../src/particules \
        -I../src/vendor/beads_dsp/include -o "$out" "$t" "${extra[@]+"${extra[@]}"}" && "$out" || fail=1
  done
done

echo "== python guard tests =="
python3 -m unittest discover -s . -p 'test_*.py' -v || fail=1

exit $fail
