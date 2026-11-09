// Modulation.h - six modulators (LFO, envelope, sample and hold, peak controller, random drift, step
// lane, macro), each with up to eight routes to channel sound parameters or FX rack parameters.
//
// Threading: the message thread edits a plain ModState copy and hands it over with setState(); the
// audio thread picks it up with a try-lock at the start of a block (it never waits). Modulators run
// once per 64-sample control block, the same rate the LXR voices use for their own envelopes.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../dsp/PatchParams.h"
#include "../fx/FxModule.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ferrum::mod
{
constexpr int kMods = 6, kRoutes = 8, kSteps = 16, kKnobs = 6;
constexpr int kBlock = 64;                       // samples per modulation update

enum class Type : int { Off = 0, LFO, Envelope, SampleHold, Peak, Random, Steps, Macro, Count };
extern const char* const kTypeNames[(int) Type::Count];

// what starts an envelope, a sample-and-hold step or an LFO restart
enum Trigger : int { TrigClock = 0, TrigCh1 = 1, /* ... TrigCh8 = 8 */ TrigAny = 9, TrigBar = 10, TrigCount = 11 };
std::string triggerName (int t);
// what a peak controller listens to
enum PeakSource : int { PeakCh1 = 0, /* ... PeakCh8 = 7 */ PeakMain = 8, PeakCount = 9 };
std::string peakSourceName (int s);

struct Route
{
    enum Kind : int { None = 0, Channel, Fx } kind = None;
    int ch = 0, param = 0;                       // Channel: channel 0-7 and patch parameter
    int bus = 0, lane = 0, slot = 0, idx = 0;    // Fx: rack slot and module parameter
    float depth = 0.f;                           // -1..1, in the target's 0..1 range
    bool sameTarget (const Route& o) const
    {
        return kind == o.kind && (kind == Channel ? (ch == o.ch && param == o.param)
                                                  : (bus == o.bus && lane == o.lane && slot == o.slot && idx == o.idx));
    }
};

struct ModState
{
    Type type = Type::Off;
    float p[kKnobs] {};                          // the type's knobs, normalised 0..1 (see knobSpec)
    int trigger = TrigClock;
    int source = PeakCh1;
    std::array<float, kSteps> steps {};
    std::array<Route, kRoutes> routes {};
    int routeCount() const { int n = 0; for (auto& r : routes) n += r.kind != Route::None; return n; }
};

// knobs per type: name, default, and how the 0..1 value is shown
struct KnobSpec { const char* name = nullptr; float def = 0.f; };
const KnobSpec& knobSpec (Type t, int i);       // name == nullptr: the type has no such knob
std::string formatKnob (Type t, int i, float v);
ModState defaultState (Type t);                  // a type with its default knobs (routes empty)
bool knobIsChoice (Type t, int i, std::vector<std::string>* names = nullptr);
float choiceToNorm (int index, int count);
int normToChoice (float v, int count);

// synced rates (beats per cycle)
constexpr int kNumDivisions = 13;
extern const char* const kDivisionNames[kNumDivisions];
double divisionBeats (int i);

class Matrix
{
public:
    // message thread
    void setState (const std::array<ModState, kMods>& s);
    void setMacro (int i, float v) { macros[(size_t) i].store (v); }

    // audio thread
    void prepare (double sampleRate);
    void reset();
    void beginBlock();                           // adopts a new state if one is waiting
    bool active() const { return anyRoute; }
    bool fxActive() const { return anyFxRoute; }
    // one control block: ppq = musical position at its start, bpm, playing = the transport runs
    void advance (int len, double ppq, double bpm, bool playing, const float* channelLevels, float mainLevel);
    void onHit (int ch, float velocity);         // a channel was triggered
    float value (int m) const { return out[(size_t) m]; }
    const std::array<ModState, kMods>& live() const { return state; }

    // UI
    std::array<std::atomic<float>, kMods> uiValue {};

private:
    struct Runtime
    {
        double phase = 0;                        // LFO / random phase
        float env = 0; int stage = 0; double gate = 0; // envelope: 0 idle, 1 attack, 2 decay/sustain, 3 release
        float vel = 1;
        float held = 0, heldTarget = 0;          // sample and hold
        float rndA = 0, rndB = 0;                // random drift: segment start / end
        float peak = 0;
        bool hit = false;
        float hitVel = 1;
        int lastStep = -1;
        double lastSlot = -1;
    };
    float nextRandom();
    std::array<ModState, kMods> state {}, pending {};
    std::array<Runtime, kMods> rt {};
    std::array<float, kMods> out {};
    std::array<std::atomic<float>, kMods> macros {};
    std::atomic_flag lock = ATOMIC_FLAG_INIT;   // the audio thread only ever try-locks
    std::atomic<bool> dirty { false };
    bool anyRoute = false, anyFxRoute = false;
    double sr = 44100.0, freeClock = 0;
    uint32_t seed = 0x9e3779b9u;
    double lastBar = -1;
};
} // namespace ferrum::mod
