// Randomizer.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "Randomizer.h"
#include "../dsp/LxrCore.h"
#include "../dsp/LxrVoice.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace ferrum
{
const char* const kFlavorNames[(int) Flavor::Count] = { "Kick", "Tom", "Perc", "Zap", "Snare", "Clap",
                                                        "Closed Hat", "Open Hat", "Cymbal", "Chaos" };
const char* const kBeatStyleNames[(int) BeatStyle::Count] = { "Auto", "House / Techno", "Breakbeat", "Drum & Bass", "Trap",
                                                              "Dembow", "Electro", "Footwork", "Euclidean", "Glitch" };
const char* const kKitCharacterNames[(int) KitCharacter::Count] = { "Auto", "Clean", "Analog", "Gritty", "Lo-Fi", "Metallic" };
const char* const kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kGenreNames[(int) Genre::Count] = { "Any", "House", "Techno", "Drum & Bass", "Jungle", "Trap", "Boom Bap", "Lo-Fi",
                                                      "Electro", "UK Garage", "Dubstep", "Industrial", "Dembow", "Footwork" };
const char* const kCymbalTypeNames[(int) CymbalType::Count] = { "Crash", "Ride", "Splash", "China", "808 Cymbal", "Bell" };

Randomizer::Randomizer()
    : rng ((uint32_t) std::chrono::high_resolution_clock::now().time_since_epoch().count()) {}

namespace
{
struct Builder
{
    Patch& p;
    Randomizer& r;
    void set (int id, float v127) { p[(size_t) id] = lxr::clamp01 (v127 / 127.f); }
    void rnd (int id, float lo, float hi) { set (id, r.uni (lo, hi)); }
    void ch (int id, int idx) { p[(size_t) id] = choiceValue (idx, paramDef (id).numChoices); }
    template <size_t N> void pick (int id, const int (&opts)[N]) { ch (id, opts[r.irange (0, (int) N - 1)]); }
    void norm (int id, float lo, float hi) { p[(size_t) id] = r.uni (lo, hi); }
};
}

Patch Randomizer::patch (Flavor f, const Patch& current, Genre g)
{
    if (f == Flavor::Cymbal) return cymbal (cymbalTypeFor (g), current, g);
    Patch p = defaultPatch (Model::Drum);
    Builder b { p, *this };
    using namespace lxr;

    auto commonTransient = [&] (std::initializer_list<int> samples, float pSnap, float pSample)
    {
        const float x = uni (0.f, 1.f);
        if (x < pSnap) { b.ch (P_TRANS_WAVE, 0); b.norm (P_TRANS_VOL, 0.1f, 0.6f); b.rnd (P_TRANS_FREQ, 20, 90); }
        else if (x < pSnap + pSample)
        {
            std::vector<int> v (samples);
            b.ch (P_TRANS_WAVE, v[(size_t) irange (0, (int) v.size() - 1)]);
            b.norm (P_TRANS_VOL, 0.2f, 0.7f); b.rnd (P_TRANS_FREQ, 15, 60);
        }
        else { b.ch (P_TRANS_WAVE, 0); b.set (P_TRANS_VOL, 0); }
    };
    auto commonOut = [&] (float driveLo, float driveHi, float panSpread)
    {
        b.rnd (P_DRIVE, driveLo, driveHi);
        b.set (P_DECIMATE, chance (0.08f) ? uni (55, 120) : 127);
        b.norm (P_LEVEL, 0.78f, 0.95f);
        b.norm (P_PAN, 0.5f - panSpread, 0.5f + panSpread);
        b.norm (P_VEL_VOL, 0.3f, 0.6f);
    };

    switch (f)
    {
        case Flavor::Kick:
        {
            b.ch (P_MODEL, (int) Model::Drum);
            b.ch (P_OSC_WAVE, chance (0.7f) ? WAVE_SINE : WAVE_TRI);
            b.rnd (P_OSC_COARSE, 22, 38); b.rnd (P_OSC_FINE, 50, 78);
            if (chance (0.25f)) { b.ch (P_MOD_WAVE, chance (0.5f) ? WAVE_SINE : WAVE_TRI); b.rnd (P_MOD_COARSE, 40, 80); b.norm (P_MOD_GAIN, 0.05f, 0.35f); }
            else b.set (P_MOD_GAIN, 0);
            b.ch (P_FM_MODE, 0);
            b.rnd (P_PITCH_AMT, 30, 75); b.rnd (P_PITCH_DECAY, 8, 40); b.rnd (P_PITCH_SLOPE, 0, 60);
            b.set (P_ATTACK, chance (0.9f) ? 0 : uni (0, 6)); b.rnd (P_DECAY, 12, 45); b.rnd (P_SLOPE, 20, 90);
            if (chance (0.8f)) { b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 100, 127); b.rnd (P_FILTER_RESO, 0, 20); }
            else { b.ch (P_FILTER_TYPE, 6); b.rnd (P_FILTER_FREQ, 70, 110); b.rnd (P_FILTER_RESO, 20, 60); }
            b.rnd (P_FILTER_DRIVE, 0, 40);
            commonTransient ({ 2, 3, 4, 5, 10 }, 0.4f, 0.3f);
            commonOut (45, 95, 0.0f);
            break;
        }
        case Flavor::Tom:
        {
            b.ch (P_MODEL, (int) Model::Drum);
            b.ch (P_OSC_WAVE, chance (0.6f) ? WAVE_SINE : WAVE_TRI);
            b.rnd (P_OSC_COARSE, 38, 62); b.rnd (P_OSC_FINE, 40, 88);
            if (chance (0.2f)) { b.rnd (P_MOD_COARSE, 50, 90); b.norm (P_MOD_GAIN, 0.05f, 0.25f); } else b.set (P_MOD_GAIN, 0);
            b.rnd (P_PITCH_AMT, 12, 45); b.rnd (P_PITCH_DECAY, 15, 60); b.rnd (P_PITCH_SLOPE, 10, 50);
            b.set (P_ATTACK, 0); b.rnd (P_DECAY, 14, 34); b.rnd (P_SLOPE, 20, 70);
            b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 90, 127); b.rnd (P_FILTER_RESO, 0, 30);
            commonTransient ({ 12, 2, 6 }, 0.2f, 0.3f);
            commonOut (40, 85, 0.3f);
            break;
        }
        case Flavor::Perc:
        {
            b.ch (P_MODEL, (int) Model::Drum);
            static const int waves[] = { WAVE_SINE, WAVE_TRI, WAVE_SAW, WAVE_REC, WAVE_PWM };
            b.pick (P_OSC_WAVE, waves); b.norm (P_PWM, 0.1f, 0.9f);
            b.rnd (P_OSC_COARSE, 55, 95); b.rnd (P_OSC_FINE, 0, 127);
            if (chance (0.6f))
            {
                b.pick (P_MOD_WAVE, waves); b.rnd (P_MOD_COARSE, 40, 115); b.norm (P_MOD_GAIN, 0.2f, 0.8f);
                b.ch (P_FM_MODE, chance (0.75f) ? 0 : 1);
            }
            else b.set (P_MOD_GAIN, 0);
            b.rnd (P_PITCH_AMT, 0, 40); b.rnd (P_PITCH_DECAY, 3, 20); b.rnd (P_PITCH_SLOPE, 0, 64);
            b.set (P_ATTACK, 0); b.rnd (P_DECAY, 4, 18); b.rnd (P_SLOPE, 0, 60);
            static const int filters[] = { 0, 1, 2, 3, 5 };
            b.pick (P_FILTER_TYPE, filters); b.rnd (P_FILTER_FREQ, 50, 120); b.rnd (P_FILTER_RESO, 0, 70);
            commonTransient ({ 2, 3, 4, 6, 7, 13 }, 0.15f, 0.4f);
            commonOut (40, 90, 0.35f);
            break;
        }
        case Flavor::Zap:
        {
            b.ch (P_MODEL, (int) Model::Drum);
            static const int waves[] = { WAVE_SINE, WAVE_TRI, WAVE_SAW };
            b.pick (P_OSC_WAVE, waves);
            b.rnd (P_OSC_COARSE, 30, 70);
            if (chance (0.3f)) { b.rnd (P_MOD_COARSE, 60, 120); b.norm (P_MOD_GAIN, 0.1f, 0.6f); } else b.set (P_MOD_GAIN, 0);
            b.rnd (P_PITCH_AMT, 90, 127); b.rnd (P_PITCH_DECAY, 3, 14); b.rnd (P_PITCH_SLOPE, 0, 40);
            b.set (P_ATTACK, 0); b.rnd (P_DECAY, 4, 16); b.rnd (P_SLOPE, 0, 50);
            b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 80, 127);
            commonTransient ({ 2, 4 }, 0.3f, 0.2f);
            commonOut (40, 90, 0.3f);
            break;
        }
        case Flavor::Snare:
        {
            b.ch (P_MODEL, (int) Model::Snare);
            const float w = uni (0, 1);
            b.ch (P_OSC_WAVE, w < 0.6f ? WAVE_TRI : (w < 0.9f ? WAVE_SINE : WAVE_REC));
            b.rnd (P_OSC_COARSE, 40, 60); b.rnd (P_OSC_FINE, 30, 97);
            b.rnd (P_NOISE_FREQ, 85, 127); b.rnd (P_NOISE_MIX, 45, 85);
            b.rnd (P_PITCH_AMT, 30, 90); b.rnd (P_PITCH_DECAY, 4, 25); b.rnd (P_PITCH_SLOPE, 0, 50);
            b.set (P_ATTACK, 0); b.rnd (P_DECAY, 6, 20); b.rnd (P_SLOPE, 5, 45);
            const float fx = uni (0, 1);
            if (fx < 0.5f) { b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 60, 127); }
            else if (fx < 0.75f) { b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 30, 70); }
            else { b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 60, 95); }
            b.rnd (P_FILTER_RESO, 0, 40); b.rnd (P_FILTER_DRIVE, 0, 30);
            commonTransient ({ 11, 2, 6, 4 }, 0.15f, 0.45f);
            commonOut (50, 95, 0.05f);
            break;
        }
        case Flavor::Clap:
        {
            b.ch (P_MODEL, (int) Model::Snare);
            b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 50, 70);
            b.rnd (P_NOISE_MIX, 100, 127); b.rnd (P_NOISE_FREQ, 95, 127);
            b.set (P_PITCH_AMT, 0);
            b.rnd (P_ATTACK, 4, 12);
            p[P_REPEAT] = std::sqrt ((float) irange (2, 5) / 127.f); // 2..5 hand claps
            b.rnd (P_DECAY, 8, 22); b.rnd (P_SLOPE, 5, 35);
            if (chance (0.7f)) { b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 55, 90); b.rnd (P_FILTER_RESO, 5, 45); }
            else { b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 50, 80); b.rnd (P_FILTER_RESO, 0, 25); }
            commonTransient ({ 9 }, 0.0f, 0.3f);
            commonOut (50, 90, 0.1f);
            break;
        }
        case Flavor::ClosedHat:
        case Flavor::OpenHat:
        {
            b.ch (P_MODEL, (int) Model::HiHat);
            const float w = uni (0, 1);
            b.ch (P_OSC_WAVE, w < 0.4f ? WAVE_SINE : (w < 0.7f ? WAVE_REC : (w < 0.85f ? WAVE_SAW : WAVE_TRI)));
            b.rnd (P_OSC_COARSE, 45, 80); b.rnd (P_OSC_FINE, 0, 127);
            static const int mw[] = { WAVE_SINE, WAVE_SINE, WAVE_REC, WAVE_NOISE };
            b.pick (P_MOD_WAVE, mw); b.rnd (P_MOD_COARSE, 40, 110); b.rnd (P_MOD_GAIN, 70, 127);
            static const int mw2[] = { WAVE_NOISE, WAVE_SINE, WAVE_REC };
            b.pick (P_MOD2_WAVE, mw2); b.rnd (P_MOD2_COARSE, 60, 127); b.rnd (P_MOD2_GAIN, 50, 127);
            b.set (P_ATTACK, 0);
            if (f == Flavor::ClosedHat) { b.rnd (P_DECAY, 2, 8); b.rnd (P_SLOPE, 0, 40); }
            else { b.rnd (P_DECAY, 12, 32); b.rnd (P_SLOPE, 10, 60); }
            if (chance (0.8f)) { b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 85, 115); }
            else { b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 95, 118); }
            b.rnd (P_FILTER_RESO, 0, 40);
            commonTransient ({ 8, 4 }, 0.0f, 0.25f);
            commonOut (40, 85, 0.12f);
            break;
        }
        case Flavor::Cymbal:
        {
            b.ch (P_MODEL, (int) Model::Cymbal);
            const float w = uni (0, 1);
            b.ch (P_OSC_WAVE, w < 0.4f ? WAVE_NOISE : (w < 0.7f ? WAVE_REC : WAVE_SINE));
            b.rnd (P_OSC_COARSE, 85, 127);
            static const int mw[] = { WAVE_SINE, WAVE_REC, WAVE_NOISE, WAVE_TRI };
            b.pick (P_MOD_WAVE, mw); b.rnd (P_MOD_COARSE, 40, 127); b.rnd (P_MOD_GAIN, 30, 127);
            b.pick (P_MOD2_WAVE, mw); b.rnd (P_MOD2_COARSE, 40, 127); b.rnd (P_MOD2_GAIN, 30, 127);
            b.set (P_ATTACK, 0); b.rnd (P_DECAY, 55, 110); b.rnd (P_SLOPE, 0, 50);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 55, 100); b.rnd (P_FILTER_RESO, 0, 30);
            commonTransient ({ 8 }, 0.0f, 0.2f);
            commonOut (40, 80, 0.2f);
            break;
        }
        case Flavor::Chaos:
        default:
        {
            for (int i = 0; i < NUM_PATCH_PARAMS; ++i) p[(size_t) i] = uni (0.f, 1.f);
            b.ch (P_MODEL, irange (0, 3));
            b.norm (P_DECAY, 0.0f, 0.7f);
            b.norm (P_ATTACK, 0.0f, 0.35f);
            b.ch (P_FILTER_TYPE, irange (0, 6));
            if (choiceIndex (p[P_FILTER_TYPE], 8) == 0 && p[P_FILTER_FREQ] < 0.35f) p[P_FILTER_FREQ] = uni (0.35f, 1.f);
            if (choiceIndex (p[P_FILTER_TYPE], 8) == 1 && p[P_FILTER_FREQ] > 0.9f) p[P_FILTER_FREQ] = uni (0.4f, 0.9f);
            b.norm (P_REPEAT, 0.f, chance (0.2f) ? 0.25f : 0.f);
            b.norm (P_LEVEL, 0.7f, 0.95f);
            b.norm (P_DECIMATE, chance (0.3f) ? 0.3f : 0.97f, 1.0f);
            b.norm (P_FILTER_RESO, 0.f, 0.8f);
            b.norm (P_VEL_AMT, 0.f, 0.3f); b.norm (P_LFO_AMT, 0.f, 0.35f);
            break;
        }
    }

    // occasional movement
    if (f != Flavor::Chaos)
    {
        if (chance (0.15f))
        {
            static const int dests[] = { P_FILTER_FREQ, P_OSC_COARSE, P_DECAY, P_PAN, P_MOD_GAIN, P_DRIVE, P_TRANS_VOL };
            const int d = dests[irange (0, 6)];
            const auto& tl = modTargets();
            for (size_t i = 0; i < tl.size(); ++i) if (tl[i] == d) b.p[P_LFO_DEST] = choiceValue ((int) i, (int) tl.size());
            b.ch (P_LFO_WAVE, irange (0, 7)); b.norm (P_LFO_FREQ, 0.2f, 0.7f); b.norm (P_LFO_AMT, 0.05f, 0.3f);
            b.ch (P_LFO_SYNC, 0);
        }
        else { b.set (P_LFO_AMT, 0); b.ch (P_LFO_DEST, 0); }
        if (chance (0.2f))
        {
            static const int dests[] = { P_FILTER_FREQ, P_PITCH_AMT, P_DECAY, P_TRANS_VOL };
            const int d = dests[irange (0, 3)];
            const auto& tl = modTargets();
            for (size_t i = 0; i < tl.size(); ++i) if (tl[i] == d) b.p[P_VEL_DEST] = choiceValue ((int) i, (int) tl.size());
            b.norm (P_VEL_AMT, 0.1f, 0.4f);
        }
        else { b.set (P_VEL_AMT, 0); b.ch (P_VEL_DEST, 0); }
        if (g != Genre::Any) applyGenre (p, f, g);
    }

    // structural settings stay with the channel
    p[P_OUTPUT] = current[P_OUTPUT];
    p[P_CHOKE] = current[P_CHOKE];
    p[P_LFO_RETRIG] = current[P_LFO_RETRIG];
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (isFeelParam (i)) p[(size_t) i] = current[(size_t) i]; // the player's feel too
    return p;
}

