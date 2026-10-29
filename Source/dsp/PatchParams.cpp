// PatchParams.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PatchParams.h"
#include "LxrCore.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace ferrum
{
const char* const kModelNames[4]     = { "Drum", "Snare", "Cymbal", "Hi-Hat" };
const char* const kWaveNames[6]      = { "Sine", "Tri", "Saw", "Square", "Noise", "PWM" };
const char* const kFilterNames[8]    = { "LP", "HP", "BP", "Unity BP", "Notch", "Peak", "LP2", "Off" };
const char* const kTransientNames[14] = { "Snap", "Offset", "Click", "Click 2", "Tick", "Kick", "Rim",
                                          "Drop", "Hat", "Clap", "Kick 2", "Snare", "Tom", "Snap 2" };
const char* const kLfoWaveNames[8]   = { "Sine", "Tri", "Saw Up", "Saw Down", "Square", "Random", "Exp Up", "Exp Down" };
const char* const kSyncNames[12]     = { "Free", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32" };
static const char* const kFmModeNames[2] = { "FM", "Mix" };
static const char* const kOutputNames[3] = { "A", "B", "FX" };
static const char* const kOnOff[2]       = { "Off", "On" };
static const char* const kRetrigNames[9] = { "Free", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8" };

static const std::vector<int>& targetList()
{
    static const std::vector<int> t = {
        -1, P_OSC_COARSE, P_OSC_FINE, P_OSC_WAVE, P_PWM, P_MOD_COARSE, P_MOD_GAIN, P_MOD_WAVE,
        P_MOD2_COARSE, P_MOD2_GAIN, P_MOD2_WAVE, P_NOISE_FREQ, P_NOISE_MIX, P_PITCH_AMT, P_PITCH_DECAY,
        P_PITCH_SLOPE, P_ATTACK, P_DECAY, P_SLOPE, P_FILTER_FREQ, P_FILTER_RESO, P_FILTER_DRIVE,
        P_FILTER_TYPE, P_TRANS_VOL, P_TRANS_FREQ, P_TRANS_WAVE, P_DRIVE, P_DECIMATE, P_LEVEL, P_PAN,
        P_LFO_FREQ, P_LFO_AMT };
    return t;
}
const std::vector<int>& modTargets() { return targetList(); }
int modTargetCount() { return (int) targetList().size(); }

static const char* const* targetNames()
{
    static std::vector<std::string> storage;
    static std::vector<const char*> ptrs;
    if (ptrs.empty())
    {
        for (int p : targetList())
            storage.push_back (p < 0 ? std::string ("Off") : std::string (paramDef (p).name));
        for (auto& s : storage) ptrs.push_back (s.c_str());
    }
    return ptrs.data();
}

static const ParamDef* defs()
{
    static ParamDef d[NUM_PATCH_PARAMS];
    static bool init = false;
    if (init) return d;
    init = true;
    auto C = [] (const char* id, const char* name, const char* label, float def, bool morph = true)
    { return ParamDef { id, name, label, 0, nullptr, def, morph }; };
    auto L = [] (const char* id, const char* name, const char* label, int n, const char* const* ch, int defIdx, bool morph = true)
    { return ParamDef { id, name, label, n, ch, choiceValue (defIdx, n), morph }; };

    d[P_MODEL]        = L ("model", "Model", "Model", 4, kModelNames, 0, false);
    d[P_OSC_WAVE]     = L ("oscWave", "Osc Wave", "Wave", 6, kWaveNames, 0);
    d[P_OSC_COARSE]   = C ("oscCoarse", "Osc Pitch", "Pitch", 70.f / 127.f);
    d[P_OSC_FINE]     = C ("oscFine", "Osc Fine", "Fine", 0.5f);
    d[P_PWM]          = C ("pwm", "Pulse Width", "PW", 0.0f);
    d[P_MOD_WAVE]     = L ("modWave", "Mod Wave", "Mod Wave", 6, kWaveNames, 0);
    d[P_MOD_COARSE]   = C ("modCoarse", "Mod Pitch", "Mod Pitch", 70.f / 127.f);
    d[P_MOD_GAIN]     = C ("modGain", "Mod Amount", "Mod Amt", 0.0f);
    d[P_MOD2_WAVE]    = L ("mod2Wave", "Mod 2 Wave", "Mod2 Wave", 6, kWaveNames, 4);
    d[P_MOD2_COARSE]  = C ("mod2Coarse", "Mod 2 Pitch", "Mod2 Pitch", 70.f / 127.f);
    d[P_MOD2_GAIN]    = C ("mod2Gain", "Mod 2 Amount", "Mod2 Amt", 0.0f);
    d[P_FM_MODE]      = L ("fmMode", "FM / Mix", "FM/Mix", 2, kFmModeNames, 0);
    d[P_NOISE_FREQ]   = C ("noiseFreq", "Noise Rate", "Noise Rt", 1.0f);
    d[P_NOISE_MIX]    = C ("noiseMix", "Noise Mix", "Noise", 0.5f);
    d[P_PITCH_AMT]    = C ("pitchAmt", "Pitch Env Amount", "P.Amount", 0.0f);
    d[P_PITCH_DECAY]  = C ("pitchDecay", "Pitch Env Decay", "P.Decay", 30.f / 127.f);
    d[P_PITCH_SLOPE]  = C ("pitchSlope", "Pitch Env Slope", "P.Slope", 0.5f);
    d[P_ATTACK]       = C ("attack", "Attack", "Attack", 0.0f);
    d[P_DECAY]        = C ("decay", "Decay", "Decay", 20.f / 127.f);
    d[P_SLOPE]        = C ("slope", "Decay Slope", "Slope", 0.5f);
    d[P_REPEAT]       = C ("repeat", "Repeat", "Repeat", 0.0f);
    d[P_FILTER_TYPE]  = L ("filterType", "Filter Type", "Type", 8, kFilterNames, 0);
    d[P_FILTER_FREQ]  = C ("filterFreq", "Filter Freq", "Freq", 1.0f);
    d[P_FILTER_RESO]  = C ("filterReso", "Resonance", "Reso", 0.0f);
    d[P_FILTER_DRIVE] = C ("filterDrive", "Filter Drive", "F.Drive", 0.0f);
    d[P_TRANS_WAVE]   = L ("transWave", "Transient Type", "Type", 14, kTransientNames, 0);
    d[P_TRANS_VOL]    = C ("transVol", "Transient Level", "Level", 0.0f);
    d[P_TRANS_FREQ]   = C ("transFreq", "Transient Pitch", "Pitch", 0.75f * 33.9f / 127.f);
    d[P_DRIVE]        = C ("drive", "Distortion", "Distort", 0.0f);
    d[P_DECIMATE]     = C ("decimate", "Sample Rate", "Rate", 1.0f);
    d[P_LEVEL]        = C ("level", "Level", "Level", 0.8f);
    d[P_PAN]          = C ("pan", "Pan", "Pan", 0.5f);
    d[P_OUTPUT]       = L ("output", "Output", "Output", 3, kOutputNames, 0, false);
    d[P_CHOKE]        = L ("choke", "Choke", "Choke", 2, kOnOff, 0, false);
    d[P_VEL_VOL]      = C ("velVol", "Velocity > Level", "Vel>Lvl", 0.4f);
    d[P_VEL_AMT]      = C ("velAmt", "Velocity Mod Amount", "Vel Amt", 0.0f);
    d[P_LFO_WAVE]     = L ("lfoWave", "LFO Wave", "Wave", 8, kLfoWaveNames, 0);
    d[P_LFO_FREQ]     = C ("lfoFreq", "LFO Rate", "Rate", 0.3f);
    d[P_LFO_SYNC]     = L ("lfoSync", "LFO Sync", "Sync", 12, kSyncNames, 0, false);
    d[P_LFO_RETRIG]   = L ("lfoRetrig", "LFO Retrigger", "Retrig", 9, kRetrigNames, 0, false);
    d[P_LFO_PHASE]    = C ("lfoPhase", "LFO Phase", "Phase", 0.0f);
    d[P_LFO_AMT]      = C ("lfoAmt", "LFO Amount", "Amount", 0.0f);
    d[P_GHOST]        = C ("ghost", "Ghost Depth", "Ghost", 0.5f);
    d[P_HUMANIZE]     = C ("humanize", "Humanize", "Human", 0.0f);
    d[P_VARIANCE]     = C ("variance", "Variance", "Vary", 0.0f);
    // the target selectors list other parameters' names, so they are filled in last
    d[P_VEL_DEST] = ParamDef { "velDest", "Velocity Mod Target", "Vel Dest", modTargetCount(), nullptr, choiceValue (0, modTargetCount()), false };
    d[P_LFO_DEST] = ParamDef { "lfoDest", "LFO Target", "Target", modTargetCount(), nullptr, choiceValue (0, modTargetCount()), false };
    d[P_VEL_DEST].choices = d[P_LFO_DEST].choices = targetNames();
    return d;
}

const ParamDef& paramDef (int p) { return defs()[p]; }

//---------------------------------------------------------------------------------------------
static std::string fmt (const char* f, double v) { char b[64]; std::snprintf (b, sizeof b, f, v); return b; }
static std::string noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[((n % 12) + 12) % 12]) + std::to_string (n / 12 - 2); // C3 = 60
}
static std::string hz (double f)
{
    if (f >= 1000.0) return fmt (f >= 10000.0 ? "%.1f kHz" : "%.2f kHz", f / 1000.0);
    if (f >= 100.0) return fmt ("%.0f Hz", f);
    return fmt ("%.1f Hz", f);
}
static std::string ms (double blocks)
{
    const double s = blocks * (double) lxr::kLxrBlock / (double) lxr::kLxrFs;
    if (! std::isfinite (s) || s > 60.0) return "inf";
    if (s >= 1.0) return fmt ("%.2f s", s);
    return fmt ("%.0f ms", s * 1000.0);
}
static double blocksFor (double inc) { return inc <= 0.0 ? INFINITY : std::ceil (1.0 / inc); }

std::string formatValue (int p, float v)
{
    const auto& d = paramDef (p);
    if (d.numChoices > 0) return d.choices[choiceIndex (v, d.numChoices)];
    const float d2 = v * 127.f;
    switch (p)
    {
        case P_OSC_COARSE: case P_MOD_COARSE: case P_MOD2_COARSE:
        {
            const float note = d2;
            return hz (lxr::midiNoteHz (note)) + "  " + noteName ((int) std::lround (note));
        }
        case P_OSC_FINE: return fmt ("%+.0f ct", 1200.0 * std::log2 (lxr::calcDetune (d2)));
        case P_NOISE_FREQ: return hz (v * 22000.0);
        case P_NOISE_MIX: return fmt ("%.0f%% noise", v * 100.0);
        case P_PITCH_AMT: return fmt ("+%.1f st", 12.0 * std::log2 (1.0 + v * v * 32.0));
        case P_PITCH_DECAY: return ms (blocksFor (lxr::DecayEg::calcTime (d2)));
        case P_PITCH_SLOPE: case P_SLOPE:
        {
            const double s = (v - 0.5) * 200.0;
            return std::fabs (s) < 0.5 ? std::string ("linear") : fmt ("%+.0f", s);
        }
        case P_ATTACK:
        {
            const double inc = lxr::SlopeEg2::calcTime (d2, lxr::SlopeEg2::kAttackK);
            return inc >= 1.0 ? std::string ("0 ms") : ms (blocksFor (inc));
        }
        case P_DECAY: return ms (blocksFor (lxr::SlopeEg2::decayFor (d2)));
        case P_REPEAT: return std::to_string ((int) std::lround (v * v * 127.0));
        case P_FILTER_FREQ: return hz (lxr::valueShaperF2F (v, -0.9f) * 0.45 * lxr::kLxrFs);
        case P_TRANS_FREQ: return fmt ("%.2fx", 1.0 + (d2 / 33.9 - 0.75));
        case P_DECIMATE:
        {
            const double r = lxr::valueShaperF2F (v, -0.7f);
            return r >= 0.999 ? std::string ("full") : hz (r * lxr::kLxrFs);
        }
        case P_LEVEL: return v <= 0.0001f ? std::string ("-inf dB") : fmt ("%.1f dB", 20.0 * std::log10 (v));
        case P_PAN:
        {
            const int pan = (int) std::lround ((v - 0.5) * 127.0);
            return pan == 0 ? std::string ("C") : (pan < 0 ? "L " + std::to_string (-pan) : "R " + std::to_string (pan));
        }
        case P_LFO_FREQ:
        {
            double f = 0.0078125 + v * (1.0 - 0.0078125);
            return hz (f * f * f * 200.0);
        }
        case P_LFO_PHASE: return fmt ("%.0f deg", v * 360.0);
        case P_GHOST: return fmt ("%.0f%% depth", v * 200.0);
        default: return fmt ("%.0f%%", v * 100.0);
    }
}

bool parseValue (int p, const std::string& text, float& out)
{
    const auto& d = paramDef (p);
    std::string t;
    for (char c : text) t += (char) std::tolower ((unsigned char) c);
    if (d.numChoices > 0)
    {
        for (int i = 0; i < d.numChoices; ++i)
        {
            std::string n;
            for (const char* c = d.choices[i]; *c; ++c) n += (char) std::tolower ((unsigned char) *c);
            if (n == t) { out = choiceValue (i, d.numChoices); return true; }
        }
        return false;
    }
    char* end = nullptr;
    double num = std::strtod (t.c_str(), &end);
    if (end == t.c_str()) return false;
    const bool k = t.find ('k') != std::string::npos;
    if (k) num *= 1000.0;
    auto inv = [] (auto fn, double target) // bisection on a monotonic display mapping
    {
        double lo = 0, hi = 1;
        const bool up = fn (1.0) > fn (0.0);
        for (int i = 0; i < 50; ++i) { const double m = 0.5 * (lo + hi); if ((fn (m) < target) == up) lo = m; else hi = m; }
        return (float) (0.5 * (lo + hi));
    };
    switch (p)
    {
        case P_OSC_COARSE: case P_MOD_COARSE: case P_MOD2_COARSE:
            if (t.find ("hz") != std::string::npos || k || num > 127.0)
                out = (float) std::clamp ((12.0 * std::log2 (num / 8.1757989156)) / 127.0, 0.0, 1.0);
            else out = (float) std::clamp (num / 127.0, 0.0, 1.0);
            return true;
        case P_FILTER_FREQ:
            out = inv ([] (double x) { return (double) lxr::valueShaperF2F ((float) x, -0.9f) * 0.45 * lxr::kLxrFs; }, num);
            return true;
        case P_NOISE_FREQ: out = (float) std::clamp (num / 22000.0, 0.0, 1.0); return true;
        case P_LEVEL: out = (float) std::clamp (std::pow (10.0, num / 20.0), 0.0, 1.0); return true;
        case P_PAN: out = (float) std::clamp (0.5 + (t[0] == 'l' ? -num : num) / 127.0, 0.0, 1.0); return true;
        case P_LFO_FREQ:
            out = inv ([] (double x) { double f = 0.0078125 + x * (1.0 - 0.0078125); return f * f * f * 200.0; }, num);
            return true;
        case P_REPEAT: out = (float) std::sqrt (std::clamp (num / 127.0, 0.0, 1.0)); return true;
        case P_PITCH_SLOPE: case P_SLOPE: out = (float) std::clamp (num / 200.0 + 0.5, 0.0, 1.0); return true;
        case P_LFO_PHASE: out = (float) std::clamp (num / 360.0, 0.0, 1.0); return true;
        case P_GHOST: out = (float) std::clamp (num / 200.0, 0.0, 1.0); return true;
        default:
            out = (float) std::clamp (num / 100.0, 0.0, 1.0); return true;
    }
}

bool isUsedByModel (int p, Model m)
{
    switch (p)
    {
        case P_MOD_WAVE: case P_MOD_COARSE: case P_MOD_GAIN:
            return m != Model::Snare;
        case P_MOD2_WAVE: case P_MOD2_COARSE: case P_MOD2_GAIN:
            return m == Model::Cymbal || m == Model::HiHat;
        case P_FM_MODE:
            return m == Model::Drum;
        case P_NOISE_FREQ: case P_NOISE_MIX:
            return m == Model::Snare;
        case P_PITCH_AMT: case P_PITCH_DECAY: case P_PITCH_SLOPE:
            return m == Model::Drum || m == Model::Snare;
        default: return true;
    }
}

//---------------------------------------------------------------------------------------------
// Init patches taken from the LXR-02 init kit (initKit.c): voice 1 for Drum, 4 Snare, 5 Cymbal,
// 6 Hi-hat. LXR values are 0..127 MIDI units.
Patch defaultPatch (Model m)
{
    Patch p {};
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i) p[(size_t) i] = paramDef (i).defaultValue;
    auto set = [&p] (int id, float v127) { p[(size_t) id] = v127 / 127.f; };
    auto ch  = [&p] (int id, int idx) { p[(size_t) id] = choiceValue (idx, paramDef (id).numChoices); };
    ch (P_MODEL, (int) m);
    set (P_DRIVE, 63); set (P_LEVEL, 127); set (P_PAN, 63.5f);
    set (P_TRANS_FREQ, 0); set (P_FILTER_FREQ, 127);
    switch (m)
    {
        case Model::Drum:
            ch (P_OSC_WAVE, lxr::WAVE_SINE); set (P_OSC_COARSE, 31); set (P_OSC_FINE, 63.5f);
            ch (P_MOD_WAVE, lxr::WAVE_SINE); set (P_MOD_COARSE, 0); set (P_MOD_GAIN, 0);
            set (P_PITCH_DECAY, 26); set (P_PITCH_AMT, 44); set (P_PITCH_SLOPE, 22);
            set (P_ATTACK, 0); set (P_DECAY, 21); set (P_SLOPE, 46);
            break;
        case Model::Snare:
            ch (P_OSC_WAVE, lxr::WAVE_TRI); set (P_OSC_COARSE, 45); set (P_OSC_FINE, 64);
            set (P_NOISE_FREQ, 127); set (P_NOISE_MIX, 61);
            set (P_PITCH_DECAY, 14); set (P_PITCH_AMT, 81); set (P_PITCH_SLOPE, 15);
            set (P_ATTACK, 0); set (P_DECAY, 10); set (P_SLOPE, 10);
            break;
        case Model::Cymbal:
            ch (P_OSC_WAVE, lxr::WAVE_NOISE); set (P_OSC_COARSE, 127); set (P_OSC_FINE, 63.5f);
            ch (P_MOD_WAVE, lxr::WAVE_SINE); set (P_MOD_COARSE, 0); set (P_MOD_GAIN, 0);
            ch (P_MOD2_WAVE, lxr::WAVE_SINE); set (P_MOD2_COARSE, 0); set (P_MOD2_GAIN, 0);
            set (P_ATTACK, 0); set (P_DECAY, 107); set (P_SLOPE, 0);
            ch (P_FILTER_TYPE, 1); set (P_FILTER_FREQ, 68);
            break;
        case Model::HiHat:
            ch (P_OSC_WAVE, lxr::WAVE_SINE); set (P_OSC_COARSE, 50); set (P_OSC_FINE, 63.5f);
            ch (P_MOD_WAVE, lxr::WAVE_SINE); set (P_MOD_COARSE, 56); set (P_MOD_GAIN, 127);
            ch (P_MOD2_WAVE, lxr::WAVE_SINE); set (P_MOD2_COARSE, 127); set (P_MOD2_GAIN, 127);
            set (P_ATTACK, 0); set (P_DECAY, 5); set (P_SLOPE, 6);
            ch (P_FILTER_TYPE, 1); set (P_FILTER_FREQ, 104);
            break;
        default: break;
    }
    return p;
}
} // namespace ferrum
