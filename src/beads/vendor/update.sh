#!/usr/bin/env bash
# Re-vendor the Mutable Instruments Beads DSP, the Clouds fx classes Brume
# uses (reverb, diffuser) and the stmlib subset they need
# from a pichenettes/eurorack checkout, then apply RobotBoy's local patch.
#
#   src/beads/vendor/update.sh /path/to/eurorack
#
# The checkout's stmlib submodule must be initialized. After an upstream bump,
# re-run, resolve any patch conflicts, and refresh robotboy.patch with
#   git diff --no-index <pristine copy> src/beads/vendor
# (see NOTICE.md for the pinned upstream commits).
set -euo pipefail

src="${1:?usage: update.sh /path/to/eurorack}"
here="$(cd "$(dirname "$0")" && pwd)"

[ -f "$src/beads/dsp/processor.h" ] || { echo "no beads/ in $src" >&2; exit 1; }
[ -f "$src/stmlib/stmlib.h" ] || { echo "stmlib submodule not initialized in $src" >&2; exit 1; }

rm -rf "$here/beads" "$here/clouds" "$here/stmlib"
mkdir -p "$here/beads/dsp/fx" "$here/clouds/dsp/fx" "$here/stmlib/dsp" "$here/stmlib/utils"

cp "$src"/beads/dsp/*.h "$src"/beads/dsp/*.cc "$here/beads/dsp/"
cp "$src"/beads/dsp/fx/*.h "$here/beads/dsp/fx/"
cp "$src"/beads/resources.h "$src"/beads/resources.cc "$here/beads/"

cp "$src"/clouds/dsp/frame.h "$here/clouds/dsp/"
for f in fx_engine.h reverb.h diffuser.h; do
  cp "$src/clouds/dsp/fx/$f" "$here/clouds/dsp/fx/"
done

cp "$src"/stmlib/stmlib.h "$here/stmlib/"
for f in cosine_oscillator.h delay_line.h dsp.h filter.h hysteresis_filter.h \
         hysteresis_quantizer.h parameter_interpolator.h sample_rate_converter.h \
         units.h units.cc; do
  cp "$src/stmlib/dsp/$f" "$here/stmlib/dsp/"
done
for f in dsp.h gate_flags.h random.h random.cc; do
  cp "$src/stmlib/utils/$f" "$here/stmlib/utils/"
done
cp "$src/stmlib/LICENSE" "$here/LICENSE"

if [ -s "$here/robotboy.patch" ]; then
  (cd "$here/../../.." && git apply --directory=src/beads/vendor -p1 "$here/robotboy.patch") \
    || { echo "robotboy.patch did not apply cleanly" >&2; exit 1; }
fi
echo "vendored from $(git -C "$src" rev-parse HEAD) (stmlib $(git -C "$src/stmlib" rev-parse HEAD))"
