// FxChains.cpp - see FxChains.h
// SPDX-License-Identifier: GPL-3.0-only
#include "FxChains.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace ferrum::fx
{
void setSlot (RackState::Slot& s, Type t, std::initializer_list<Setting> settings)
{
    s = {};
    s.type = t;
    const auto& in = info (t);
    for (size_t i = 0; i < in.params.size() && i < (size_t) kMaxParams; ++i) s.p[i] = in.params[i].def;
    for (const auto& st : settings)
    {
        const int idx = paramIndex (t, st.param);
        if (idx < 0) continue;
        const auto& ps = in.params[(size_t) idx];
        if (st.choice != nullptr)
        {
            for (size_t k = 0; k < ps.choices.size(); ++k)
                if (std::string (ps.choices[k]) == st.choice) s.p[idx] = choiceNorm (ps, (int) k);
        }
        else s.p[idx] = normFromValue (ps, st.value);
    }
}

static RackState::Slot& at (RackState::Bus& b, int lane, int slot) { return b.lanes[(size_t) lane].slots[(size_t) slot]; }
static float dbToLin (float db) { return std::pow (10.f, db / 20.f); }

const std::vector<Chain>& chains()
{
    static const std::vector<Chain> c = {
        { "Brut Crunch", "Ferric on the Lab preamp with the resonant high-pass and a little feedback: crunchy, forward drums.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Ferric, { { "Preamp", 0, "Lab" }, { "Drive", 18 }, { "Filter", 0, "Resonant HP" }, { "Cutoff", 90 },
                                                      { "Resonance", 35 }, { "Feedback", 20 }, { "Tilt", 1.5f }, { "Mix", 100 } });
          } },
        { "Brut Scream", "Ferric on the Soviet preamp with heavy feedback into a resonant bandpass: sputtering, sustaining, nasal fuzz.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Ferric, { { "Preamp", 0, "Soviet" }, { "Drive", 26 }, { "Filter", 0, "Bandpass" }, { "Cutoff", 1400 },
                                                      { "Resonance", 55 }, { "Feedback", 85 }, { "Tilt", -2 }, { "Mix", 70 } });
          } },
        { "Germanium Punch", "Ferric's germanium preamp: sag and punch, darker tilt. Thickens kicks and toms.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Ferric, { { "Preamp", 0, "Germanium" }, { "Drive", 14 }, { "Tilt", -1.5f }, { "Mix", 100 } });
          } },
        { "Amp + Cab", "A crunchy tube amp into a 2x12 combo. Great on snares, claps and percussion.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Amp, { { "Model", 0, "Crunch" }, { "Gain", 55 }, { "Mid", 2 }, { "Sag", 35 } });
              setSlot (at (b, 0, 1), Type::Cabinet, { { "Cabinet", 0, "2x12 Combo" }, { "Mic Edge", 35 }, { "Distance", 20 } });
          } },
        { "Bass Rig", "Bass amp into a 4x10 cabinet: weight and growl for 808s and low toms.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Amp, { { "Model", 0, "Bass" }, { "Gain", 40 }, { "Bass", 3 }, { "Treble", -2 } });
              setSlot (at (b, 0, 1), Type::Cabinet, { { "Cabinet", 0, "4x10 Bass" }, { "Mic Edge", 20 } });
          } },
        { "Lo-Fi Radio", "Bit crusher into a small radio speaker and a worn 7.5 ips tape.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Crusher, { { "Bits", 9 }, { "Rate", 16000 }, { "Jitter", 15 } });
              setSlot (at (b, 0, 1), Type::Cabinet, { { "Cabinet", 0, "Small Radio" } });
              setSlot (at (b, 0, 2), Type::Tape, { { "Speed", 0, "7.5 ips" }, { "Age", 60 }, { "Wow", 30 }, { "Hiss", 15 } });
          } },
        { "Tape Glue", "Tape saturation into the bus compressor.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Tape, { { "Drive", 8 }, { "Speed", 0, "15 ips" }, { "Wow", 8 }, { "Flutter", 8 }, { "Hiss", 4 } });
              setSlot (at (b, 0, 1), Type::Glue, { { "Threshold", -18 }, { "Ratio", 0, "4:1" }, { "Attack", 0, "10 ms" }, { "Release", 0, "Auto" },
                                                   { "Makeup", 4 } });
          } },
        { "Parallel Smash", "New York compression: the dry kit plus a squashed, saturated copy underneath.", false,
          [] (RackState::Bus& b)
          {
              b.dry = 1.f;
              setSlot (at (b, 0, 0), Type::Glue, { { "Threshold", -32 }, { "Ratio", 0, "10:1" }, { "Attack", 0, "0.3 ms" }, { "Release", 0, "0.1 s" },
                                                   { "Makeup", 14 }, { "SC HPF", 0, "Off" } });
              setSlot (at (b, 0, 1), Type::Ferric, { { "Preamp", 0, "Germanium" }, { "Drive", 8 } });
              b.lanes[0].level = dbToLin (-6.f);
          } },
        { "Dirt + Space", "Three lanes in parallel: dry, a driven lane and a plate reverb lane.", false,
          [] (RackState::Bus& b)
          {
              b.dry = 1.f;
              setSlot (at (b, 0, 0), Type::Ferric, { { "Preamp", 0, "Dark" }, { "Drive", 20 }, { "Filter", 0, "Resonant HP" }, { "Cutoff", 200 } });
              b.lanes[0].level = dbToLin (-8.f);
              setSlot (at (b, 1, 0), Type::Reverb, { { "Type", 0, "Plate" }, { "Decay", 1.6f }, { "Pre-delay", 15 }, { "Mix", 100 } });
              b.lanes[1].level = dbToLin (-12.f);
          } },
        { "Dub Echo", "A dark, saturated ping-pong echo feeding a spring tank, on a parallel lane.", false,
          [] (RackState::Bus& b)
          {
              b.dry = 1.f;
              setSlot (at (b, 0, 0), Type::Echo, { { "Sync", 0, "1/8D" }, { "Feedback", 62 }, { "Tone", 3200 }, { "Mod", 30 }, { "Age", 40 },
                                                   { "Ping-Pong", 0, "On" }, { "Mix", 100 } });
              setSlot (at (b, 0, 1), Type::Spring, { { "Decay", 2 }, { "Mix", 45 } });
              b.lanes[0].level = dbToLin (-3.f);
          } },
        { "Spring Tank", "Spring reverb on a parallel lane: drip and boing for snares and percussion.", false,
          [] (RackState::Bus& b)
          {
              b.dry = 1.f;
              setSlot (at (b, 0, 0), Type::Spring, { { "Springs", 0, "2" }, { "Decay", 2.5f }, { "Drip", 60 }, { "Mix", 100 } });
              b.lanes[0].level = dbToLin (-6.f);
          } },
        { "Big Hall", "A long hall on a parallel lane, with pre-delay to keep the transients clear.", false,
          [] (RackState::Bus& b)
          {
              b.dry = 1.f;
              setSlot (at (b, 0, 0), Type::Utility, { { "Low Cut", 180 } });
              setSlot (at (b, 0, 1), Type::Reverb, { { "Type", 0, "Hall" }, { "Size", 80 }, { "Decay", 3.5f }, { "Pre-delay", 25 }, { "Mix", 100 } });
              b.lanes[0].level = dbToLin (-9.f);
          } },
        { "Ensemble Wash", "Three-voice ensemble chorus into a plate: wide, shimmering hats and cymbals.", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Chorus, { { "Mode", 0, "Ensemble" }, { "Depth", 60 }, { "Mix", 50 } });
              setSlot (at (b, 0, 1), Type::Reverb, { { "Type", 0, "Plate" }, { "Decay", 2.2f }, { "Mix", 28 } });
          } },
        { "Ladder Sweep", "A resonant 24 dB ladder that opens with every hit (envelope follower).", false,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Ladder, { { "Mode", 0, "LP 24" }, { "Cutoff", 700 }, { "Resonance", 55 }, { "Drive", 8 },
                                                     { "Env Amount", 70 }, { "Env Speed", 0, "Medium" } });
          } },
        { "Drum Bus", "Transient shaper, glue compressor, program EQ and 30 ips tape: the classic drum bus.", true,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Shaper, { { "Attack", 25 }, { "Sustain", -10 } });
              setSlot (at (b, 0, 1), Type::Glue, { { "Threshold", -14 }, { "Ratio", 0, "4:1" }, { "Attack", 0, "30 ms" }, { "Release", 0, "Auto" },
                                                   { "Makeup", 3 }, { "SC HPF", 0, "90 Hz" } });
              setSlot (at (b, 0, 2), Type::Eq, { { "Low Freq", 0, "60 Hz" }, { "Low Boost", 4 }, { "Low Atten", 2 }, { "High Freq", 0, "10 kHz" },
                                                 { "High Boost", 3 } });
              setSlot (at (b, 0, 3), Type::Tape, { { "Drive", 4 }, { "Speed", 0, "30 ips" }, { "Wow", 4 }, { "Flutter", 4 }, { "Hiss", 2 }, { "Age", 10 } });
          } },
        { "Analog Master", "15 ips tape and a gentle 2:1 glue on the whole mix.", true,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Tape, { { "Drive", 6 }, { "Speed", 0, "15 ips" }, { "Wow", 6 }, { "Flutter", 6 }, { "Hiss", 3 } });
              setSlot (at (b, 0, 1), Type::Glue, { { "Threshold", -10 }, { "Ratio", 0, "2:1" }, { "Attack", 0, "30 ms" }, { "Release", 0, "Auto" },
                                                   { "Makeup", 2 } });
          } },
        { "Crunch Master", "Ferric's Glow preamp blended in, with the lows kept mono.", true,
          [] (RackState::Bus& b)
          {
              setSlot (at (b, 0, 0), Type::Ferric, { { "Preamp", 0, "Glow" }, { "Drive", 10 }, { "Mix", 60 } });
              setSlot (at (b, 0, 1), Type::Utility, { { "Mono Below", 120 } });
          } },
    };
    return c;
}

