// PatchParams.h - the parameters of one drum channel ("drum patch").
// All values are stored normalised 0..1 so they can be morphed, randomised and modulated the same
// way the LXR firmware modulates its MIDI-CC parameters (value + amount * (mod * 2 - 1)).
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ferrum
{
enum class Model : int { Drum = 0, Snare, Cymbal, HiHat, Count };

enum PatchParam : int
{
    P_MODEL,
    P_OSC_WAVE, P_OSC_COARSE, P_OSC_FINE, P_PWM,
    P_MOD_WAVE, P_MOD_COARSE, P_MOD_GAIN, P_MOD2_WAVE, P_MOD2_COARSE, P_MOD2_GAIN, P_FM_MODE,
    P_NOISE_FREQ, P_NOISE_MIX,
    P_PITCH_AMT, P_PITCH_DECAY, P_PITCH_SLOPE,
    P_ATTACK, P_DECAY, P_SLOPE, P_REPEAT,
    P_FILTER_TYPE, P_FILTER_FREQ, P_FILTER_RESO, P_FILTER_DRIVE,
    P_TRANS_WAVE, P_TRANS_VOL, P_TRANS_FREQ,
    P_DRIVE, P_DECIMATE, P_LEVEL, P_PAN, P_OUTPUT, P_CHOKE,
    P_VEL_VOL, P_VEL_AMT, P_VEL_DEST,
    P_LFO_WAVE, P_LFO_FREQ, P_LFO_SYNC, P_LFO_RETRIG, P_LFO_PHASE, P_LFO_AMT, P_LFO_DEST,
    P_GHOST, P_HUMANIZE, P_VARIANCE,   // "feel": how ghost notes, timing and per-hit variation behave
    NUM_PATCH_PARAMS
};

struct ParamDef
{
    const char* id;        // stable id fragment used for host parameter ids and files
    const char* name;      // full name
    const char* label;     // short label under a knob
    int numChoices;        // 0 = continuous
    const char* const* choices;
    float defaultValue;    // normalised
    bool morphable;        // false: identical on both morph end points (model, routing ...)
};

const ParamDef& paramDef (int p);
std::string formatValue (int p, float v);           // human readable value (Hz, ms, dB, note...)
bool parseValue (int p, const std::string& text, float& outNorm); // inverse of formatValue where sensible
bool isUsedByModel (int p, Model m);                 // whether the knob does anything for the model
inline bool isFeelParam (int p) { return p == P_GHOST || p == P_HUMANIZE || p == P_VARIANCE; } // kept by randomize

inline int choiceIndex (float v, int count)
{
    int i = (int) (v * (float) count);
    return i < 0 ? 0 : (i >= count ? count - 1 : i);
}
inline float choiceValue (int index, int count) { return ((float) index + 0.5f) / (float) count; }

// modulation destinations shared by the LFO and velocity (index 0 = off)
const std::vector<int>& modTargets();
int modTargetCount();

// displayed names of choice lists
extern const char* const kModelNames[4];
extern const char* const kWaveNames[6];
extern const char* const kFilterNames[8];
extern const char* const kTransientNames[14];
extern const char* const kLfoWaveNames[8];
extern const char* const kSyncNames[12];

using Patch = std::array<float, NUM_PATCH_PARAMS>;

// how one hit is played (from the sequencer's per-step expression lanes); read by Voice::trigger()
struct Articulation
{
    int8_t ghost = 0;   // 0 full .. 3 whisper
    int8_t decay = 0;   // -8 .. +8
    int8_t tone = 0;    // -8 .. +8
    int8_t pitch = 0;   // semitones
};
Patch defaultPatch (Model m); // an initialised patch for a model (based on the LXR init kit)
} // namespace ferrum
