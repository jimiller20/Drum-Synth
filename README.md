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

## FX rack (added Oct 29 2026)

The **FX RACK** tab (or the Tab key, or a bus card in the bottom bar) replaces the old single FX
bus. There are three buses: **FX 1** and **FX 2** (a channel plays through one when its Output is
set to FX 1 / FX 2, or with the channel numbers in the bus header) and **Master** (processes
everything on output A). Each bus has three **lanes in parallel**, each a chain of five **slots in
series**, plus Dry, Output gain and a destination (A or B). Drag cards to reorder (Alt-drag
copies), right-click for more, the power icon bypasses. **Chains** has ready-made setups (Brut
Crunch, Brut Scream, Germanium Punch, Amp + Cab, Bass Rig, Lo-Fi Radio, Tape Glue, Parallel
Smash, Dirt + Space, Dub Echo, Spring Tank, Big Hall, Ensemble Wash, Ladder Sweep; Master: Drum
Bus, Analog Master, Crunch Master); **Roll Chain** builds a random one (series insert, parallel
dirt + space, or space send; gentle chains for the master).

Modules (`Source/fx`): **Ferric** (vintage saturation after Felt Instruments' Brut: six preamp
characters - Glow, Lab, Dark, Meter, Germanium, Soviet - a resonant high-pass or band-pass filter
inside a feedback loop from output to input, tilt EQ last, 2x oversampled), **Amp** (1-3 triode
stages, tone stack, sagging power amp), **Cabinet** (7 cabinets, mic edge / distance), **Tape**
(saturation with bias, head bump, wow, flutter, hiss, age), **Glue** (bus compressor), **Shaper**
(transients), **Ladder** (4-pole ZDF, drive, envelope follower), **Program EQ** (passive
boost/attenuate), **Echo** (bucket-brigade, tempo sync, ping-pong), **Reverb** (8-line FDN: plate,
room, hall, ambience), **Spring**, **Chorus** (I, II, I+II, Ensemble), **Crusher**, **Utility**,
and the four original LXR-02 effects. Modules are swapped without allocating on the audio thread
(atomic hand-over, deleted later on the message thread). An empty bus passes its input through
untouched, so the LXR voices stay bit-identical. Old presets convert automatically: their FX bus
becomes FX 1 with the matching LXR module, Output "FX" becomes FX 1.

## Sound dice: morph ends, genres, cymbals (added Oct 29 2026)

- Every sound dice (channel dice, Random ▾, Alter, Sounds, All) writes the **morph end the Sound
  Morph slider is nearest to** (A below 50 %, B from 50 %); the other end keeps its sound. Roll at
  A, move the slider to B, roll again, and the slider morphs between two random sounds. A sound
  with a different voice model goes to both ends.
- **Genres**: House, Techno, Drum & Bass, Jungle, Trap, Boom Bap, Lo-Fi, Electro, UK Garage,
  Dubstep, Industrial, Dembow, Footwork. The channel's Random ▾ menu offers "Random snare in the
  style of ..." for the channel's role, and every kind of sound by genre; the Dice panel's
  **Genre** (default: follow the beat style) shapes the Sounds / All rolls and the dice button.
- **Curated cymbals**: Crash, Ride, Splash, China, 808 Cymbal, Bell - each with its own carrier /
  modulator ratios, filter and decay ranges, checked for a dense metallic spectrum (no single
  whistling partial, no low rumble).
- **Vary** (FEEL group) now also changes cymbals and hi-hats on every hit: random modulator
  phases, slight modulator detune and FM depth, so no two hits sound the same.

## Modulation, sound mutation, outputs (added Nov 9 2026)