//==============================================================================================
namespace
{
struct Dice
{
    explicit Dice (uint32_t seed) : rng (seed) {}
    float uni (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng); }
    int pick (int n) { return std::uniform_int_distribution<int> (0, n - 1) (rng); }
    bool chance (float p) { return uni (0.f, 1.f) < p; }
    template <typename T> T from (std::initializer_list<T> l) { return *(l.begin() + pick ((int) l.size())); }
    std::mt19937 rng;
};

bool isSpace (Type t) { return t == Type::Echo || t == Type::Reverb || t == Type::Spring || t == Type::LxrDelay || t == Type::Chorus; }

// random values inside each parameter's musical range
void roll (Dice& d, RackState::Slot& s, Type t, bool wetLane)
{
    setSlot (s, t);
    const auto& in = info (t);
    for (size_t i = 0; i < in.params.size(); ++i)
    {
        const auto& ps = in.params[i];
        const std::string n = ps.name;
        float& v = s.p[i];
        if (ps.scale == Scale::Choice)
        {
            if (n == "Ping-Pong" || n == "Polarity" || n == "SC HPF") continue;
            if (n == "Sync") { v = choiceNorm (ps, d.from ({ 3, 5, 6, 7, 8 })); continue; }
            v = choiceNorm (ps, d.pick ((int) ps.choices.size()));
            continue;
        }
        if (n == "Master" || n == "Output" || n == "Makeup" || n == "Gain" || n == "Volume" || n == "Pan" || n == "Width") continue;
        if (n == "Mix") { v = wetLane ? 1.f : (isSpace (t) ? d.uni (0.15f, 0.4f) : d.uni (0.55f, 1.f)); continue; }
        if (n == "Feedback") { v = t == Type::Echo ? d.uni (0.2f, 0.6f) : d.uni (0.f, t == Type::Ferric ? 0.55f : 0.6f); continue; }
        if (n == "Hiss") { v = d.uni (0.f, 0.15f); continue; }
        if (n == "Resonance") { v = d.uni (0.05f, 0.65f); continue; }
        if (n == "Decay" && t == Type::Reverb) { v = d.uni (0.25f, 0.65f); continue; }
        if (n == "Bits") { v = d.uni (0.25f, 0.65f); continue; }
        if (n == "Rate" && t == Type::Crusher) { v = d.uni (0.55f, 1.f); continue; }
        if (n == "Low Cut" || n == "High Cut" || n == "Mono Below") continue;
        if (n == "Threshold") { v = d.uni (0.35f, 0.75f); continue; }
        if (n == "Drive") { v = d.uni (0.2f, 0.75f); continue; }
        v = d.uni (0.12f, 0.88f);
    }
}
} // namespace