Flavor Randomizer::guessFlavor (const Patch& p)
{
    const Model m = (Model) choiceIndex (p[P_MODEL], 4);
    const float decay = p[P_DECAY] * 127.f;
    switch (m)
    {
        case Model::Snare:  return p[P_NOISE_MIX] > 0.85f && p[P_REPEAT] > 0.05f ? Flavor::Clap : Flavor::Snare;
        case Model::HiHat:  return decay > 10.f ? Flavor::OpenHat : Flavor::ClosedHat;
        case Model::Cymbal: return Flavor::Cymbal;
        case Model::Drum:
        default:
        {
            const float coarse = p[P_OSC_COARSE] * 127.f;
            if (coarse < 39.f) return Flavor::Kick;
            if (p[P_PITCH_AMT] * 127.f > 90.f) return Flavor::Zap;
            if (coarse < 62.f && p[P_MOD_GAIN] < 0.3f) return Flavor::Tom;
            return Flavor::Perc;
        }
    }
}

Patch Randomizer::patchForModel (const Patch& current, Genre g)
{
    const Model m = (Model) choiceIndex (current[P_MODEL], 4);
    Flavor f = Flavor::Kick;
    switch (m)
    {
        case Model::Drum:   { const Flavor o[] = { Flavor::Kick, Flavor::Tom, Flavor::Perc, Flavor::Zap }; f = o[irange (0, 3)]; break; }
        case Model::Snare:  f = chance (0.7f) ? Flavor::Snare : Flavor::Clap; break;
        case Model::HiHat:  f = chance (0.6f) ? Flavor::ClosedHat : Flavor::OpenHat; break;
        case Model::Cymbal: f = Flavor::Cymbal; break;
        default: break;
    }
    return patch (f, current, g);
}

//==============================================================================================
// Curated cymbals. The LXR cymbal is one carrier oscillator frequency-modulated by two modulators,
// then filtered. Metal comes from inharmonic carrier : modulator ratios with a high FM index; a
// noise modulator adds the wash. Fully random settings mostly give one whistling partial or a
// low, muddy ring, so each type below keeps its ratios, index, filter and decay in a range that
// measures like the real thing (dense spectrum, no single dominant partial, nothing below 400 Hz).
Patch Randomizer::cymbal (CymbalType t, const Patch& current, Genre g)
{
    Patch p = defaultPatch (Model::Cymbal);
    Builder b { p, *this };
    using namespace lxr;
    b.ch (P_MODEL, (int) Model::Cymbal);
    b.ch (P_FM_MODE, 0);
    b.rnd (P_ATTACK, 0, 2);                       // a hair of attack takes the click off the strike
    b.rnd (P_OSC_FINE, 44, 84);
    b.set (P_PITCH_AMT, 0);
    b.ch (P_TRANS_WAVE, 0); b.set (P_TRANS_VOL, 0);
    const auto rel = [&] (int id, float carrier, float lo, float hi) { b.set (id, std::clamp (carrier + uni (lo, hi), 0.f, 127.f)); };
    float c = 0;
    // Decay times (to -40 dB): crash 1.3-2 s, ride 1-1.6 s, splash 0.6-1 s, china 1-1.5 s,
    // 808 0.8-1.3 s, bell 0.5-0.9 s. The envelope's slope stays above 20 (an exponential tail).
    switch (t)
    {
        case CymbalType::Crash: // triangle / square carrier, triangle modulator at ~1.4-1.6x, noise wash
            b.ch (P_OSC_WAVE, chance (0.6f) ? WAVE_TRI : WAVE_REC); c = uni (74, 86); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, WAVE_TRI); rel (P_MOD_COARSE, c, 5.6f, 8.4f); b.norm (P_MOD_GAIN, 0.34f, 0.5f);
            b.ch (P_MOD2_WAVE, WAVE_NOISE); b.rnd (P_MOD2_COARSE, 110, 127); b.norm (P_MOD2_GAIN, 0.2f, 0.45f);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 72, 84); b.rnd (P_FILTER_RESO, 0, 15);
            b.rnd (P_DECAY, 62, 74); b.rnd (P_SLOPE, 22, 40);
            break;
        case CymbalType::Ride: // triangle carrier, sine modulator at a moderate index: a ping over a quiet wash
            b.ch (P_OSC_WAVE, WAVE_TRI); c = uni (72, 82); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, chance (0.5f) ? WAVE_SINE : WAVE_TRI); rel (P_MOD_COARSE, c, 10.4f, 13.6f); b.norm (P_MOD_GAIN, 0.3f, 0.45f);
            b.ch (P_MOD2_WAVE, WAVE_NOISE); b.rnd (P_MOD2_COARSE, 105, 127); b.norm (P_MOD2_GAIN, 0.15f, 0.32f);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 70, 82); b.rnd (P_FILTER_RESO, 0, 15);
            b.rnd (P_DECAY, 56, 68); b.rnd (P_SLOPE, 25, 45);
            break;
        case CymbalType::Splash: // a small, bright, short crash
            b.ch (P_OSC_WAVE, WAVE_TRI); c = uni (76, 88); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, WAVE_TRI); rel (P_MOD_COARSE, c, 5.6f, 8.4f); b.norm (P_MOD_GAIN, 0.3f, 0.42f);
            b.ch (P_MOD2_WAVE, WAVE_NOISE); b.rnd (P_MOD2_COARSE, 110, 127); b.norm (P_MOD2_GAIN, 0.25f, 0.5f);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 74, 86); b.rnd (P_FILTER_RESO, 0, 15);
            b.rnd (P_DECAY, 42, 52); b.rnd (P_SLOPE, 22, 35);
            break;
        case CymbalType::China: // trashy: saw carrier, square and saw modulators, resonant high-pass, a little drive
            b.ch (P_OSC_WAVE, WAVE_SAW); c = uni (74, 84); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, WAVE_REC); rel (P_MOD_COARSE, c, 4.5f, 7.5f); b.norm (P_MOD_GAIN, 0.7f, 0.95f);
            b.ch (P_MOD2_WAVE, WAVE_SAW); rel (P_MOD2_COARSE, c, 16.f, 21.f); b.norm (P_MOD2_GAIN, 0.45f, 0.75f);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 64, 76); b.rnd (P_FILTER_RESO, 15, 35);
            b.rnd (P_DECAY, 56, 66); b.rnd (P_SLOPE, 22, 35);
            break;
        case CymbalType::Cymbal808: // the TR-808 recipe: square waves at fixed odd ratios, high-passed
            b.ch (P_OSC_WAVE, WAVE_REC); c = uni (55, 60); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, WAVE_REC); rel (P_MOD_COARSE, c, 6.6f, 7.4f); b.norm (P_MOD_GAIN, 0.4f, 0.65f);
            b.ch (P_MOD2_WAVE, WAVE_REC); rel (P_MOD2_COARSE, c, 15.8f, 16.6f); b.norm (P_MOD2_GAIN, 0.3f, 0.55f);
            b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 90, 104); b.rnd (P_FILTER_RESO, 0, 15);
            b.rnd (P_DECAY, 48, 60); b.rnd (P_SLOPE, 22, 35);
            break;
        case CymbalType::Bell: // ride bell / ping: sine FM at ~2.6x with a low index, few partials
        default:
            b.ch (P_OSC_WAVE, WAVE_SINE); c = uni (74, 88); b.set (P_OSC_COARSE, c);
            b.ch (P_MOD_WAVE, WAVE_SINE); rel (P_MOD_COARSE, c, 15.5f, 17.f); b.norm (P_MOD_GAIN, 0.25f, 0.45f);
            b.ch (P_MOD2_WAVE, WAVE_SINE); rel (P_MOD2_COARSE, c, 27.f, 29.5f); b.norm (P_MOD2_GAIN, 0.1f, 0.25f);
            if (chance (0.6f)) { b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 85, 105); b.rnd (P_FILTER_RESO, 5, 20); }
            else { b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 100, 118); b.rnd (P_FILTER_RESO, 0, 12); }
            b.rnd (P_DECAY, 38, 50); b.rnd (P_SLOPE, 22, 40);
            break;
    }
    if (t == CymbalType::China) b.rnd (P_DRIVE, 30, 55); else b.rnd (P_DRIVE, 0, 18);
    b.set (P_DECIMATE, 127);
    b.norm (P_PAN, 0.35f, 0.65f);
    b.norm (P_VEL_VOL, 0.3f, 0.6f);
    b.set (P_LFO_AMT, 0); b.ch (P_LFO_DEST, 0); b.set (P_VEL_AMT, 0); b.ch (P_VEL_DEST, 0);
    // genre texture
    switch (g)
    {
        case Genre::Jungle: case Genre::BoomBap: b.rnd (P_DECIMATE, 80, 108); break;
        case Genre::LoFi: b.rnd (P_DECIMATE, 62, 92); b.p[P_FILTER_FREQ] = std::max (0.f, b.p[P_FILTER_FREQ] - uni (0.02f, 0.08f)); break;
        case Genre::Industrial: b.rnd (P_DRIVE, 40, 70); if (chance (0.4f)) b.rnd (P_DECIMATE, 70, 105); break;
        case Genre::Trap: case Genre::Garage: b.p[P_DECAY] = std::max (0.f, b.p[P_DECAY] - uni (0.03f, 0.07f)); break;
        default: break;
    }
    p[P_OUTPUT] = current[P_OUTPUT];
    p[P_CHOKE] = current[P_CHOKE];
    p[P_LFO_RETRIG] = current[P_LFO_RETRIG];
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) if (isFeelParam (i)) p[(size_t) i] = current[(size_t) i];
    fitCymbalLevel (p);
    return p;
}