- **MOD tab** (third tab, or Tab key): six modulators - **LFO** (6 shapes, synced to the bar or free,
  restart on a channel's hits), **Envelope** (attack, hold, decay, sustain, release, velocity;
  triggered by a channel, any hit, every beat or every bar), **Sample & Hold** (new random value
  per clock step or hit, glide, chance), **Peak Controller** (follows a channel's or output A's
  level, like FL Studio's: base, volume, tension, attack, release - use a negative depth to duck),
  **Random Drift**, **Step Lane** (16 drawn values in time) and **Macro** (host-automatable
  "Macro 1-6" parameters). Each has up to eight targets: any channel sound parameter or any FX
  module parameter, with a bipolar depth. Right-click any sound or FX knob > **Modulate with** to
  add a target; modulated knobs show their range as a blue outer arc. Modulators run once per
  64-sample control block (the LXR's own control rate); with no routes the engine is unchanged.
- **Sound mutation** (Mutate button in the sound header): each channel can move its own sound a
  little every 1-16 pattern loops (pitch, decay, tone, filter, noise, FM depth, transient,
  drive), at its own depth. Orbit returns to a home sound each time (Keep sets it, Home goes
  back); Drift lets the sound travel. Model, level and routing never change.
- **Outputs**: 12 stereo outputs - A (main), B, **Ch 1-Ch 8** and **FX 1 / FX 2**. Set a channel's
  Output to **Own** to send it to its own output; an FX bus can go to A, B, its own output, or (FX 1)
  **into FX 2** for series processing. In FL Studio: open the plugin's wrapper settings
  (Processing tab) and give each output a mixer track; an output the host does not use falls back
  to A. Sidechain from the kick by giving Ch 1 its own mixer track.
- **FX rack clarity**: a routing map (sends > bus > destination) for all three buses, a line that
  states the selected bus's signal flow in words, Insert / Parallel buttons for the Dry Thru level,
  and labels for lanes (parallel) and slots (series).
- **Ferric feedback** is now an analog-style loop: no comb delay, the loop is closed around the
  preamp at the oversampled rate through a 70 Hz coupling high-pass and a 4.2 kHz band limit, with
  rail-style soft limiting and a loop gain normalised below 1. It adds gain, grit and sustain to
  quiet parts (a -36 dBFS sine gets ~8 dB louder with 8% THD at 100%) and never howls. The drive
  itself is bit-identical at Feedback 0.
- **Cymbals**: shorter (crash ~1.6 s, ride ~1.4 s, splash ~0.85 s to -40 dB), gentler waveforms,
  low drive, and a measured level: every random cymbal is fitted to about -26 LUFS per hit, roughly
  9 dB under a typical snare.
- The dice never change a channel's Output.

## Status (handoff, Oct 28 2026)

Done and tested on Linux (Windows build not yet tried):
- **DSP port** (`Source/dsp`): drum / snare / cymbal / hi-hat voices, wavetable + FM + PWM oscillators,
  ZDF filter (7 types), slope/decay/snap envelopes, 14 transients, LFO, velocity mod, the LXR FX
  effects (tube/fold/rat distortion, ring mod, compressor, ping-pong delay; now rack modules). Verified against the original
  firmware code compiled on the host: 25 scenarios match within 0-3 LSB of 32768
  (`tests/firmware_compare/build_and_run.sh <path-to-lxr-desktop-app>`).
- **Engine / sequencer** (`Source/engine`): host-synced pattern engine, synced and immediate
  start/stop, chains, swing warp, fill rolls, choke groups, mutes, MIDI map (C1-G1 trigger,
  C2-G2 mute, C3-B3 patterns, C4 stop), pitched mode, pattern launch modes, MIDI out.
- **Randomizer**: per-sound recipes (kick, tom, perc, zap, snare, clap, hats, cymbal, chaos),
  alter, randomize all, random morph targets, rhythm-aware pattern randomizer.
- **Plugin**: 398 host parameters including Macro 1-6 (the FX rack and modulators are saved with presets), morph A/B end points, presets (.f8preset), drum patches
  (.f8drum), undo/redo (40), MIDI-file export + drag-out, WAV export, 5 factory kits.
- **UI**: all sections working, DRUMS and FX RACK tabs, zoom 60-200%. Snapshots look right.
- Tests: `tests/engine_identity.cpp` (the sequencer plays the LXR voices bit-identically when expression and feel are off; build line at the top of the file), `tests/render_test.cpp` (engine), `tests/plugin_tests.cpp` (processor + UI snapshots,
  build with `-DFERRUM_BUILD_TESTS=ON`) - all passing.

Next steps when picking back up:
1. Build on Windows with MSVC and fix any compiler differences; load in FL Studio.
2. Listen-test the factory kits and tune their levels (Concrete peaks hot before the -6 dB master).
3. Possible extras: MIDI learn, 16 program slots, preset browser previewing.

## Licences
GPL-3.0 (the LXR firmware licence). JUCE is AGPLv3 or commercial: fine for personal use; distributing
a closed-source build would need a commercial JUCE licence. Barlow Condensed font: SIL OFL.
