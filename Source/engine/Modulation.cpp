// Modulation.cpp - see Modulation.h
// SPDX-License-Identifier: GPL-3.0-only
#include "Modulation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

namespace ferrum::mod
{
const char* const kTypeNames[(int) Type::Count] = { "Off", "LFO", "Envelope", "Sample & Hold", "Peak Controller", "Random Drift", "Step Lane", "Macro" };
const char* const kDivisionNames[kNumDivisions] = { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4 dot", "1/4 trip", "1/8", "1/8 dot",
                                                    "1/8 trip", "1/16", "1/16 trip", "1/32" };
double divisionBeats (int i)
{
    static const double b[kNumDivisions] = { 16, 8, 4, 2, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[std::clamp (i, 0, kNumDivisions - 1)];
}

std::string triggerName (int t)
{
    if (t >= TrigCh1 && t < TrigCh1 + 8) return "Ch " + std::to_string (t - TrigCh1 + 1) + " hits";
    if (t == TrigAny) return "Any hit";
    if (t == TrigBar) return "Every bar";
    return "Free / clock";
}

std::string peakSourceName (int s)
{
    if (s >= PeakCh1 && s < PeakCh1 + 8) return "Channel " + std::to_string (s - PeakCh1 + 1);
    return "Output A";
}

float choiceToNorm (int index, int count) { return ((float) index + 0.5f) / (float) std::max (1, count); }
int normToChoice (float v, int count) { return std::clamp ((int) (v * (float) count), 0, count - 1); }

//== knobs =========================================================================================
enum { kLfoShape, kLfoRate, kLfoSync, kLfoPhase, kLfoPolarity, kLfoFree };
enum { kEnvAttack, kEnvHold, kEnvDecay, kEnvSustain, kEnvRelease, kEnvVel };
enum { kShRate, kShGlide, kShChance, kShPolarity };
enum { kPkBase, kPkVolume, kPkTension, kPkAttack, kPkRelease };
enum { kRndRate, kRndSmooth, kRndSync, kRndPolarity, kRndDivision };
enum { kStRate, kStLength, kStGlide, kStPolarity };

static const char* const kShapes[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" };
static const char* const kPolarity[] = { "Bipolar", "Unipolar" };
static const char* const kOnOff[] = { "Free", "Synced" };

const KnobSpec& knobSpec (Type t, int i)
{
    static const KnobSpec none {};
    static const KnobSpec lfo[kKnobs] = { { "Shape", choiceToNorm (0, 6) }, { "Rate", choiceToNorm (7, kNumDivisions) }, { "Sync", choiceToNorm (1, 2) },
                                          { "Phase", 0.f }, { "Polarity", choiceToNorm (0, 2) }, { "Free Rate", 0.45f } };
    static const KnobSpec env[kKnobs] = { { "Attack", 0.15f }, { "Hold", 0.35f }, { "Decay", 0.45f }, { "Sustain", 0.f }, { "Release", 0.4f }, { "Velocity", 0.5f } };
    static const KnobSpec sh[kKnobs] = { { "Rate", choiceToNorm (10, kNumDivisions) }, { "Glide", 0.f }, { "Chance", 1.f }, { "Polarity", choiceToNorm (0, 2) } };
    static const KnobSpec pk[kKnobs] = { { "Base", 0.f }, { "Volume", 0.25f }, { "Tension", 0.5f }, { "Attack", 0.1f }, { "Release", 0.35f } };
    static const KnobSpec rnd[kKnobs] = { { "Free Rate", 0.35f }, { "Smooth", 0.7f }, { "Sync", choiceToNorm (0, 2) }, { "Polarity", choiceToNorm (0, 2) },
                                          { "Rate", choiceToNorm (4, kNumDivisions) } };
    static const KnobSpec st[kKnobs] = { { "Rate", choiceToNorm (10, kNumDivisions) }, { "Length", 1.f }, { "Glide", 0.f }, { "Polarity", choiceToNorm (1, 2) } };
    if (i < 0 || i >= kKnobs) return none;
    switch (t)
    {
        case Type::LFO: return lfo[i];
        case Type::Envelope: return env[i];
        case Type::SampleHold: return sh[i];
        case Type::Peak: return pk[i];
        case Type::Random: return rnd[i];
        case Type::Steps: return st[i];
        default: return none;
    }
}

bool knobIsChoice (Type t, int i, std::vector<std::string>* names)
{
    auto fill = [names] (const char* const* list, int n) { if (names != nullptr) { names->clear(); for (int k = 0; k < n; ++k) names->push_back (list[k]); } return true; };
    switch (t)
    {
        case Type::LFO:
            if (i == kLfoShape) return fill (kShapes, 6);
            if (i == kLfoRate) return fill (kDivisionNames, kNumDivisions);
            if (i == kLfoSync) return fill (kOnOff, 2);
            if (i == kLfoPolarity) return fill (kPolarity, 2);
            return false;
        case Type::SampleHold:
            if (i == kShRate) return fill (kDivisionNames, kNumDivisions);
            if (i == kShPolarity) return fill (kPolarity, 2);
            return false;
        case Type::Random:
            if (i == kRndSync) return fill (kOnOff, 2);
            if (i == kRndPolarity) return fill (kPolarity, 2);
            if (i == kRndDivision) return fill (kDivisionNames, kNumDivisions);
            return false;
        case Type::Steps:
            if (i == kStRate) return fill (kDivisionNames, kNumDivisions);
            if (i == kStPolarity) return fill (kPolarity, 2);
            return false;
        default: return false;
    }
}

static float msCurve (float v, float maxMs) { return maxMs * v * v * v; }
static float lfoHz (float v) { return 0.05f * std::pow (600.f, v); }        // 0.05 .. 30 Hz
static float rndHz (float v) { return 0.05f * std::pow (400.f, v); }        // 0.05 .. 20 Hz

static std::string fmt (const char* f, double v) { char b[48]; std::snprintf (b, sizeof (b), f, v); return b; }

std::string formatKnob (Type t, int i, float v)
{
    std::vector<std::string> names;
    if (knobIsChoice (t, i, &names)) return names[(size_t) normToChoice (v, (int) names.size())];
    switch (t)
    {
        case Type::LFO:
            if (i == kLfoPhase) return fmt ("%.0f deg", v * 360.0);
            if (i == kLfoFree) return fmt (lfoHz (v) < 1 ? "%.2f Hz" : "%.1f Hz", lfoHz (v));
            break;
        case Type::Envelope:
            if (i == kEnvSustain || i == kEnvVel) return fmt ("%.0f%%", v * 100.0);
            return fmt ("%.0f ms", msCurve (v, i == kEnvAttack || i == kEnvHold ? 2000.f : 4000.f) + (i == kEnvDecay || i == kEnvRelease ? 1.f : 0.f));
        case Type::SampleHold: return fmt ("%.0f%%", v * 100.0);
        case Type::Peak:
            if (i == kPkBase) return fmt ("%.0f%%", v * 100.0);
            if (i == kPkVolume) return fmt ("%.0f%%", v * 400.0);
            if (i == kPkTension) return fmt ("%+.0f", (v - 0.5) * 200.0);
            if (i == kPkAttack) return fmt ("%.0f ms", msCurve (v, 500.f));
            return fmt ("%.0f ms", msCurve (v, 3000.f) + 5.f);
        case Type::Random:
            if (i == kRndRate) return fmt (rndHz (v) < 1 ? "%.2f Hz" : "%.1f Hz", rndHz (v));
            return fmt ("%.0f%%", v * 100.0);
        case Type::Steps:
            if (i == kStLength) return std::to_string (1 + normToChoice (v, kSteps));
            return fmt ("%.0f%%", v * 100.0);
        default: break;
    }
    return fmt ("%.0f%%", v * 100.0);
}

ModState defaultState (Type t)
{
    ModState s;
    s.type = t;
    for (int i = 0; i < kKnobs; ++i) s.p[i] = knobSpec (t, i).def;
    s.trigger = t == Type::Envelope ? TrigCh1 : TrigClock;
    s.source = PeakCh1;
    for (int i = 0; i < kSteps; ++i) s.steps[(size_t) i] = (i % 4 == 0) ? 1.f : (i % 2 == 0 ? 0.5f : 0.25f);
    return s;
}

//== matrix ==========================================================================================
void Matrix::setState (const std::array<ModState, kMods>& s)
{
    while (lock.test_and_set (std::memory_order_acquire)) std::this_thread::yield();
    pending = s;
    dirty.store (true);
    lock.clear (std::memory_order_release);
}

void Matrix::prepare (double sampleRate) { sr = sampleRate; reset(); }

void Matrix::reset()
{
    for (auto& r : rt) r = Runtime {};
    for (auto& o : out) o = 0.f;
    lastBar = -1;
}

void Matrix::beginBlock()
{
    if (! dirty.load()) return;
    if (lock.test_and_set (std::memory_order_acquire)) return; // being written: take it next block
    for (int m = 0; m < kMods; ++m)
        if (pending[(size_t) m].type != state[(size_t) m].type) rt[(size_t) m] = Runtime {};
    state = pending;
    dirty.store (false);
    lock.clear (std::memory_order_release);
    anyRoute = anyFxRoute = false;
    for (const auto& s : state)
    {
        if (s.type == Type::Off) continue;
        for (const auto& r : s.routes)
        {
            if (r.kind == Route::None || r.depth == 0.f) continue;
            anyRoute = true;
            if (r.kind == Route::Fx) anyFxRoute = true;
        }
    }
}

float Matrix::nextRandom()
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float) (seed >> 8) * (2.0f / 16777216.0f) - 1.f; // -1 .. 1
}

void Matrix::onHit (int ch, float velocity)
{
    for (int m = 0; m < kMods; ++m)
    {
        const auto& s = state[(size_t) m];
        if (s.type == Type::Off) continue;
        if (s.trigger == TrigAny || s.trigger == TrigCh1 + ch) { rt[(size_t) m].hit = true; rt[(size_t) m].hitVel = velocity; }
    }
}

static float lfoShape (int shape, double ph, float rndHeld)
{
    const float p = (float) (ph - std::floor (ph));
    switch (shape)
    {
        case 0: return std::sin (6.2831853f * p);
        case 1: return p < 0.25f ? 4.f * p : (p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f);
        case 2: return 2.f * p - 1.f;
        case 3: return 1.f - 2.f * p;
        case 4: return p < 0.5f ? 1.f : -1.f;
        default: return rndHeld;
    }
}

void Matrix::advance (int len, double ppq, double bpm, bool playing, const float* channelLevels, float mainLevel)
{
    const double dt = (double) len / sr;
    const double beatsPerBlock = dt * bpm / 60.0;
    // musical clock: the host / sequencer position while playing, a free-running clock otherwise
    double beats;
    if (playing) { beats = ppq; freeClock = ppq; }
    else { freeClock += beatsPerBlock; beats = freeClock; }
    const double bar = std::floor (beats / 4.0 + 1e-9);
    const bool barHit = bar != lastBar;
    lastBar = bar;

    for (int m = 0; m < kMods; ++m)
    {
        const auto& s = state[(size_t) m];
        auto& r = rt[(size_t) m];
        bool trig = r.hit || (s.trigger == TrigBar && barHit);
        const float vel = r.hitVel;
        r.hit = false;
        float v = 0.f;
        switch (s.type)
        {
            case Type::LFO:
            {
                const int shape = normToChoice (s.p[kLfoShape], 6);
                const bool synced = normToChoice (s.p[kLfoSync], 2) == 1;
                const double cyclesPerBeat = 1.0 / divisionBeats (normToChoice (s.p[kLfoRate], kNumDivisions));
                const double hz = lfoHz (s.p[kLfoFree]);
                const bool retrig = s.trigger != TrigClock;
                double ph;
                if (synced && ! retrig) ph = beats * cyclesPerBeat;  // locked to the bar
                else
                {
                    if (retrig && trig) r.phase = 0;
                    r.phase += synced ? beatsPerBlock * cyclesPerBeat : hz * dt;
                    ph = r.phase;
                }
                ph += s.p[kLfoPhase];
                const double slot = std::floor (ph);
                if (slot != r.lastSlot) { r.lastSlot = slot; r.held = nextRandom(); }
                v = lfoShape (shape, ph, r.held);
                if (normToChoice (s.p[kLfoPolarity], 2) == 1) v = 0.5f * (v + 1.f);
                break;
            }
            case Type::Envelope:
            {
                if (s.trigger == TrigClock && std::floor (beats) != std::floor (beats - beatsPerBlock)) trig = true; // every beat
                if (trig) { r.stage = 1; r.gate = msCurve (s.p[kEnvHold], 2000.f) / 1000.0; r.vel = s.trigger == TrigClock || s.trigger == TrigBar ? 1.f : vel; }
                const float att = msCurve (s.p[kEnvAttack], 2000.f) / 1000.f, dec = (msCurve (s.p[kEnvDecay], 4000.f) + 1.f) / 1000.f,
                            rel = (msCurve (s.p[kEnvRelease], 4000.f) + 1.f) / 1000.f, sus = s.p[kEnvSustain];
                r.gate -= dt;
                if (r.stage == 1)
                {
                    r.env += att <= 0.0005f ? 1.f : (float) (dt / att);
                    if (r.env >= 1.f) { r.env = 1.f; r.stage = 2; }
                }
                else if (r.stage == 2) r.env = sus + (r.env - sus) * std::exp ((float) (-dt / (dec * 0.25f)));
                if ((r.stage == 1 || r.stage == 2) && r.gate <= 0) r.stage = 3;
                if (r.stage == 3)
                {
                    r.env *= std::exp ((float) (-dt / (rel * 0.25f)));
                    if (r.env < 1.0e-4f) { r.env = 0; r.stage = 0; }
                }
                const float va = s.p[kEnvVel];
                v = r.env * (1.f - va + va * r.vel);
                break;
            }
            case Type::SampleHold:
            {
                bool take = false;
                if (s.trigger == TrigClock)
                {
                    const double slot = std::floor (beats / divisionBeats (normToChoice (s.p[kShRate], kNumDivisions)) + 1e-9);
                    if (slot != r.lastSlot) { r.lastSlot = slot; take = true; }
                }
                else take = trig;
                if (take && (nextRandom() * 0.5f + 0.5f) <= s.p[kShChance]) r.heldTarget = nextRandom();
                const float glide = s.p[kShGlide];
                if (glide <= 0.001f) r.held = r.heldTarget;
                else r.held += (r.heldTarget - r.held) * (1.f - std::exp ((float) (-dt / (glide * glide * 0.6))));
                v = r.held;
                if (normToChoice (s.p[kShPolarity], 2) == 1) v = 0.5f * (v + 1.f);
                break;
            }
            case Type::Peak:
            {
                const float in = (s.source >= PeakCh1 && s.source < PeakCh1 + 8 && channelLevels != nullptr) ? channelLevels[s.source - PeakCh1] : mainLevel;
                const float tau = in > r.peak ? std::max (0.0005f, msCurve (s.p[kPkAttack], 500.f) / 1000.f)
                                              : (msCurve (s.p[kPkRelease], 3000.f) + 5.f) / 1000.f;
                r.peak += (in - r.peak) * (1.f - std::exp ((float) (-dt / tau)));
                float x = std::clamp (r.peak * s.p[kPkVolume] * 4.f, 0.f, 1.f);
                const float tension = (s.p[kPkTension] - 0.5f) * 2.f;
                if (tension > 0) x = std::pow (x, 1.f + 3.f * tension);
                else if (tension < 0) x = 1.f - std::pow (1.f - x, 1.f - 3.f * tension);
                v = std::clamp (s.p[kPkBase] + x * (1.f - s.p[kPkBase]), 0.f, 1.f);
                break;
            }
            case Type::Random:
            {
                const bool synced = normToChoice (s.p[kRndSync], 2) == 1;
                if (s.trigger != TrigClock && trig) { r.rndA = r.rndB; r.rndB = nextRandom(); r.phase = 0; }
                r.phase += synced ? beatsPerBlock / divisionBeats (normToChoice (s.p[kRndDivision], kNumDivisions)) : rndHz (s.p[kRndRate]) * dt;
                while (r.phase >= 1.0) { r.phase -= 1.0; r.rndA = r.rndB; r.rndB = nextRandom(); }
                const float f = (float) r.phase, sm = f * f * (3.f - 2.f * f);
                const float smooth = s.p[kRndSmooth];
                v = smooth * (r.rndA + (r.rndB - r.rndA) * sm) + (1.f - smooth) * r.rndB;
                if (normToChoice (s.p[kRndPolarity], 2) == 1) v = 0.5f * (v + 1.f);
                break;
            }
            case Type::Steps:
            {
                const int steps = 1 + normToChoice (s.p[kStLength], kSteps);
                const double pos = beats / divisionBeats (normToChoice (s.p[kStRate], kNumDivisions));
                const int idx = (int) (((long long) std::floor (pos + 1e-9) % steps + steps) % steps);
                const float target = s.steps[(size_t) idx];
                const float glide = s.p[kStGlide];
                if (glide <= 0.001f) r.held = target;
                else r.held += (target - r.held) * (1.f - std::exp ((float) (-dt / (glide * glide * 0.4))));
                v = r.held;
                if (normToChoice (s.p[kStPolarity], 2) == 0) v = v * 2.f - 1.f;
                break;
            }
            case Type::Macro: v = macros[(size_t) m].load(); break;
            case Type::Off: default: v = 0.f; break;
        }
        out[(size_t) m] = std::clamp (v, -1.f, 1.f);
        uiValue[(size_t) m].store (out[(size_t) m]);
    }
}
} // namespace ferrum::mod