// K-weighted short-term loudness (ITU BS.1770 filter shape, 400 ms window) of one full-velocity hit
float Randomizer::hitLoudness (const Patch& p)
{
    constexpr double sr = 44100.0;
    Voice v;
    v.prepare (sr, 17);
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) v.setParam (i, p[(size_t) i]);
    v.setParam (P_VARIANCE, 0.f);
    v.trigger (1.f, 60);
    std::vector<float> x ((size_t) (sr * 0.45));
    v.render (x.data(), (int) x.size());
    struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double run (double in) { const double y = b0 * in + z1; z1 = b1 * in - a1 * y + z2; z2 = b2 * in - a2 * y; return y; } };
    const double pi = 3.14159265358979;
    Bq shelf, hp;
    {
        const double K = std::tan (pi * 1681.97 / sr), Q = 0.7071752, Vh = std::pow (10.0, 3.99984 / 20.0), Vb = std::pow (Vh, 0.4996668);
        const double a0 = 1 + K / Q + K * K;
        shelf = { (Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 };
    }
    {
        const double K = std::tan (pi * 38.13547 / sr), Q = 0.500327, a0 = 1 + K / Q + K * K;
        hp = { 1 / a0, -2 / a0, 1 / a0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0 };
    }
    double e = 0;
    const size_t w = (size_t) (sr * 0.4);
    for (size_t i = 0; i < x.size(); ++i) { const double y = hp.run (shelf.run (x[i])); if (i < w) e += y * y; }
    return (float) (-0.691 + 10.0 * std::log10 (e / (double) w + 1e-12));
}

// Cymbals sit about 10 dB under a typical snare (around -25 LUFS for a full-velocity hit), so
// their level is set from a measured hit rather than guessed from the recipe.
void Randomizer::fitCymbalLevel (Patch& p)
{
    p[P_LEVEL] = 0.5f;
    const float target = uni (-27.5f, -24.5f);
    const float now = hitLoudness (p);
    p[P_LEVEL] = std::clamp (0.5f * std::pow (10.f, (target - now) / 20.f), 0.04f, 0.9f);
}

CymbalType Randomizer::cymbalTypeFor (Genre g)
{
    // weights: crash, ride, splash, china, 808, bell
    static const float w[(int) Genre::Count][6] = {
        { 25, 25, 15, 10, 15, 10 },  // Any
        { 30, 40, 10,  0, 20,  0 },  // House (909 ride and crash)
        { 20, 50,  0, 10,  0, 20 },  // Techno
        { 25, 55, 15,  5,  0,  0 },  // Drum & Bass
        { 45, 35, 10, 10,  0,  0 },  // Jungle
        { 40,  0, 20,  0, 40,  0 },  // Trap
        { 40, 40, 20,  0,  0,  0 },  // Boom Bap
        { 30, 50,  0,  0,  0, 20 },  // Lo-Fi
        { 20,  0,  0,  0, 70, 10 },  // Electro
        { 30, 40, 30,  0,  0,  0 },  // UK Garage
        { 50, 20,  0, 30,  0,  0 },  // Dubstep
        { 20,  0,  0, 50,  0, 30 },  // Industrial
        { 40,  0, 30,  0, 30,  0 },  // Dembow
        { 30,  0,  0,  0, 50, 20 },  // Footwork
    };
    const auto& row = w[std::clamp ((int) g, 0, (int) Genre::Count - 1)];
    float total = 0; for (float x : row) total += x;
    float r = uni (0.f, total);
    for (int i = 0; i < 6; ++i) { if (r < row[i]) return (CymbalType) i; r -= row[i]; }
    return CymbalType::Crash;
}

Genre Randomizer::genreFor (const DiceSettings& s)
{
    if (s.genre >= 0) return (Genre) std::clamp (s.genre, 0, (int) Genre::Count - 1);
    switch (s.style)
    {
        case BeatStyle::House:     return chance (0.5f) ? Genre::House : Genre::Techno;
        case BeatStyle::Breakbeat: return chance (0.5f) ? Genre::BoomBap : Genre::Jungle;
        case BeatStyle::DnB:       return chance (0.75f) ? Genre::DnB : Genre::Jungle;
        case BeatStyle::Trap:      return Genre::Trap;
        case BeatStyle::Dembow:    return Genre::Dembow;
        case BeatStyle::Electro:   return Genre::Electro;
        case BeatStyle::Footwork:  return Genre::Footwork;
        default:                   return Genre::Any;
    }
}