RackState::Bus randomChain (uint32_t seed, bool master, int kind)
{
    Dice d (seed);
    RackState::Bus b;
    if (master)
    {
        // gentle: optional shaper, then glue and/or tape, optional EQ
        int s = 0;
        if (d.chance (0.4f)) setSlot (at (b, 0, s++), Type::Shaper, { { "Attack", d.uni (5, 35) }, { "Sustain", d.uni (-20, 10) } });
        setSlot (at (b, 0, s++), Type::Glue, { { "Threshold", d.uni (-20, -8) }, { "Ratio", 0, d.chance (0.6f) ? "2:1" : "4:1" },
                                               { "Attack", 0, d.from ({ "3 ms", "10 ms", "30 ms" }) }, { "Release", 0, "Auto" }, { "Makeup", d.uni (1, 4) },
                                               { "SC HPF", 0, "90 Hz" } });
        if (d.chance (0.5f))
            setSlot (at (b, 0, s++), Type::Eq, { { "Low Freq", 0, d.from ({ "30 Hz", "60 Hz", "100 Hz" }) }, { "Low Boost", d.uni (0, 5) },
                                                 { "Low Atten", d.uni (0, 3) }, { "High Freq", 0, d.from ({ "8 kHz", "10 kHz", "12 kHz" }) }, { "High Boost", d.uni (0, 4) } });
        if (d.chance (0.7f))
            setSlot (at (b, 0, s++), Type::Tape, { { "Drive", d.uni (2, 9) }, { "Speed", 0, d.from ({ "15 ips", "30 ips" }) }, { "Wow", d.uni (0, 10) },
                                                   { "Flutter", d.uni (0, 10) }, { "Hiss", d.uni (0, 4) }, { "Age", d.uni (0, 25) } });
        else
            setSlot (at (b, 0, s++), Type::Ferric, { { "Preamp", 0, d.from ({ "Glow", "Meter", "Lab" }) }, { "Drive", d.uni (4, 12) }, { "Mix", d.uni (40, 80) } });
        return b;
    }

    if (kind < 0) kind = d.pick (3);
    const Type drive = d.from ({ Type::Ferric, Type::Ferric, Type::Amp, Type::Tape, Type::Crusher, Type::LxrDist });
    const Type tone = d.from ({ Type::Ladder, Type::Eq, Type::Cabinet });
    const Type space = d.from ({ Type::Echo, Type::Reverb, Type::Spring, Type::Chorus, Type::LxrDelay });
    switch (kind)
    {
        case 0: // series insert: drive -> (cab / tone) -> (dynamics) -> (light space)
        {
            int s = 0;
            roll (d, at (b, 0, s++), drive, false);
            if (drive == Type::Amp) roll (d, at (b, 0, s++), Type::Cabinet, false);
            else if (d.chance (0.6f)) roll (d, at (b, 0, s++), tone, false);
            if (d.chance (0.45f)) roll (d, at (b, 0, s++), d.from ({ Type::Glue, Type::Shaper }), false);
            if (d.chance (0.35f)) roll (d, at (b, 0, s++), space, false);
            break;
        }
        case 1: // parallel: dry + dirty lane + space lane
        {
            b.dry = 1.f;
            roll (d, at (b, 0, 0), drive, true);
            if (drive == Type::Amp) roll (d, at (b, 0, 1), Type::Cabinet, true);
            else if (d.chance (0.5f)) roll (d, at (b, 0, 1), tone, true);
            b.lanes[0].level = dbToLin (d.uni (-12.f, -4.f));
            roll (d, at (b, 1, 0), space, true);
            b.lanes[1].level = dbToLin (d.uni (-14.f, -6.f));
            break;
        }
        default: // space send: (tone) -> space, wet on a lane under the dry signal
        {
            b.dry = 1.f;
            int s = 0;
            if (d.chance (0.5f)) roll (d, at (b, 0, s++), d.from ({ Type::Ladder, Type::Utility, Type::Chorus }), true);
            roll (d, at (b, 0, s++), space, true);
            if (d.chance (0.4f)) roll (d, at (b, 0, s++), d.from ({ Type::Reverb, Type::Spring }), true);
            b.lanes[0].level = dbToLin (d.uni (-11.f, -4.f));
            break;
        }
    }
    return b;
}
} // namespace ferrum::fx
