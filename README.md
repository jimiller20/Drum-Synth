# Ferrum 8

An 8-channel drum synth plugin (VST3 + Standalone, JUCE 9): the **sound engine of the Erica Synths LXR-02**
(ported from its GPL-3.0 open-source firmware) with a **Microtonic-style workflow** (8 channels,
12 patterns A-L, chains, trig/accent/fill/length lanes, swing, fill rate, matrix editor, sound morph,
randomizer). Original look; not affiliated with Erica Synths or Sonic Charge.

## Build on Windows (Visual Studio 2022 + CMake 3.22+)

```
git clone <this folder> Ferrum8   (or unzip it)
cd Ferrum8
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Ferrum8_VST3 Ferrum8_Standalone
```
CMake downloads JUCE 9.0.4 automatically (pinned commit). Output:
`build/Ferrum8_artefacts/Release/VST3/Ferrum 8.vst3` - copy that folder to
`C:\Program Files\Common Files\VST3\` and rescan in FL Studio.
(Or configure with `-DFERRUM_COPY_PLUGIN=ON` from an admin prompt to copy automatically.)
Faster dev builds: add `-DFERRUM_LTO=OFF`.

## Groove & expression (added Oct 29 2026)

Below TRIG / ACC / FILL there is now an **expression lane** with seven tabs. Each step of each
channel has its own value. Drag up/down to draw across steps, double-click resets, right-click
lists every value, Shift edits all unmuted channels.

| Lane | Values | What it does |
|---|---|---|
| GHOST | 0-3 | 1 Soft / 2 Ghost / 3 Whisper: each level quieter (-3 / -6.5 / -11 dB), darker (high shelf), rounder attack, less transient and pitch-bend, slightly shorter and lower |
| DECAY | -8..+8 | per-step amp decay |
| TONE | -8..+8 | per-step brightness (high shelf, -12 dB .. +6 dB) |
| PITCH | -12..+12 | semitones |
| NUDGE | -12..+12 | micro-timing, up to 1/3 of a step early or late |
| CHANCE | 100%..13%, 1:2 2:2 1:4 2:4 3:4 4:4 | probability or "every Nth loop" conditions |
| ROLL | Flam, Drag, x2, x3, x4 | grace notes before the hit or ratchets inside the step |

Per-channel **FEEL** knobs (patch panel): **Ghost** (how strong ghost levels are, 0-200%),
**Human** (timing and dynamics that wander like a player, random walk), **Vary** (independent
per-hit random decay / pitch / tone / filter / transient: happy accidents).
Pattern menu: **Add Ghost Notes (DnB)**, **Randomize Expression**, **Groove Shake**, **Clear
Expression**; Randomize Pattern also adds a light layer of expression. New factory kit
**Jungle Ghosts** demonstrates all of it (patterns A -> B chained). MIDI export writes ghost
velocities, nudges, flams and ratchets. With every lane at 0 and Human / Vary at 0 the voices are
still bit-identical to the LXR firmware (firmware compare passes).

## Dice / randomizer (added Oct 29 2026)

Toolbar: **Sounds** (new sounds, patterns kept), **Beat** (new steps, sounds kept), **All**, and
the **▾ Dice settings** pop-over:
- **Style**: Auto, House/Techno, Breakbeat, Drum & Bass, Trap, Dembow, Electro, Footwork,
  Euclidean, Glitch. Template styles hold two 16-step probability maps per role (straight and
  syncopated); **Complexity** blends between them, **Density** scales them, anchors (house kick,
  DnB backbeats, trap snare on 3) always play. Each style adds its own expression (DnB ghost
  snares, trap hat rolls, house open-hat decay ...); **Expression** sets how much.
- **Beat writes**: the selected pattern, or **A-L as a set**: Main, Variation, Main + fill,
  Busier, Breakdown, Build-up, Half-time, Hat run, Minimal, Glitch, Variation 2, Transition.
- **Kit character** (Clean, Analog, Gritty, Lo-Fi, Metallic) and **Key** (pitched drums snap to
  a minor pentatonic) for new sounds.
- **Evolve while playing**: Orbit (every N loops a variation of the original; turning it off
  restores the original; "Keep variation" makes the current one the new centre) or Drift
  (changes accumulate). **Mutation** sets the size of each change.
- **History**: the last 16 rolls; pick one to bring it back.
- **Locks**: the padlock on each channel strip keeps that channel's sound and steps out of every
  roll. Pattern panel: the dice tool rolls the selected channel's steps (Shift: whole beat); the
  dice next to the expression tabs rolls only the shown lane.

## Status (handoff, Oct 28 2026)

Done and tested on Linux (Windows build not yet tried):
- **DSP port** (`Source/dsp`): drum / snare / cymbal / hi-hat voices, wavetable + FM + PWM oscillators,
  ZDF filter (7 types), slope/decay/snap envelopes, 14 transients, LFO, velocity mod, FX bus
  (tube/fold/rat distortion, ring mod, compressor, ping-pong delay). Verified against the original
  firmware code compiled on the host: 25 scenarios match within 0-3 LSB of 32768
  (`tests/firmware_compare/build_and_run.sh <path-to-lxr-desktop-app>`).
- **Engine / sequencer** (`Source/engine`): host-synced pattern engine, synced and immediate
  start/stop, chains, swing warp, fill rolls, choke groups, mutes, MIDI map (C1-G1 trigger,
  C2-G2 mute, C3-B3 patterns, C4 stop), pitched mode, pattern launch modes, MIDI out.
- **Randomizer**: per-sound recipes (kick, tom, perc, zap, snare, clap, hats, cymbal, chaos),
  alter, randomize all, random morph targets, rhythm-aware pattern randomizer.
- **Plugin**: 403 host parameters, morph A/B end points, presets (.f8preset), drum patches
  (.f8drum), undo/redo (40), MIDI-file export + drag-out, WAV export, 5 factory kits.
- **UI**: all sections working, zoom 60-200%. Snapshots look right.
- Tests: `tests/render_test.cpp` (engine), `tests/plugin_tests.cpp` (processor + UI snapshots,
  build with `-DFERRUM_BUILD_TESTS=ON`) - all passing.

Next steps when picking back up:
1. Build on Windows with MSVC and fix any compiler differences; load in FL Studio.
2. Listen-test the factory kits and tune their levels (Concrete peaks hot before the -6 dB master).
3. Possible extras: MIDI learn, 16 program slots, preset browser previewing.

## Licences
GPL-3.0 (the LXR firmware licence). JUCE is AGPLv3 or commercial: fine for personal use; distributing
a closed-source build would need a commercial JUCE licence. Barlow Condensed font: SIL OFL.