// Genre ranges in LXR units (0..127) on top of a flavor's recipe. Only what defines the genre is
// set; the rest of the recipe's randomness stays, so two DnB snares still differ.
void Randomizer::applyGenre (Patch& p, Flavor f, Genre g)
{
    Builder b { p, *this };
    using namespace lxr;
    const auto trans = [&] (int wave, float volLo, float volHi, float fLo = 20, float fHi = 70)
    {
        b.ch (P_TRANS_WAVE, wave); b.norm (P_TRANS_VOL, volLo, volHi); b.rnd (P_TRANS_FREQ, fLo, fHi);
    };
    const auto clean = [&] { b.set (P_DECIMATE, 127); };
    switch (f)
    {
        case Flavor::Kick:
            b.ch (P_FM_MODE, 0);
            switch (g)
            {
                case Genre::House:   b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 27, 33); b.rnd (P_PITCH_AMT, 50, 70); b.rnd (P_PITCH_DECAY, 10, 22);
                                     b.rnd (P_DECAY, 20, 34); b.rnd (P_SLOPE, 40, 70); b.rnd (P_DRIVE, 55, 80); trans (2, 0.2f, 0.45f); clean(); break;
                case Genre::Techno:  b.ch (P_OSC_WAVE, chance (0.7f) ? WAVE_SINE : WAVE_TRI); b.rnd (P_OSC_COARSE, 24, 30); b.rnd (P_PITCH_AMT, 45, 65);
                                     b.rnd (P_PITCH_DECAY, 12, 25); b.rnd (P_DECAY, 28, 48); b.rnd (P_SLOPE, 50, 90); b.rnd (P_DRIVE, 85, 115);
                                     b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 90, 115); b.rnd (P_FILTER_DRIVE, 30, 70); trans (chance (0.5f) ? 10 : 2, 0.25f, 0.5f); clean(); break;
                case Genre::DnB:     b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 29, 35); b.rnd (P_PITCH_AMT, 55, 80); b.rnd (P_PITCH_DECAY, 6, 14);
                                     b.rnd (P_DECAY, 12, 22); b.rnd (P_SLOPE, 30, 60); b.rnd (P_DRIVE, 70, 100); trans (chance (0.5f) ? 0 : 2, 0.4f, 0.7f, 40, 80); clean(); break;
                case Genre::Jungle:  b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 25, 31); b.rnd (P_PITCH_AMT, 35, 55); b.rnd (P_PITCH_DECAY, 14, 30);
                                     b.rnd (P_DECAY, 20, 36); b.rnd (P_DRIVE, 60, 90); b.rnd (P_DECIMATE, 85, 110); b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 85, 110); break;
                case Genre::Trap:    b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 22, 28); b.rnd (P_PITCH_AMT, 30, 55); b.rnd (P_PITCH_DECAY, 18, 40);
                                     b.rnd (P_DECAY, 60, 100); b.rnd (P_SLOPE, 60, 110); b.rnd (P_DRIVE, 70, 110); trans (chance (0.5f) ? 5 : 2, 0.15f, 0.4f); clean(); break;
                case Genre::BoomBap: b.ch (P_OSC_WAVE, chance (0.7f) ? WAVE_SINE : WAVE_TRI); b.rnd (P_OSC_COARSE, 26, 32); b.rnd (P_PITCH_AMT, 40, 60);
                                     b.rnd (P_PITCH_DECAY, 10, 20); b.rnd (P_DECAY, 18, 30); b.rnd (P_DRIVE, 65, 95); b.rnd (P_DECIMATE, 80, 105);
                                     b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 80, 105); trans (5, 0.3f, 0.6f); break;
                case Genre::LoFi:    b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 26, 32); b.rnd (P_DECAY, 16, 28); b.rnd (P_DECIMATE, 60, 95);
                                     b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 60, 90); b.rnd (P_DRIVE, 45, 70); trans (5, 0.f, 0.2f); break;
                case Genre::Electro: b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 25, 31); b.rnd (P_PITCH_AMT, 35, 55); b.rnd (P_PITCH_DECAY, 14, 28);
                                     b.rnd (P_DECAY, 35, 60); b.rnd (P_SLOPE, 40, 80); b.rnd (P_DRIVE, 50, 80); trans (3, 0.2f, 0.4f); clean(); break;
                case Genre::Garage:  b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 28, 34); b.rnd (P_PITCH_AMT, 50, 75); b.rnd (P_PITCH_DECAY, 8, 16);
                                     b.rnd (P_DECAY, 14, 24); b.rnd (P_DRIVE, 55, 85); trans (2, 0.3f, 0.5f); clean(); break;
                case Genre::Dubstep: b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 25, 31); b.rnd (P_PITCH_AMT, 60, 90); b.rnd (P_PITCH_DECAY, 10, 20);
                                     b.rnd (P_DECAY, 24, 40); b.rnd (P_DRIVE, 95, 125); b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 100, 127);
                                     b.rnd (P_FILTER_DRIVE, 40, 80); trans (10, 0.4f, 0.7f); clean(); break;
                case Genre::Industrial: b.ch (P_OSC_WAVE, chance (0.5f) ? WAVE_TRI : WAVE_SAW); b.rnd (P_OSC_COARSE, 24, 32); b.rnd (P_PITCH_AMT, 60, 100);
                                     b.rnd (P_DECAY, 20, 45); b.rnd (P_DRIVE, 110, 127); b.rnd (P_DECIMATE, 70, 110); b.ch (P_FILTER_TYPE, 0);
                                     b.rnd (P_FILTER_FREQ, 70, 100); b.rnd (P_FILTER_RESO, 30, 70); b.rnd (P_FILTER_DRIVE, 60, 110); trans (2, 0.3f, 0.6f); break;
                case Genre::Dembow:  b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 27, 33); b.rnd (P_PITCH_AMT, 45, 65); b.rnd (P_PITCH_DECAY, 12, 22);
                                     b.rnd (P_DECAY, 22, 36); b.rnd (P_DRIVE, 60, 90); trans (2, 0.25f, 0.45f); clean(); break;
                case Genre::Footwork: b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 22, 28); b.rnd (P_PITCH_AMT, 30, 50); b.rnd (P_PITCH_DECAY, 20, 40);
                                     b.rnd (P_DECAY, 55, 90); b.rnd (P_DRIVE, 60, 90); clean(); break;
                default: break;
            }
            break;
        case Flavor::Snare:
            switch (g)
            {
                case Genre::House:   b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 47, 54); b.rnd (P_NOISE_MIX, 60, 85); b.rnd (P_NOISE_FREQ, 110, 127);
                                     b.rnd (P_PITCH_AMT, 35, 60); b.rnd (P_PITCH_DECAY, 8, 16); b.rnd (P_DECAY, 12, 20); b.rnd (P_SLOPE, 15, 35);
                                     b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 100, 127); trans (11, 0.2f, 0.4f); b.rnd (P_DRIVE, 55, 80); clean(); break;
                case Genre::Techno:  b.ch (P_OSC_WAVE, chance (0.5f) ? WAVE_TRI : WAVE_SINE); b.rnd (P_OSC_COARSE, 44, 52); b.rnd (P_NOISE_MIX, 55, 80);
                                     b.rnd (P_DECAY, 14, 26); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 60, 85); b.rnd (P_FILTER_RESO, 10, 30);
                                     b.rnd (P_DRIVE, 80, 110); clean(); break;
                case Genre::DnB:     // tight, high-tuned body, bright noise, a hard crack on top
                                     b.ch (P_OSC_WAVE, chance (0.6f) ? WAVE_TRI : WAVE_SINE); b.rnd (P_OSC_COARSE, 55, 62); b.rnd (P_OSC_FINE, 50, 78);
                                     b.rnd (P_NOISE_MIX, 55, 80); b.rnd (P_NOISE_FREQ, 115, 127); b.rnd (P_PITCH_AMT, 40, 70); b.rnd (P_PITCH_DECAY, 4, 10);
                                     b.rnd (P_DECAY, 8, 15); b.rnd (P_SLOPE, 10, 30);
                                     if (chance (0.6f)) { b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 30, 55); } else { b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 110, 127); }
                                     b.rnd (P_FILTER_RESO, 0, 20);
                                     if (chance (0.6f)) trans (0, 0.45f, 0.8f, 60, 100); else trans (chance (0.5f) ? 6 : 11, 0.35f, 0.65f);
                                     b.rnd (P_DRIVE, 75, 105); clean(); break;
                case Genre::Jungle:  // Amen-flavoured: ringier mid body, bandpassed, a little crunchy
                                     b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 49, 56); b.rnd (P_NOISE_MIX, 50, 72); b.rnd (P_NOISE_FREQ, 95, 120);
                                     b.rnd (P_PITCH_AMT, 30, 55); b.rnd (P_DECAY, 13, 22); b.rnd (P_SLOPE, 20, 45); b.ch (P_FILTER_TYPE, 2);
                                     b.rnd (P_FILTER_FREQ, 70, 95); b.rnd (P_FILTER_RESO, 10, 35); b.rnd (P_DECIMATE, 75, 105); b.rnd (P_DRIVE, 70, 100);
                                     trans (11, 0.3f, 0.6f); break;
                case Genre::Trap:    b.ch (P_OSC_WAVE, chance (0.5f) ? WAVE_TRI : WAVE_SINE); b.rnd (P_OSC_COARSE, 50, 58); b.rnd (P_NOISE_MIX, 70, 100);
                                     b.rnd (P_NOISE_FREQ, 115, 127); b.rnd (P_PITCH_AMT, 30, 55); b.rnd (P_PITCH_DECAY, 6, 14); b.rnd (P_DECAY, 10, 18);
                                     b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 35, 60); trans (chance (0.5f) ? 9 : 0, 0.3f, 0.6f);
                                     if (chance (0.35f)) { b.rnd (P_ATTACK, 3, 7); p[P_REPEAT] = std::sqrt ((float) irange (1, 2) / 127.f); }
                                     b.rnd (P_DRIVE, 60, 90); clean(); break;
                case Genre::BoomBap: b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 44, 51); b.rnd (P_NOISE_MIX, 50, 75); b.rnd (P_NOISE_FREQ, 95, 120);
                                     b.rnd (P_PITCH_AMT, 30, 50); b.rnd (P_DECAY, 14, 24); b.rnd (P_SLOPE, 25, 50); b.ch (P_FILTER_TYPE, 0);
                                     b.rnd (P_FILTER_FREQ, 75, 100); b.rnd (P_DECIMATE, 80, 105); b.rnd (P_DRIVE, 70, 100); trans (11, 0.3f, 0.6f); break;
                case Genre::LoFi:    b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 44, 52); b.rnd (P_NOISE_MIX, 45, 70); b.rnd (P_NOISE_FREQ, 80, 105);
                                     b.rnd (P_DECAY, 12, 22); b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 60, 85); b.rnd (P_DECIMATE, 55, 90);
                                     b.rnd (P_DRIVE, 50, 75); trans (11, 0.1f, 0.35f); break;
                case Genre::Electro: b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 50, 56); b.rnd (P_NOISE_MIX, 40, 65); b.rnd (P_NOISE_FREQ, 105, 125);
                                     b.rnd (P_PITCH_AMT, 20, 40); b.rnd (P_PITCH_DECAY, 6, 12); b.rnd (P_DECAY, 8, 16); b.ch (P_FILTER_TYPE, 1);
                                     b.rnd (P_FILTER_FREQ, 30, 50); b.rnd (P_DRIVE, 50, 80); trans (0, 0.f, 0.25f); clean(); break;
                case Genre::Garage:  b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 54, 60); b.rnd (P_NOISE_MIX, 55, 80); b.rnd (P_DECAY, 8, 14);
                                     b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 35, 55); trans (6, 0.3f, 0.6f); b.rnd (P_DRIVE, 60, 90); clean(); break;
                case Genre::Dubstep: b.ch (P_OSC_WAVE, chance (0.5f) ? WAVE_TRI : WAVE_SINE); b.rnd (P_OSC_COARSE, 42, 50); b.rnd (P_NOISE_MIX, 60, 90);
                                     b.rnd (P_PITCH_AMT, 60, 95); b.rnd (P_PITCH_DECAY, 10, 20); b.rnd (P_DECAY, 18, 30); b.rnd (P_SLOPE, 25, 55);
                                     b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 95, 127); b.rnd (P_DRIVE, 100, 127); trans (11, 0.5f, 0.8f); clean(); break;
                case Genre::Industrial: b.ch (P_OSC_WAVE, chance (0.5f) ? WAVE_REC : WAVE_SAW); b.rnd (P_OSC_COARSE, 44, 56); b.rnd (P_NOISE_MIX, 70, 110);
                                     b.rnd (P_DECAY, 14, 28); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 60, 90); b.rnd (P_FILTER_RESO, 40, 80);
                                     b.rnd (P_DRIVE, 110, 127); b.rnd (P_DECIMATE, 60, 100); break;
                case Genre::Dembow:  b.ch (P_OSC_WAVE, WAVE_TRI); b.rnd (P_OSC_COARSE, 52, 58); b.rnd (P_NOISE_MIX, 50, 75); b.rnd (P_DECAY, 10, 16);
                                     b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 30, 50); trans (6, 0.4f, 0.7f); clean(); break;
                case Genre::Footwork: b.ch (P_OSC_WAVE, WAVE_SINE); b.rnd (P_OSC_COARSE, 52, 58); b.rnd (P_NOISE_MIX, 70, 95); b.rnd (P_DECAY, 8, 14);
                                     b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 35, 55); b.rnd (P_DRIVE, 70, 100); clean(); break;
                default: break;
            }
            break;
        case Flavor::Clap:
            switch (g)
            {
                case Genre::House: case Genre::Garage: case Genre::Dembow:
                    p[P_REPEAT] = std::sqrt ((float) irange (3, 4) / 127.f); b.rnd (P_DECAY, 12, 20); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 60, 80); clean(); break;
                case Genre::Trap: case Genre::Footwork:
                    p[P_REPEAT] = std::sqrt ((float) irange (2, 4) / 127.f); b.rnd (P_DECAY, 10, 18); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 65, 85); clean(); break;
                case Genre::DnB: case Genre::Techno:
                    p[P_REPEAT] = std::sqrt ((float) irange (2, 3) / 127.f); b.rnd (P_DECAY, 8, 14); b.rnd (P_DRIVE, 70, 100); clean(); break;
                case Genre::Electro:
                    p[P_REPEAT] = std::sqrt ((float) irange (3, 4) / 127.f); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 55, 70); b.rnd (P_FILTER_RESO, 20, 40); clean(); break;
                case Genre::BoomBap: case Genre::Jungle: b.rnd (P_DECIMATE, 75, 105); break;
                case Genre::LoFi: b.rnd (P_DECIMATE, 55, 90); b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 65, 85); break;
                case Genre::Industrial: case Genre::Dubstep: b.rnd (P_DRIVE, 105, 127); break;
                default: break;
            }
            break;
        case Flavor::ClosedHat:
        case Flavor::OpenHat:
        {
            const bool closed = f == Flavor::ClosedHat;
            const auto dec = [&] (float cl, float ch, float ol, float oh) { if (closed) b.rnd (P_DECAY, cl, ch); else b.rnd (P_DECAY, ol, oh); };
            switch (g)
            {
                case Genre::House:   dec (3, 6, 18, 30); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 95, 112); b.rnd (P_DRIVE, 50, 75); clean(); break;
                case Genre::Techno:  dec (2, 5, 12, 24); b.ch (P_FILTER_TYPE, chance (0.6f) ? 1 : 2); b.rnd (P_FILTER_FREQ, 100, 118); b.rnd (P_DRIVE, 70, 100); clean(); break;
                case Genre::DnB:     dec (1.5f, 4, 8, 16); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 105, 122); clean(); break;
                case Genre::Jungle:  dec (3, 6, 12, 20); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 95, 112); b.rnd (P_DECIMATE, 85, 110); break;
                case Genre::Trap:    dec (1, 3.5f, 10, 20); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 105, 125); clean(); break;
                case Genre::BoomBap: dec (3, 7, 14, 24); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 90, 108); b.rnd (P_DECIMATE, 75, 105); break;
                case Genre::LoFi:    dec (3, 7, 12, 22); b.ch (P_FILTER_TYPE, 2); b.rnd (P_FILTER_FREQ, 85, 102); b.rnd (P_DECIMATE, 60, 95); break;
                case Genre::Electro: b.ch (P_OSC_WAVE, WAVE_REC); b.ch (P_MOD_WAVE, WAVE_REC); b.ch (P_MOD2_WAVE, WAVE_REC); b.rnd (P_MOD_GAIN, 80, 127);
                                     dec (2, 5, 14, 26); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 98, 115); clean(); break;
                case Genre::Garage: case Genre::Dembow: case Genre::Footwork:
                                     dec (2, 4, 10, 18); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 102, 120); clean(); break;
                case Genre::Dubstep: dec (2, 5, 12, 22); b.ch (P_FILTER_TYPE, 1); b.rnd (P_FILTER_FREQ, 100, 118); clean(); break;
                case Genre::Industrial: b.ch (P_MOD2_WAVE, WAVE_NOISE); b.rnd (P_MOD2_GAIN, 100, 127); b.rnd (P_DRIVE, 100, 127); b.rnd (P_DECIMATE, 70, 110);
                                     dec (2, 6, 12, 26); break;
                default: break;
            }
            break;
        }
        case Flavor::Tom:
        case Flavor::Perc:
        case Flavor::Zap:
            switch (g) // texture only: these vary too much between tracks to pin down
            {
                case Genre::BoomBap: case Genre::Jungle: b.rnd (P_DECIMATE, 80, 108); break;
                case Genre::LoFi: b.rnd (P_DECIMATE, 55, 92); b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 65, 95); break;
                case Genre::Industrial: b.rnd (P_DRIVE, 105, 127); b.rnd (P_FILTER_RESO, 30, 70); break;
                case Genre::Dubstep: case Genre::Techno: b.rnd (P_DRIVE, 85, 115); clean(); break;
                case Genre::DnB: case Genre::Garage: b.p[P_DECAY] *= 0.75f; clean(); break;
                case Genre::Electro: if (f == Flavor::Zap || chance (0.5f)) { b.ch (P_OSC_WAVE, WAVE_SINE); } clean(); break;
                case Genre::Trap: case Genre::Footwork: if (f == Flavor::Tom) { b.rnd (P_DECAY, 30, 60); b.ch (P_OSC_WAVE, WAVE_SINE); } clean(); break;
                default: break;
            }
            break;
        default: break;
    }
}

