#!/usr/bin/env bash
# Builds the original LXR-02 voice code for the host and compares it with the Ferrum 8 port.
# Usage: tests/firmware_compare/build_and_run.sh /path/to/lxr-desktop-app
# (git clone https://github.com/erica-synths/lxr-desktop-app)
set -euo pipefail
FW="${1:?path to the lxr-desktop-app checkout}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source"
OUT="${2:-$HERE/build}"
mkdir -p "$OUT/fw"
DSP="$FW/Core/Src/Soundengine/DSPAudio"

# Copy the voice sources. Oscillator.c converts floats to uint32 in two places where the ARM FPU
# saturates but x86 wraps; make those conversions explicit so the host build sounds like the hardware.
for f in DrumVoice Snare HiHat CymbalVoice ResonantFilter SlopeEg2 Decay snapEg transientGenerator \
         transientTables wavetable distortion 1PoleLp lfo Oscillator; do
    cp "$DSP/$f.c" "$OUT/fw/$f.c"
done
sed -i -e 's/(uint32_t)(modBuffer\[i\]\*osc->fmMod)/armF2U_c(modBuffer[i]*osc->fmMod)/g' \
       -e 's/osc->phase + pwm \* (UINT32_MAX\/2)/armF2U_c((float)osc->phase + pwm * (float)(UINT32_MAX\/2))/g' \
       -e 's/index + pwm \* (UINT32_MAX\/2)/armF2U_c((float)index + pwm * (float)(UINT32_MAX\/2))/g' \
       "$OUT/fw/Oscillator.c"

CFLAGS="-O2 -std=gnu11 -w -I$HERE/stubs -I$FW/Core/Inc"
for f in "$OUT"/fw/*.c "$HERE/fw_glue.c"; do
    gcc $CFLAGS -c "$f" -o "$OUT/$(basename "${f%.c}").o"
done
g++ -O2 -std=c++17 -I"$HERE" -c "$HERE/compare.cpp" -o "$OUT/compare.o"
for f in LxrVoice PatchParams LxrTables; do g++ -O2 -std=c++17 -c "$SRC/dsp/$f.cpp" -o "$OUT/port_$f.o"; done
g++ "$OUT"/*.o -o "$OUT/compare" -lm
"$OUT/compare"