Patch Randomizer::mutateSound (const Patch& base, float depth)
{
    Patch p = base;
    depth = std::clamp (depth, 0.f, 1.f);
    const Model m = (Model) choiceIndex (base[P_MODEL], 4);
    const bool metal = m == Model::Cymbal || m == Model::HiHat;
    const auto nudge = [&] (int id, float range, float lo = 0.f, float hi = 1.f)
    {
        p[(size_t) id] = std::clamp (p[(size_t) id] + uni (-1.f, 1.f) * range * depth, lo, hi);
    };
    nudge (P_OSC_COARSE, (metal ? 1.5f : 2.0f) / 127.f);   // semitones
    nudge (P_OSC_FINE, 0.12f);
    nudge (P_MOD_COARSE, (metal ? 1.2f : 3.0f) / 127.f);   // carrier : modulator ratio drifts
    nudge (P_MOD2_COARSE, (metal ? 1.2f : 3.0f) / 127.f);
    nudge (P_MOD_GAIN, 0.14f);
    nudge (P_MOD2_GAIN, 0.14f);
    nudge (P_PITCH_AMT, 0.14f);
    nudge (P_PITCH_DECAY, 0.12f);
    nudge (P_DECAY, 0.10f);
    nudge (P_SLOPE, 0.10f);
    nudge (P_NOISE_MIX, 0.14f);
    nudge (P_NOISE_FREQ, 0.12f);
    nudge (P_FILTER_FREQ, 0.14f);
    nudge (P_FILTER_RESO, 0.10f, 0.f, std::max (0.85f, base[P_FILTER_RESO]));
    nudge (P_FILTER_DRIVE, 0.10f);
    nudge (P_TRANS_VOL, 0.14f);
    nudge (P_TRANS_FREQ, 0.14f);
    nudge (P_DRIVE, 0.12f);
    nudge (P_ATTACK, 0.02f);
    if (base[P_DECIMATE] < 0.99f) nudge (P_DECIMATE, 0.08f);
    if (chance (depth * 0.12f)) p[P_TRANS_WAVE] = choiceValue (irange (0, paramDef (P_TRANS_WAVE).numChoices - 1), paramDef (P_TRANS_WAVE).numChoices);
    if (m == Model::Cymbal) fitCymbalLevel (p); // a mutated cymbal never jumps out of the mix
    return p;
}

Patch Randomizer::alter (const Patch& in, float amount)
{
    Patch p = in;
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i)
    {
        const auto& d = paramDef (i);
        if (! d.morphable || i == P_LEVEL || isFeelParam (i)) continue;
        if (d.numChoices > 0)
        {
            if (chance (amount * 0.3f)) p[(size_t) i] = choiceValue (irange (0, d.numChoices - 1), d.numChoices);
        }
        else
        {
            const float scale = (i == P_DECAY || i == P_ATTACK || i == P_OSC_COARSE) ? 0.5f : 1.0f;
            p[(size_t) i] = lxr::clamp01 (p[(size_t) i] + uni (-amount, amount) * scale);
        }
    }
    return p;
}

//---------------------------------------------------------------------------------------------
static bool euclid (int i, int k, int n, int rot) { const int j = ((i + rot) % n + n) % n; return ((j * k) % n) < k; }

void Randomizer::channelPattern (PatternData& d, int ch, Flavor role)
{
    const int n = d.length;
    uint16_t t = 0, a = 0, f = 0;
    auto on = [&] (int s, bool acc = false) { if (s < n) { t |= (uint16_t) (1u << s); if (acc) a |= (uint16_t) (1u << s); } };
    switch (role)
    {
        case Flavor::Kick:
            if (chance (0.3f)) { for (int s = 0; s < n; s += 4) on (s, s == 0 && chance (0.5f)); }
            else
            {
                on (0, chance (0.5f));
                if (chance (0.7f)) on (8);
                for (int s = 1; s < n; ++s) if (s != 8 && chance (s % 2 == 0 ? 0.22f : 0.1f)) on (s);
            }
            break;
        case Flavor::Snare:
            if (chance (0.85f)) { on (4, chance (0.3f)); on (chance (0.85f) ? 12 : 14, chance (0.4f)); }
            else for (int s = 0; s < n; ++s) if (euclid (s, irange (2, 4), n, irange (0, 3))) on (s);
            for (int s = 0; s < n; ++s) if (chance (0.07f)) on (s);
            if (chance (0.25f)) { on (n - 1); f |= (uint16_t) (1u << (n - 1)); }
            break;
        case Flavor::Clap:
            if (chance (0.7f)) { on (4); on (12, chance (0.3f)); }
            else { const int k = irange (3, 5), r = irange (0, 3); for (int s = 0; s < n; ++s) if (euclid (s, k, n, r)) on (s); }
            break;
        case Flavor::ClosedHat:
        {
            const float st = uni (0, 1);
            if (st < 0.5f) { for (int s = 0; s < n; s += 2) on (s); }
            else if (st < 0.8f) { for (int s = 0; s < n; ++s) on (s); }
            else { const int k = irange (5, 11), r = irange (0, 7); for (int s = 0; s < n; ++s) if (euclid (s, k, n, r)) on (s); }
            for (int s = 0; s < n; ++s) if (chance (0.1f)) t &= (uint16_t) ~(1u << s);
            if (chance (0.4f)) for (int s = 2; s < n; s += 4) if (t & (1u << s)) a |= (uint16_t) (1u << s);
            for (int s = 0; s < n; ++s) if ((t & (1u << s)) && chance (0.06f)) f |= (uint16_t) (1u << s);
            break;
        }
        case Flavor::OpenHat:
            if (chance (0.6f)) { for (int s = 2; s < n; s += 4) if (chance (0.45f)) on (s); }
            else on (14);
            break;
        case Flavor::Cymbal:
            if (chance (0.5f)) on (0, true);
            break;
        default:
        {
            const int k = irange (2, 7), r = irange (0, n - 1);
            for (int s = 0; s < n; ++s) if (euclid (s, k, n, r)) on (s, chance (0.2f));
            break;
        }
    }
    if (role != Flavor::ClosedHat) for (int s = 0; s < n; ++s) if ((t & (1u << s)) && chance (0.03f)) f |= (uint16_t) (1u << s);
    d.trig[(size_t) ch] = t; d.acc[(size_t) ch] = (uint16_t) (a & t); d.fill[(size_t) ch] = (uint16_t) (f & t);
}

void Randomizer::channelExpression (PatternData& d, int ch, Flavor role, float amt)
{
    auto& ex = d.expr[(size_t) ch];
    for (auto& lane : ex) lane.fill (0);
    const uint16_t t = d.trig[(size_t) ch], a = d.acc[(size_t) ch];
    auto set = [&] (int lane, int s, int v)
    {
        const auto& i = exprInfo (lane);
        ex[(size_t) lane][(size_t) s] = (int8_t) std::clamp (v, i.min, i.max);
    };
    for (int s = 0; s < d.length; ++s)
    {
        if (! ((t >> s) & 1u)) continue;
        const bool acc = (a >> s) & 1u, down = s % 4 == 0, offEighth = s % 4 == 2, odd = s % 2 == 1;
        switch (role)
        {
            case Flavor::Snare:
            case Flavor::Clap:
                if (! acc && s != 4 && s != 12 && ! (s == 14 && ! ((t >> 12) & 1u))) // all but the backbeats become ghosts
                {
                    if (chance (0.2f + 0.8f * amt)) set (X_GHOST, s, irange (1, 3));
                    if (chance (0.3f * amt)) set (X_CHANCE, s, irange (1, 4));
                    if (chance (0.3f * amt)) set (X_NUDGE, s, irange (1, 4));
                    if (chance (0.4f * amt)) set (X_TONE, s, -irange (1, 3));
                }
                else if (chance (0.15f * amt)) set (X_ROLL, s, chance (0.6f) ? 1 : 2);
                break;
            case Flavor::ClosedHat:
                if (odd)
                {
                    if (chance (0.6f * amt)) set (X_GHOST, s, irange (1, 2));
                    if (chance (0.35f * amt)) set (X_NUDGE, s, irange (1, 4));
                    if (chance (0.2f * amt)) set (X_CHANCE, s, irange (1, 5));
                }
                if (offEighth && chance (0.3f * amt)) set (X_DECAY, s, irange (1, 4));
                if (chance (0.35f * amt)) set (X_TONE, s, irange (-3, 2));
                if (chance (0.06f * amt)) set (X_ROLL, s, irange (3, 4));
                break;
            case Flavor::OpenHat:
                if (chance (0.3f * amt)) set (X_DECAY, s, irange (-3, 3));
                if (chance (0.2f * amt)) set (X_CHANCE, s, irange (8, 13));
                break;
            case Flavor::Kick:
                if (! down)
                {
                    if (chance (0.4f * amt)) set (X_GHOST, s, irange (1, 2));
                    if (chance (0.3f * amt)) set (X_CHANCE, s, irange (2, 5));
                }
                if (chance (0.1f * amt)) set (X_PITCH, s, -irange (1, 3));
                break;
            case Flavor::Tom:
            case Flavor::Perc:
            {
                static const int scale[6] = { -5, -3, 0, 2, 4, 7 };
                if (chance (0.55f * amt)) set (X_PITCH, s, scale[irange (0, 5)]);
                if (chance (0.3f * amt)) set (X_GHOST, s, irange (1, 2));
                if (chance (0.15f * amt)) set (X_CHANCE, s, irange (1, 6));
                if (chance (0.08f * amt)) set (X_ROLL, s, irange (1, 5));
                break;
            }
            case Flavor::Cymbal:
                if (chance (0.4f * amt)) set (X_CHANCE, s, irange (8, 13));
                break;
            default: // zaps and chaos: anything goes
                if (chance (0.3f * amt)) set (X_PITCH, s, irange (-12, 12));
                if (chance (0.3f * amt)) set (X_DECAY, s, irange (-6, 6));
                if (chance (0.3f * amt)) set (X_TONE, s, irange (-6, 6));
                if (chance (0.2f * amt)) set (X_CHANCE, s, irange (1, 7));
                if (chance (0.1f * amt)) set (X_ROLL, s, irange (1, 5));
                break;
        }
    }
}

void Randomizer::randomizeExpression (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel)
{
    for (int c = 0; c < kNumChannels; ++c)
        if (onlyChannel < 0 || c == onlyChannel) channelExpression (d, c, roles[(size_t) c], 1.0f);
}

int Randomizer::addGhostNotes (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel)
{
    const int n = d.length;
    int added = 0;
    // which channels: the chosen one, else snares that already play, else claps that play
    std::array<bool, kNumChannels> target {};
    if (onlyChannel >= 0) target[(size_t) onlyChannel] = true;
    else
        for (Flavor want : { Flavor::Snare, Flavor::Clap })
        {
            bool any = false;
            for (int c = 0; c < kNumChannels; ++c)
                if (roles[(size_t) c] == want && (d.trig[(size_t) c] & ((1u << n) - 1u))) target[(size_t) c] = any = true;
            if (any) break;
        }
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (! target[(size_t) c]) continue;
        auto& t = d.trig[(size_t) c];
        auto& ex = d.expr[(size_t) c];
        auto on = [&] (int s) { return s >= 0 && s < n && ((t >> s) & 1u) != 0; };
        auto mainHit = [&] (int s) { return on (s) && ex[X_GHOST][(size_t) s] == 0; };
        int budget = std::max (2, n / 3);
        for (int s = 0; s < n && budget > 0; ++s)
        {
            if (on (s)) continue;
            bool kickHere = false; // ghosts sit in the gaps between kicks and backbeats
            for (int k = 0; k < kNumChannels; ++k)
                if (k != c && roles[(size_t) k] == Flavor::Kick && ((d.trig[(size_t) k] >> s) & 1u)) kickHere = true;
            float w = s % 2 == 1 ? 0.42f : 0.22f;
            if (s % 4 == 0) w = 0.06f;
            if (mainHit (s + 1) || mainHit (s - 1)) w += 0.18f; // pick-ups and drags around the backbeat
            if (kickHere) w *= 0.35f;
            if (! chance (w)) continue;
            t = (uint16_t) (t | (1u << s));
            d.acc[(size_t) c] = (uint16_t) (d.acc[(size_t) c] & ~(1u << s));
            d.fill[(size_t) c] = (uint16_t) (d.fill[(size_t) c] & ~(1u << s));
            ex[X_GHOST][(size_t) s] = (int8_t) (s % 2 == 1 ? irange (2, 3) : irange (1, 2));
            ex[X_TONE][(size_t) s] = (int8_t) -irange (0, 2);
            ex[X_NUDGE][(size_t) s] = (int8_t) irange (0, 3);          // laid back a touch
            ex[X_CHANCE][(size_t) s] = (int8_t) (chance (0.4f) ? irange (1, 3) : 0);
            ex[X_ROLL][(size_t) s] = 0;
            ++added; --budget;
        }
        if (chance (0.3f)) // the odd flam on a backbeat
            for (int s = 0; s < n; ++s)
                if (mainHit (s) && (s == 4 || s == 12) && chance (0.5f)) { ex[X_ROLL][(size_t) s] = 1; break; }
    }
    return added;
}

void Randomizer::grooveShake (PatternData& d, int onlyChannel)
{
    const int n = d.length;
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (onlyChannel >= 0 && c != onlyChannel) continue;
        auto& ex = d.expr[(size_t) c];
        auto bump = [&] (int lane, int s, int lo, int hi)
        {
            const auto& i = exprInfo (lane);
            ex[(size_t) lane][(size_t) s] = (int8_t) std::clamp (ex[(size_t) lane][(size_t) s] + irange (lo, hi), i.min, i.max);
        };
        for (int s = 0; s < n; ++s)
        {
            if (! ((d.trig[(size_t) c] >> s) & 1u)) continue;
            bump (X_NUDGE, s, -1, 2);
            if (chance (0.5f)) bump (X_TONE, s, -1, 1);
            if (chance (0.4f)) bump (X_DECAY, s, -1, 1);
            const bool accented = (d.acc[(size_t) c] >> s) & 1u;
            if (! accented && chance (0.15f)) bump (X_GHOST, s, -1, 1);
            if (chance (0.05f) && ex[X_CHANCE][(size_t) s] == 0) ex[X_CHANCE][(size_t) s] = (int8_t) irange (1, 2);
            if (chance (0.06f)) // a hit slips to a free neighbour
            {
                const int t2 = std::clamp (s + (chance (0.5f) ? 1 : -1), 0, n - 1);
                if (! ((d.trig[(size_t) c] >> t2) & 1u)) { d.swapSteps (c, s, t2); }
            }
        }
    }
}

void Randomizer::randomizePattern (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel)
{
    for (int c = 0; c < kNumChannels; ++c)
        if (onlyChannel < 0 || c == onlyChannel)
        {
            channelPattern (d, c, roles[(size_t) c]);
            channelExpression (d, c, roles[(size_t) c], 0.5f); // a lighter touch than "Randomize Expression"
        }
}

void Randomizer::randomizeAccentsFills (PatternData& d, int onlyChannel)
{
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (onlyChannel >= 0 && c != onlyChannel) continue;
        uint16_t a = 0, f = 0;
        for (int s = 0; s < d.length; ++s)
        {
            if (! (d.trig[(size_t) c] & (1u << s))) continue;
            if (chance (0.22f)) a |= (uint16_t) (1u << s);
            if (chance (0.07f)) f |= (uint16_t) (1u << s);
        }
        d.acc[(size_t) c] = a; d.fill[(size_t) c] = f;
    }
}

void Randomizer::alterPattern (PatternData& d, int onlyChannel)
{
    const int n = d.length;
    if (n < 2) return;
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (onlyChannel >= 0 && c != onlyChannel) continue;
        if (d.trig[(size_t) c] == 0) continue; // only works on existing material
        const int ops = irange (1, 3);
        for (int o = 0; o < ops; ++o)
        {
            auto swapSteps = [&] (int s1, int s2) { d.swapSteps (c, s1, s2); };
            const float x = uni (0, 1);
            if (x < 0.5f) swapSteps (irange (0, n - 1), irange (0, n - 1));
            else if (x < 0.8f)
            {
                const int s = irange (0, n - 1);
                const int t = std::clamp (s + (chance (0.5f) ? 1 : -1), 0, n - 1);
                swapSteps (s, t);
            }
            else if (n >= 8)
            {
                const int blocks = n / 4, from = irange (0, blocks - 1), to = irange (0, blocks - 1);
                for (int k = 0; k < 4; ++k) d.copyStep (c, from * 4 + k, to * 4 + k);
            }
        }
    }
}

//=============================================================================================
// Beat generator
//
// Template styles give every role two 16-step probability strings: a "straight" one and a
// "syncopated" one. Complexity blends between them, density scales the probabilities, and steps
// marked x / X in both strings are anchors that always play (X = accented).
// '.' = never, '1'..'9' = 10..90 %, 'x' = always, 'X' = always + accent.
namespace
{
enum TplRole { TR_KICK = 0, TR_SNARE, TR_CLAP, TR_HATC, TR_HATO, TR_PERC, TR_CYM, TR_COUNT };
struct Tpl { const char* straight; const char* synco; };

const Tpl kTemplates[7][TR_COUNT] = {
    { // House / Techno
        { "X...x...x...x...", "X...x..2x...x.4." },
        { "....2.......2...", "....3..2....3.2." },
        { "....X.......X...", "....X.......X..2" },
        { "4...4...4...4...", "46.746.746.746.7" },
        { "..x...x...x...x.", "..x...x...x..3x." },
        { "......2.......2.", "..2..3.2.3..2..3" },
        { "3...............", "3.2...2.3.2...2." } },
    { // Breakbeat
        { "X.3.......x.2...", "X.5...3..2x.3.2." },
        { "....X.......X...", "....X..3.4..X..3" },
        { "....3.......3...", "....3.......3..." },
        { "x.x.x.x.x.x.x.x.", "x3x.x4x.x3x.x4x3" },
        { "..............3.", "......3.......4." },
        { "..........3.....", "...3..2...3..3.." },
        { "2...............", "3..............." } },
    { // Drum & Bass (two-step)
        { "X.........x.....", "X.3.......x..3.." },
        { "....X.......X...", "....X.......X..." },
        { "....2.......2...", "....2.......3..." },
        { "x.x.x.x.x.x.x.x.", "x.xxx.x.x.xxx.x." },
        { "......3.......3.", ".....3.......3.." },
        { "...........3....", "..3....3..3....3" },
        { "4...............", "4...........3..." } },
    { // Trap (half time: snare on the third beat)
        { "X......4..5.....", "X..5..4.3.x..4.." },
        { "........X.......", "........X......3" },
        { "........X.......", "........X......." },
        { "x.x.x.x.x.x.x.x.", "x.x.x5x.x.x.x5x." },
        { "..............3.", "......3.......3." },
        { "..........2.....", "...3......3..3.." },
        { "3...............", "3..............." } },
    { // Dembow
        { "X...x...x...x...", "X...x..3x...x..3" },
        { "...x..x....x..x.", "...x..x.3..x..x." },
        { "...3..3....3..3.", "...3..3....3..3." },
        { "x.x.x.x.x.x.x.x.", "x3x.x3x.x3x.x3x." },
        { "..............2.", "......2.......3." },
        { "..3.......3.....", ".3.3..3..3.3..3." },
        { "2...............", "2..............." } },
    { // Electro
        { "X.....x...x.....", "X..3..x...x..3.2" },
        { "....X.......X...", "....X.......X.3." },
        { "....3.......3...", "....4.......4..." },
        { "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx" },
        { "......3.......3.", "..3...3...3...3." },
        { "..........3.....", ".3....3....3..3." },
        { "2...............", "2..............." } },
    { // Footwork / juke
        { "X..x..x...x..x..", "X..x..x.x.x..x.x" },
        { "............X...", "....3.......X..3" },
        { "....X.......X...", "....X.......X..." },
        { "x.x.x.x.x.x.x.x.", "x.xx.xx.xx.xx.x." },
        { "..3.......3.....", "..3...3...3...3." },
        { "...3..3......3..", ".3.3..3.3..3.3.." },
        { "2...............", "2..............." } },
};

int templateIndex (BeatStyle st)
{
    switch (st)
    {
        case BeatStyle::House: return 0;  case BeatStyle::Breakbeat: return 1; case BeatStyle::DnB: return 2;
        case BeatStyle::Trap: return 3;   case BeatStyle::Dembow: return 4;    case BeatStyle::Electro: return 5;
        case BeatStyle::Footwork: return 6;
        default: return -1;
    }
}
TplRole templateRole (Flavor f)
{
    switch (f)
    {
        case Flavor::Kick: return TR_KICK;       case Flavor::Snare: return TR_SNARE; case Flavor::Clap: return TR_CLAP;
        case Flavor::ClosedHat: return TR_HATC;  case Flavor::OpenHat: return TR_HATO; case Flavor::Cymbal: return TR_CYM;
        default: return TR_PERC;
    }
}
float charProb (char c) { return (c == 'x' || c == 'X') ? 1.f : ((c >= '1' && c <= '9') ? (float) (c - '0') / 10.f : 0.f); }
bool isAnchor (char c) { return c == 'x' || c == 'X'; }
bool isBackbeat (Flavor role, int step)
{
    if (role == Flavor::Kick) return step == 0;
    if (role == Flavor::Snare || role == Flavor::Clap) return step == 4 || step == 12 || step == 8;
    return false;
}
void setBit (uint16_t& v, int s, bool on) { v = on ? (uint16_t) (v | (1u << s)) : (uint16_t) (v & ~(1u << s)); }
void clearStep (PatternData& d, int c, int s)
{
    setBit (d.trig[(size_t) c], s, false); setBit (d.acc[(size_t) c], s, false); setBit (d.fill[(size_t) c], s, false);
    for (auto& lane : d.expr[(size_t) c]) lane[(size_t) s] = 0;
}
bool has (const PatternData& d, int c, int s) { return (d.trig[(size_t) c] >> s) & 1u; }
} // namespace

const char* Randomizer::setSlotName (int p)
{
    static const char* n[kNumPatterns] = { "Main", "Variation", "Main + fill", "Busier", "Breakdown", "Build-up",
                                           "Half-time", "Hat run", "Minimal", "Glitch", "Variation 2", "Transition" };
    return n[std::clamp (p, 0, kNumPatterns - 1)];
}

void Randomizer::templateChannel (PatternData& d, int c, Flavor role, const DiceSettings& s, int dup)
{
    const Tpl& t = kTemplates[templateIndex (s.style)][templateRole (role)];
    const float dens = 0.25f + 1.5f * s.density;
    for (int st = 0; st < d.length && st < kMaxSteps; ++st)
    {
        const char a = t.straight[st], b = t.synco[st];
        const bool anchor = isAnchor (a) && isAnchor (b);
        float p = charProb (a) + (charProb (b) - charProb (a)) * s.complexity;
        if (! anchor) p = std::min (0.95f, p * dens);
        if (dup > 0) p *= anchor ? 0.35f : 0.5f; // a second kick / snare plays less
        // hats follow density all the way: sparse beats thin the eighths, busy ones fill the sixteenths
        if (role == Flavor::ClosedHat)
        {
            if (anchor && st % 4 != 0 && s.density < 0.4f) p = 1.f - (0.4f - s.density) * 2.2f;
            if (! anchor && p < 0.05f && s.density > 0.6f) p = (s.density - 0.6f) * 1.4f;
        }
        if (! chance (p)) continue;
        setBit (d.trig[(size_t) c], st, true);
        const bool big = a == 'X' || b == 'X';
        float pa = big ? 0.85f : 0.05f + 0.12f * s.complexity;
        if (role == Flavor::ClosedHat && st % 4 == 2) pa += 0.2f * s.complexity;
        if (chance (pa)) setBit (d.acc[(size_t) c], st, true);
    }
}

void Randomizer::euclidChannel (PatternData& d, int c, Flavor role, const DiceSettings& s, int dup)
{
    const int n = d.length;
    const float dd = s.density;
    int k = 3, rot = 0;
    switch (role)
    {
        case Flavor::Kick:      k = 2 + (int) std::lround (dd * 4.f); rot = s.complexity > 0.6f ? irange (0, 3) : 0; break;
        case Flavor::Snare:
        case Flavor::Clap:      k = 2 + (dd > 0.7f ? 1 : 0); rot = -1; break;
        case Flavor::ClosedHat: k = 5 + (int) std::lround (dd * 8.f); rot = irange (0, n - 1); break;
        case Flavor::OpenHat:   k = 1 + (int) std::lround (dd * 3.f); rot = irange (0, n - 1); break;
        case Flavor::Cymbal:    k = 1 + (dd > 0.6f ? 1 : 0); rot = 0; break;
        default:                k = 3 + (int) std::lround (dd * 4.f + s.complexity * 2.f); rot = irange (0, n - 1); break;
    }
    k = std::clamp (k - dup, 1, n);
    if (rot < 0) // snares: rotate so a hit lands on the backbeat
    {
        rot = 0;
        for (int r = 0; r < n; ++r) if (euclid (4 % n, k, n, r)) { rot = r; break; }
    }
    bool first = true;
    for (int st = 0; st < n; ++st)
        if (euclid (st, k, n, rot))
        {
            setBit (d.trig[(size_t) c], st, true);
            if ((first && chance (0.6f)) || chance (0.1f * s.complexity)) setBit (d.acc[(size_t) c], st, true);
            first = false;
        }
}

void Randomizer::glitchChannel (PatternData& d, int c, Flavor role, const DiceSettings& s)
{
    float p = 0.12f + 0.4f * s.density;
    if (role == Flavor::ClosedHat) p += 0.2f;
    if (role == Flavor::Cymbal || role == Flavor::OpenHat) p *= 0.4f;
    for (int st = 0; st < d.length; ++st)
    {
        const bool anchor = role == Flavor::Kick && st == 0;
        if (anchor || chance (p))
        {
            setBit (d.trig[(size_t) c], st, true);
            if (chance (0.15f + 0.2f * s.complexity)) setBit (d.acc[(size_t) c], st, true);
        }
    }
}

void Randomizer::densityAdjust (PatternData& d, int c, Flavor role, const DiceSettings& s)
{
    const int n = d.length;
    if (s.density < 0.5f)
    {
        const float drop = (0.5f - s.density) * 1.6f;
        for (int st = 0; st < n; ++st)
            if (has (d, c, st) && ! isBackbeat (role, st) && chance (drop)) clearStep (d, c, st);
    }
    else
    {
        const float add = (s.density - 0.5f) * 2.f;
        for (int st = 0; st < n; ++st)
        {
            if (has (d, c, st)) continue;
            const bool odd = st % 2 == 1;
            float w = 0.f;
            switch (role)
            {
                case Flavor::ClosedHat: w = odd ? 0.25f + 0.4f * s.complexity : 0.6f; break;
                case Flavor::Kick:      w = odd ? 0.06f + 0.12f * s.complexity : 0.14f; break;
                case Flavor::Snare:
                case Flavor::Clap:      w = odd ? 0.1f * s.complexity : 0.04f; break;
                case Flavor::OpenHat:
                case Flavor::Cymbal:    w = 0.04f; break;
                default:                w = 0.12f + 0.1f * s.complexity; break;
            }
            if (chance (w * add)) setBit (d.trig[(size_t) c], st, true);
        }
    }
}

void Randomizer::styleExpression (PatternData& d, int c, Flavor role, const DiceSettings& s, const std::array<Flavor, kNumChannels>& roles)
{
    const float e = s.expression;
    auto& ex = d.expr[(size_t) c];
    const int n = d.length;
    auto hits = [&] (auto fn) { for (int st = 0; st < n; ++st) if (has (d, c, st)) fn (st); };
    switch (s.style)
    {
        case BeatStyle::DnB:
        case BeatStyle::Breakbeat:
            if (role == Flavor::Snare && chance (e * (0.25f + s.density))) addGhostNotes (d, roles, c);
            if (role == Flavor::ClosedHat) hits ([&] (int st) { if (st % 2 == 1 && chance (0.6f * e)) ex[X_NUDGE][(size_t) st] = (int8_t) irange (1, 3); });
            break;
        case BeatStyle::Trap:
            if (role == Flavor::ClosedHat)
                hits ([&] (int st)
                {
                    if (chance (0.08f + 0.3f * e)) { ex[X_ROLL][(size_t) st] = (int8_t) irange (3, 5); ex[X_PITCH][(size_t) st] = (int8_t) irange (0, 5); }
                });
            if (role == Flavor::Kick) hits ([&] (int st) { if (st != 0 && chance (0.25f * e)) ex[X_PITCH][(size_t) st] = (int8_t) -irange (1, 5); });
            break;
        case BeatStyle::House:
            if (role == Flavor::OpenHat) hits ([&] (int st) { if (chance (0.5f * e)) ex[X_DECAY][(size_t) st] = (int8_t) irange (-3, 3); });
            if (role == Flavor::ClosedHat) hits ([&] (int st) { if (st % 4 == 0 && chance (0.6f * e)) ex[X_TONE][(size_t) st] = (int8_t) -irange (1, 3); });
            if (role == Flavor::Clap) hits ([&] (int st) { if (chance (0.15f * e)) ex[X_ROLL][(size_t) st] = 1; });
            break;
        case BeatStyle::Footwork:
            if (role == Flavor::ClosedHat || role == Flavor::Perc || role == Flavor::Tom)
                hits ([&] (int st) { if (chance (0.15f * e)) ex[X_ROLL][(size_t) st] = 4; });
            break;
        case BeatStyle::Dembow:
            if (role == Flavor::Perc || role == Flavor::Tom)
            {
                static const int scale[5] = { 0, 3, 5, 7, 10 };
                hits ([&] (int st) { if (chance (0.6f * e)) ex[X_PITCH][(size_t) st] = (int8_t) scale[irange (0, 4)]; });
            }
            break;
        case BeatStyle::Glitch:
            hits ([&] (int st)
            {
                if (chance (0.4f * e + 0.1f)) ex[X_CHANCE][(size_t) st] = (int8_t) irange (1, 13);
                if (chance (0.25f * e)) ex[X_ROLL][(size_t) st] = (int8_t) irange (3, 5);
                if (chance (0.3f * e)) ex[X_PITCH][(size_t) st] = (int8_t) irange (-12, 12);
                if (chance (0.3f * e)) ex[X_DECAY][(size_t) st] = (int8_t) irange (-8, 8);
                if (chance (0.2f * e)) ex[X_TONE][(size_t) st] = (int8_t) irange (-8, 6);
            });
            break;
        default: break;
    }
}

void Randomizer::generateBeat (PatternData& d, const std::array<Flavor, kNumChannels>& roles, const DiceSettings& s,
                               const ChannelMask& keep, bool mayChangeLength)
{
    if (s.style == BeatStyle::Glitch && mayChangeLength) d.length = irange (11, 16); // odd lengths are part of the fun
    d.length = std::clamp (d.length, 1, kMaxSteps);
    int seen[TR_COUNT] {};
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (keep[(size_t) c]) continue;
        const Flavor role = roles[(size_t) c];
        const int dup = seen[templateRole (role)]++;
        d.clearChannel (c);
        switch (s.style)
        {
            case BeatStyle::Auto:   channelPattern (d, c, role); densityAdjust (d, c, role, s); break;
            case BeatStyle::Euclid: euclidChannel (d, c, role, s, dup); break;
            case BeatStyle::Glitch: glitchChannel (d, c, role, s); break;
            default:                templateChannel (d, c, role, s, dup); break;
        }
        // the odd fill roll at the end of the bar
        const bool fillRole = role == Flavor::Snare || role == Flavor::Tom || role == Flavor::Perc;
        if (fillRole && dup == 0 && chance (0.04f + 0.14f * s.density * s.complexity))
        {
            const int st = d.length - 1 - (chance (0.5f) ? 1 : 0);
            if (st >= 0) { setBit (d.trig[(size_t) c], st, true); setBit (d.fill[(size_t) c], st, true); }
        }
        d.acc[(size_t) c] &= d.trig[(size_t) c];
        d.fill[(size_t) c] &= d.trig[(size_t) c];
        if (s.expression > 0.01f) channelExpression (d, c, role, s.expression);
        styleExpression (d, c, role, s, roles);
    }
}

void Randomizer::mutate (PatternData& d, const std::array<Flavor, kNumChannels>& roles, float amount, const ChannelMask& keep)
{
    const int n = d.length;
    if (n < 2) return;
    for (int c = 0; c < kNumChannels; ++c)
    {
        if (keep[(size_t) c] || d.trig[(size_t) c] == 0) continue;
        const Flavor role = roles[(size_t) c];
        const int ops = std::max (0, (int) std::lround (amount * 5.f * uni (0.4f, 1.6f)));
        auto randomHit = [&] (bool skipAnchors) -> int
        {
            for (int tries = 0; tries < 24; ++tries)
            {
                const int st = irange (0, n - 1);
                if (has (d, c, st) && ! (skipAnchors && isBackbeat (role, st))) return st;
            }
            return -1;
        };
        for (int o = 0; o < ops; ++o)
        {
            const float x = uni (0.f, 1.f);
            if (x < 0.25f) // add a hit where this role would play
            {
                const int st = irange (0, n - 1);
                if (has (d, c, st)) continue;
                const bool odd = st % 2 == 1;
                if (role == Flavor::Kick && odd && chance (0.6f)) continue;
                setBit (d.trig[(size_t) c], st, true);
                if (role == Flavor::Snare || role == Flavor::Clap) d.expr[(size_t) c][X_GHOST][(size_t) st] = (int8_t) irange (1, 3);
            }
            else if (x < 0.45f) { const int st = randomHit (true); if (st >= 0) clearStep (d, c, st); }
            else if (x < 0.62f) // slide a hit to a free neighbour
            {
                const int st = randomHit (true);
                if (st < 0) continue;
                const int to = std::clamp (st + (chance (0.5f) ? 1 : -1), 0, n - 1);
                if (! has (d, c, to)) d.swapSteps (c, st, to);
            }
            else if (x < 0.72f) { const int st = randomHit (false); if (st >= 0) setBit (d.acc[(size_t) c], st, ! ((d.acc[(size_t) c] >> st) & 1u)); }
            else // expression on one hit
            {
                const int st = randomHit (false);
                if (st < 0) continue;
                auto& ex = d.expr[(size_t) c];
                switch (irange (0, 5))
                {
                    case 0: if (! isBackbeat (role, st)) ex[X_GHOST][(size_t) st] = (int8_t) irange (0, 3); break;
                    case 1: ex[X_CHANCE][(size_t) st] = (int8_t) (chance (0.5f) ? 0 : irange (1, 5)); break;
                    case 2: ex[X_ROLL][(size_t) st] = (int8_t) (chance (0.6f) ? 0 : irange (1, 5)); break;
                    case 3: ex[X_NUDGE][(size_t) st] = (int8_t) irange (-3, 4); break;
                    case 4: ex[X_TONE][(size_t) st] = (int8_t) irange (-4, 3); break;
                    default: ex[X_DECAY][(size_t) st] = (int8_t) irange (-3, 3); break;
                }
            }
        }
        d.acc[(size_t) c] &= d.trig[(size_t) c];
        d.fill[(size_t) c] &= d.trig[(size_t) c];
    }
}

void Randomizer::generateSet (std::array<PatternData, kNumPatterns>& pats, const std::array<Flavor, kNumChannels>& roles,
                              const DiceSettings& s, const ChannelMask& keep)
{
    const auto old = pats;
    PatternData main;
    main.length = 16;
    main = [&] { PatternData m = old[0]; m.length = 16; generateBeat (m, roles, s, keep); return m; }();
    const int n = main.length;
    auto roleIs = [&] (int c, std::initializer_list<Flavor> fs) { for (auto f : fs) if (roles[(size_t) c] == f) return true; return false; };
    auto finish = [&] (PatternData x, int p)
    {
        for (int c = 0; c < kNumChannels; ++c) if (keep[(size_t) c]) x.setChannel (c, old[(size_t) p].channel (c));
        x.chainNext = old[(size_t) p].chainNext;
        pats[(size_t) p] = x;
    };
    auto onlyRoles = [&] (std::initializer_list<Flavor> fs) { ChannelMask k = keep; for (int c = 0; c < kNumChannels; ++c) if (! roleIs (c, fs)) k[(size_t) c] = true; return k; };

    finish (main, 0);                                                       // A main
    { PatternData b = main; mutate (b, roles, 0.15f + 0.6f * s.mutation, keep); finish (b, 1); } // B variation
    {                                                                        // C main + fill ending
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Snare, Flavor::Tom, Flavor::Perc }))
                for (int st = n - 4; st < n; ++st)
                    if (chance (roleIs (c, { Flavor::Snare }) ? 0.7f : 0.45f))
                    {
                        setBit (x.trig[(size_t) c], st, true);
                        if (chance (0.35f)) setBit (x.fill[(size_t) c], st, true);
                        if (st == n - 1) setBit (x.acc[(size_t) c], st, true);
                    }
            if (roleIs (c, { Flavor::OpenHat, Flavor::Cymbal })) for (int st = n - 4; st < n; ++st) clearStep (x, c, st);
        }
        finish (x, 2);
    }
    {                                                                        // D busier hats and percussion
        DiceSettings sd = s; sd.density = std::min (1.f, s.density + 0.3f); sd.complexity = std::min (1.f, s.complexity + 0.15f);
        PatternData x = main;
        generateBeat (x, roles, sd, onlyRoles ({ Flavor::ClosedHat, Flavor::OpenHat, Flavor::Perc, Flavor::Tom, Flavor::Zap }), false);
        finish (x, 3);
    }
    {                                                                        // E breakdown
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Kick, Flavor::Cymbal })) x.clearChannel (c);
            else if (roleIs (c, { Flavor::Snare, Flavor::Clap })) { for (int st = 0; st < n; ++st) if (st != 12) clearStep (x, c, st); }
            else for (int st = 0; st < n; ++st) if (has (x, c, st) && chance (0.4f)) clearStep (x, c, st);
        }
        finish (x, 4);
    }
    {                                                                        // F build-up
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Kick })) { x.clearChannel (c); for (int st = 0; st < n; st += 4) setBit (x.trig[(size_t) c], st, true); }
            else if (roleIs (c, { Flavor::Snare, Flavor::Clap }))
            {
                x.clearChannel (c);
                for (int st = 0; st < n; ++st)
                    if (st >= n / 2 || st % 4 == 0 || (st >= n / 4 && st % 2 == 0))
                    {
                        setBit (x.trig[(size_t) c], st, true);
                        if (st >= n - 4) setBit (x.fill[(size_t) c], st, roleIs (c, { Flavor::Snare }));
                        x.expr[(size_t) c][X_GHOST][(size_t) st] = (int8_t) (st < n / 4 ? 3 : (st < n / 2 ? 2 : (st < 3 * n / 4 ? 1 : 0)));
                    }
                setBit (x.acc[(size_t) c], n - 1, true);
            }
            else if (roleIs (c, { Flavor::Cymbal, Flavor::OpenHat })) x.clearChannel (c);
        }
        finish (x, 5);
    }
    {                                                                        // G half-time
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Snare, Flavor::Clap })) { x.clearChannel (c); setBit (x.trig[(size_t) c], 8 % n, true); setBit (x.acc[(size_t) c], 8 % n, true); }
            else if (roleIs (c, { Flavor::Kick })) { for (int st = 1; st < n; ++st) if (has (x, c, st) && st != 10 && chance (0.6f)) clearStep (x, c, st); }
            else if (roleIs (c, { Flavor::ClosedHat })) { for (int st = 1; st < n; st += 2) clearStep (x, c, st); }
        }
        finish (x, 6);
    }
    {                                                                        // H hat run
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c] || ! roleIs (c, { Flavor::ClosedHat })) continue;
            for (int st = 0; st < n; ++st)
            {
                setBit (x.trig[(size_t) c], st, true);
                x.expr[(size_t) c][X_GHOST][(size_t) st] = (int8_t) (st % 2 == 1 ? 2 : 0);
                if (chance (0.12f)) x.expr[(size_t) c][X_ROLL][(size_t) st] = (int8_t) irange (3, 5);
            }
        }
        finish (x, 7);
    }
    {                                                                        // I minimal
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Snare, Flavor::Clap })) { for (int st = 0; st < n; ++st) if (! isBackbeat (roles[(size_t) c], st)) clearStep (x, c, st); }
            else if (! roleIs (c, { Flavor::Kick, Flavor::ClosedHat })) x.clearChannel (c);
        }
        finish (x, 8);
    }
    {                                                                        // J glitch
        DiceSettings sg = s; sg.style = BeatStyle::Glitch; sg.expression = std::max (0.6f, s.expression);
        PatternData x = main;
        for (int c = 0; c < kNumChannels; ++c) if (! keep[(size_t) c]) styleExpression (x, c, roles[(size_t) c], sg, roles);
        mutate (x, roles, 0.5f, keep);
        finish (x, 9);
    }
    { PatternData b = main; mutate (b, roles, 0.25f + 1.1f * s.mutation, keep); finish (b, 10); } // K variation 2
    {                                                                        // L transition
        PatternData x = main;
        static const int run[4] = { 7, 4, 2, 0 };
        for (int c = 0; c < kNumChannels; ++c)
        {
            if (keep[(size_t) c]) continue;
            if (roleIs (c, { Flavor::Cymbal })) { x.clearChannel (c); setBit (x.trig[(size_t) c], 0, true); setBit (x.acc[(size_t) c], 0, true); }
            else if (roleIs (c, { Flavor::Tom, Flavor::Perc }))
            {
                for (int st = n / 2; st < n; ++st) clearStep (x, c, st);
                for (int st = n / 2; st < n; st += 2)
                {
                    setBit (x.trig[(size_t) c], st, true);
                    x.expr[(size_t) c][X_PITCH][(size_t) st] = (int8_t) run[std::min (3, (st - n / 2) / 2 % 4)];
                }
            }
            else if (roleIs (c, { Flavor::Snare })) for (int st = n - 3; st < n; ++st) { setBit (x.trig[(size_t) c], st, true); x.expr[(size_t) c][X_ROLL][(size_t) st] = (int8_t) (st == n - 1 ? 5 : 2); }
            else if (roleIs (c, { Flavor::Kick })) { for (int st = 1; st < n; ++st) clearStep (x, c, st); setBit (x.trig[(size_t) c], 0, true); }
        }
        finish (x, 11);
    }
}

void Randomizer::randomizeExpressionLane (PatternData& d, int ch, Flavor role, int lane, float amount)
{
    PatternData tmp = d;
    channelExpression (tmp, ch, role, amount);
    if (lane == X_GHOST && (role == Flavor::Snare || role == Flavor::Clap)) // make sure there is something to hear
        for (int st = 0; st < d.length; ++st)
            if (has (d, ch, st) && ! isBackbeat (role, st) && tmp.expr[(size_t) ch][X_GHOST][(size_t) st] == 0 && chance (0.5f))
                tmp.expr[(size_t) ch][X_GHOST][(size_t) st] = (int8_t) irange (1, 3);
    if (lane != X_GHOST && lane != X_CHANCE && lane != X_ROLL) // continuous lanes: give every hit a value
    {
        const auto& info = exprInfo (lane);
        const int span = lane == X_PITCH ? 7 : (lane == X_NUDGE ? 4 : 4);
        for (int st = 0; st < d.length; ++st)
            if (has (d, ch, st) && chance (0.5f + 0.4f * amount))
                tmp.expr[(size_t) ch][(size_t) lane][(size_t) st] = (int8_t) std::clamp (irange (-span, span), info.min, info.max);
    }
    d.expr[(size_t) ch][(size_t) lane] = tmp.expr[(size_t) ch][(size_t) lane];
}

//=============================================================================================
// Kit sounds
Patch Randomizer::kitPatch (Flavor f, const Patch& current, const DiceSettings& s)
{
    Patch p = patch (f, current, genreFor (s));
    applyCharacter (p, s.character);
    tuneToKey (p, s.key);
    return p;
}

void Randomizer::applyCharacter (Patch& p, KitCharacter c)
{
    Builder b { p, *this };
    using namespace lxr;
    const Model m = (Model) choiceIndex (p[P_MODEL], 4);
    if (m == Model::Cymbal) // the curated cymbals keep their own tone; a character only nudges them
    {
        switch (c)
        {
            case KitCharacter::Analog:   b.rnd (P_DRIVE, 10, 30); break;
            case KitCharacter::Gritty:   b.rnd (P_DRIVE, 30, 55); break;
            case KitCharacter::LoFi:     b.rnd (P_DECIMATE, 70, 100); b.rnd (P_FILTER_FREQ, 66, 84); break;
            case KitCharacter::Metallic: b.norm (P_MOD_GAIN, std::min (1.f, p[P_MOD_GAIN] + 0.1f), std::min (1.f, p[P_MOD_GAIN] + 0.2f)); break;
            case KitCharacter::Clean: default: b.rnd (P_DRIVE, 0, 12); break;
        }
        fitCymbalLevel (p);
        return;
    }
    switch (c)
    {
        case KitCharacter::Clean:
            b.rnd (P_DRIVE, 0, 25); b.set (P_DECIMATE, 127); b.norm (P_FILTER_DRIVE, 0.f, 0.08f);
            break;
        case KitCharacter::Analog:
            b.rnd (P_DRIVE, 45, 80); b.set (P_DECIMATE, 127); b.norm (P_FILTER_DRIVE, 0.1f, 0.35f);
            if (m == Model::Drum) b.ch (P_OSC_WAVE, chance (0.7f) ? WAVE_SINE : WAVE_TRI);
            break;
        case KitCharacter::Gritty:
            b.rnd (P_DRIVE, 90, 127); b.norm (P_FILTER_DRIVE, 0.35f, 0.8f);
            if (chance (0.35f)) b.rnd (P_DECIMATE, 85, 115);
            break;
        case KitCharacter::LoFi:
            b.rnd (P_DECIMATE, 55, 95); b.rnd (P_DRIVE, 50, 90);
            if (m == Model::Drum || m == Model::Snare) { b.ch (P_FILTER_TYPE, 0); b.rnd (P_FILTER_FREQ, 70, 105); }
            else b.rnd (P_FILTER_FREQ, 70, 95);
            break;
        case KitCharacter::Metallic:
            if (m == Model::Drum)
            {
                static const int w[] = { WAVE_REC, WAVE_SAW, WAVE_SINE };
                b.pick (P_MOD_WAVE, w); b.rnd (P_MOD_COARSE, 70, 120); b.norm (P_MOD_GAIN, 0.35f, 0.85f); b.ch (P_FM_MODE, 0);
            }
            else if (m == Model::Snare) { b.ch (P_OSC_WAVE, WAVE_REC); b.rnd (P_NOISE_MIX, 30, 55); }
            else { b.ch (P_OSC_WAVE, WAVE_REC); b.ch (P_MOD_WAVE, WAVE_REC); b.rnd (P_MOD_GAIN, 90, 127); }
            break;
        default: break;
    }
}

void Randomizer::tuneToKey (Patch& p, int key)
{
    if (key < 0) return;
    const Model m = (Model) choiceIndex (p[P_MODEL], 4);
    if (m != Model::Drum && m != Model::Snare) return; // hats and cymbals are noise: leave them
    static const int scale[5] = { 0, 3, 5, 7, 10 };     // minor pentatonic
    const int note = (int) std::lround (p[P_OSC_COARSE] * 127.f);
    int best = note, bestDist = 1000;
    for (int n = std::max (0, note - 6); n <= std::min (127, note + 6); ++n)
    {
        const int deg = ((n - key) % 12 + 12) % 12;
        bool inScale = false;
        for (int sd : scale) inScale |= sd == deg;
        if (inScale && std::abs (n - note) < bestDist) { best = n; bestDist = std::abs (n - note); }
    }
    p[P_OSC_COARSE] = (float) best / 127.f;
    p[P_OSC_FINE] = 0.5f;
}
} // namespace ferrum
