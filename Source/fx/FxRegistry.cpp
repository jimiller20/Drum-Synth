// FxRegistry.cpp - module descriptions, parameter formatting and the module factory
// SPDX-License-Identifier: GPL-3.0-only
#include "FxModule.h"
#include "FxFactories.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ferrum::fx
{
static ParamSpec pct (const char* n, float def) { ParamSpec s { n, def }; s.lo = 0; s.hi = 100; s.unit = "%"; return s; }
static ParamSpec lin (const char* n, float def, float lo, float hi, const char* unit, bool bipolar = false)
{
    ParamSpec s { n, def }; s.lo = lo; s.hi = hi; s.unit = unit; s.bipolar = bipolar; return s;
}
static ParamSpec lg (const char* n, float def, float lo, float hi, const char* unit)
{
    ParamSpec s { n, def, Scale::Log }; s.lo = lo; s.hi = hi; s.unit = unit; return s;
}
static ParamSpec ch (const char* n, int defIdx, std::vector<const char*> c)
{
    ParamSpec s { n, 0.f, Scale::Choice };
    s.choices = std::move (c);
    s.def = ((float) defIdx + 0.5f) / (float) s.choices.size();
    s.unit = "";
    return s;
}
static ParamSpec offMin (ParamSpec s) { s.offAtMin = true; return s; }
static ParamSpec offMax (ParamSpec s) { s.offAtMax = true; return s; }
static ParamSpec mix (float def = 1.f) { return pct ("Mix", def); }

static const std::vector<ModuleInfo>& table()
{
    static const std::vector<ModuleInfo> t = {
        { Type::None, "none", "Empty", "", "An empty slot.", {} },
        { Type::Ferric, "ferric", "Ferric", "Drive",
          "Vintage saturation after Felt Instruments' Brut: six preamp characters, a resonant highpass or a bandpass "
          "filter, an analog-style feedback loop around the preamp and a tilt EQ at the end. Feedback adds bloom, sustain "
          "and grit to the drive (more gain at low levels, compression as it saturates); above 80 % a little resonance sings "
          "at the Cutoff frequency.",
          { ch ("Preamp", 0, { "Glow", "Lab", "Dark", "Meter", "Germanium", "Soviet" }), lin ("Drive", 0.35f, 0, 36, "dB"),
            ch ("Filter", 0, { "Off", "Resonant HP", "Bandpass" }), lg ("Cutoff", 0.35f, 20, 12000, "Hz"), pct ("Resonance", 0.3f),
            pct ("Feedback", 0.f), lin ("Tilt", 0.5f, -6, 6, "dB", true), mix() } },
        { Type::Amp, "amp", "Amp", "Drive",
          "A tube amplifier: up to three preamp stages, a passive tone stack and a power amp that sags under load. "
          "Follow it with Cabinet.",
          { ch ("Model", 1, { "Clean", "Crunch", "Lead", "Bass" }), pct ("Gain", 0.5f), lin ("Bass", 0.5f, -12, 12, "dB", true),
            lin ("Mid", 0.5f, -12, 12, "dB", true), lin ("Treble", 0.5f, -12, 12, "dB", true), lin ("Presence", 0.3f, 0, 8, "dB"),
            pct ("Sag", 0.3f), lin ("Master", 0.6667f, -24, 12, "dB") } },
        { Type::Cabinet, "cabinet", "Cabinet", "Tone",
          "Speaker cabinet and microphone: resonance, cone break-up and roll-off of real boxes, mic position and distance.",
          { ch ("Cabinet", 1, { "1x12 Open", "2x12 Combo", "4x12 Closed", "4x10 Bass", "1x15 Bass", "Small Radio", "Telephone" }),
            pct ("Mic Edge", 0.3f), pct ("Distance", 0.2f), offMin (lg ("Low Cut", 0.f, 20, 400, "Hz")),
            offMax (lg ("High Cut", 1.f, 1500, 20000, "Hz")), mix() } },
        { Type::Tape, "tape", "Tape", "Drive",
          "A tape machine: soft magnetic saturation, head bump, high-frequency loss with speed and age, wow, flutter and hiss.",
          { lin ("Drive", 0.35f, 0, 24, "dB"), pct ("Bias", 0.3f), ch ("Speed", 1, { "7.5 ips", "15 ips", "30 ips" }), pct ("Wow", 0.15f),
            pct ("Flutter", 0.15f), pct ("Hiss", 0.1f), pct ("Age", 0.2f), mix() } },
        { Type::Glue, "glue", "Glue", "Dynamics",
          "Analog bus compressor: soft knee, side-chain high-pass and program-dependent auto release. Glues a kit together.",
          { lin ("Threshold", 0.6f, -40, 0, "dB"), ch ("Ratio", 0, { "2:1", "4:1", "10:1" }),
            ch ("Attack", 4, { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }), ch ("Release", 4, { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }),
            lin ("Makeup", 0.2f, 0, 20, "dB"), ch ("SC HPF", 2, { "Off", "60 Hz", "90 Hz", "150 Hz" }), mix() } },
        { Type::Shaper, "shaper", "Shaper", "Dynamics",
          "Transient shaper: more or less attack and sustain, independent of level.",
          { lin ("Attack", 0.5f, -100, 100, "%", true), lin ("Sustain", 0.5f, -100, 100, "%", true), ch ("Speed", 1, { "Fast", "Medium", "Slow" }),
            lin ("Output", 0.5f, -12, 12, "dB", true), mix() } },
        { Type::Ladder, "ladder", "Ladder", "Tone",
          "A 4-pole transistor ladder filter with input drive, self-oscillating resonance and an envelope follower on the cutoff.",
          { ch ("Mode", 0, { "LP 24", "LP 12", "Bandpass", "HP 12" }), lg ("Cutoff", 0.75f, 20, 18000, "Hz"), pct ("Resonance", 0.2f),
            lin ("Drive", 0.25f, 0, 24, "dB"), lin ("Env Amount", 0.5f, -100, 100, "%", true), ch ("Env Speed", 1, { "Fast", "Medium", "Slow" }), mix() } },
        { Type::Eq, "eq", "Program EQ", "Tone",
          "A passive program equalizer with the classic low trick: boost and attenuate the same low frequency at once "
          "for a tight, deep bottom. Gentle tube make-up stage.",
          { ch ("Low Freq", 2, { "20 Hz", "30 Hz", "60 Hz", "100 Hz" }), lin ("Low Boost", 0.f, 0, 10, ""), lin ("Low Atten", 0.f, 0, 10, ""),
            ch ("High Freq", 4, { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }), lin ("High Boost", 0.f, 0, 10, ""),
            pct ("Bandwidth", 0.5f), lin ("High Atten", 0.f, 0, 10, ""), lin ("Output", 0.5f, -12, 12, "dB", true) } },
        { Type::Echo, "echo", "Echo", "Space",
          "Bucket-brigade echo: darkening repeats, gentle saturation in the loop, clock wobble and noise as it ages.",
          { ch ("Sync", 5, { "Free", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2" }), lg ("Time", 0.55f, 20, 1200, "ms"),
            lin ("Feedback", 0.364f, 0, 110, "%"), lg ("Tone", 0.5f, 800, 12000, "Hz"), pct ("Mod", 0.2f), pct ("Age", 0.2f),
            ch ("Ping-Pong", 0, { "Off", "On" }), mix (0.3f) } },
        { Type::Reverb, "reverb", "Reverb", "Space",
          "Algorithmic reverb (8-line feedback delay network): plate, room, hall and short ambience.",
          { ch ("Type", 0, { "Plate", "Room", "Hall", "Ambience" }), pct ("Size", 0.5f), lg ("Decay", 0.45f, 0.2f, 12, "s"), pct ("Damping", 0.4f),
            lin ("Pre-delay", 0.05f, 0, 200, "ms"), pct ("Mod", 0.3f), pct ("Width", 1.f), mix (0.25f) } },
        { Type::Spring, "spring", "Spring", "Space",
          "Spring reverb tank: dispersive chirps, drip and boing. Lovely on snares and percussion.",
          { ch ("Springs", 1, { "1", "2", "3" }), lg ("Decay", 0.4f, 0.3f, 6, "s"), pct ("Tension", 0.5f), pct ("Drip", 0.5f),
            lg ("Tone", 0.5f, 1500, 9000, "Hz"), mix (0.3f) } },
        { Type::Chorus, "chorus", "Chorus", "Modulation",
          "Bucket-brigade chorus and ensemble: the classic two-mode chorus plus a three-voice ensemble.",
          { ch ("Mode", 0, { "I", "II", "I+II", "Ensemble" }), lg ("Rate", 0.4f, 0.05f, 8, "Hz"), pct ("Depth", 0.5f), pct ("Width", 1.f),
            lg ("Tone", 0.6f, 2000, 16000, "Hz"), mix (0.5f) } },
        { Type::Crusher, "crusher", "Crusher", "Drive",
          "Bit depth and sample-rate reduction with clock jitter.",
          { lin ("Bits", 0.3f, 16, 2, "bit"), offMax (lg ("Rate", 1.f, 500, 48000, "Hz")), pct ("Jitter", 0.f),
            offMax (lg ("Tone", 1.f, 1000, 20000, "Hz")), mix() } },
        { Type::Utility, "utility", "Utility", "Utility",
          "Gain, pan, stereo width, low / high cut, mono bass and polarity.",
          { lin ("Gain", 0.5f, -24, 24, "dB", true), lin ("Pan", 0.5f, -100, 100, "", true), lin ("Width", 0.5f, 0, 200, "%"),
            offMin (lg ("Low Cut", 0.f, 10, 500, "Hz")), offMax (lg ("High Cut", 1.f, 1000, 22000, "Hz")), offMin (lg ("Mono Below", 0.f, 10, 300, "Hz")),
            ch ("Polarity", 0, { "Normal", "Invert L", "Invert R", "Invert Both" }) } },
        { Type::LxrDist, "lxrdist", "LXR Distortion", "LXR-02",
          "The LXR-02's own FX-bus distortion: tube (ECC83 curve), wave folder or the rat-style diode clipper.",
          { ch ("Type", 0, { "Tube", "Fold", "Rat" }), pct ("Drive", 0.5f), pct ("Color", 0.5f), pct ("Tone", 0.5f), pct ("Volume", 0.5f), mix (0.5f) } },
        { Type::LxrRing, "lxrring", "LXR Ring Mod", "LXR-02",
          "The LXR-02 ring modulator.",
          { lin ("Freq", 0.5f, 0, 880, "Hz"), ch ("Wave", 3, { "Sine", "Tri", "Saw", "Square", "Noise", "PWM" }), mix (0.5f) } },
        { Type::LxrComp, "lxrcomp", "LXR Compressor", "LXR-02",
          "The LXR-02 compressor.",
          { lin ("Attack", 0.1f, 0.03f, 100.03f, "ms"), lin ("Release", 0.3f, 25, 200, "ms"), lin ("Threshold", 0.7f, -96, 0, "dB"),
            ch ("Ratio", 2, { "1:1", "2:1", "3:1", "4:1", "6:1", "8:1" }), pct ("Gain", 0.f), mix (1.f) } },
        { Type::LxrDelay, "lxrdelay", "LXR Delay", "LXR-02",
          "The LXR-02 delay with its half-precision delay line and ping-pong mode.",
          { pct ("Time", 0.5f), pct ("Range", 0.5f), pct ("Tone", 0.5f), pct ("Feedback", 0.5f), ch ("Ping-Pong", 0, { "Off", "On" }), mix (0.5f) } },
    };
    return t;
}

const ModuleInfo& info (Type t)
{
    const auto& tb = table();
    const int i = (int) t;
    return tb[(size_t) (i < 0 || i >= (int) tb.size() ? 0 : i)];
}

Type typeFromId (const std::string& id)
{
    for (const auto& m : table()) if (id == m.id) return m.type;
    return Type::None;
}

int choiceIndex (const ParamSpec& s, float norm)
{
    const int n = (int) s.choices.size();
    if (n <= 0) return 0;
    const int i = (int) (norm * (float) n);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

float choiceNorm (const ParamSpec& s, int index)
{
    const int n = std::max (1, (int) s.choices.size());
    return ((float) std::clamp (index, 0, n - 1) + 0.5f) / (float) n;
}

float paramValue (const ParamSpec& s, float norm)
{
    norm = std::clamp (norm, 0.f, 1.f);
    switch (s.scale)
    {
        case Scale::Log: return s.lo * std::pow (s.hi / s.lo, norm);
        case Scale::Choice: return (float) choiceIndex (s, norm);
        case Scale::Linear:
        default: return s.lo + (s.hi - s.lo) * norm;
    }
}

float normFromValue (const ParamSpec& s, float v)
{
    switch (s.scale)
    {
        case Scale::Log: return std::clamp (std::log (std::max (v, s.lo) / s.lo) / std::log (s.hi / s.lo), 0.f, 1.f);
        case Scale::Choice: return choiceNorm (s, (int) std::lround (v));
        case Scale::Linear:
        default: return s.hi != s.lo ? std::clamp ((v - s.lo) / (s.hi - s.lo), 0.f, 1.f) : 0.f;
    }
}

bool parseParam (const ParamSpec& s, const std::string& text, float& norm)
{
    std::string t;
    for (char c : text) if (c != ' ') t += (char) std::tolower ((unsigned char) c);
    if (t.empty()) return false;
    if (s.scale == Scale::Choice)
    {
        for (size_t i = 0; i < s.choices.size(); ++i)
        {
            std::string c;
            for (const char* q = s.choices[i]; *q != 0; ++q) if (*q != ' ') c += (char) std::tolower ((unsigned char) *q);
            if (c == t) { norm = choiceNorm (s, (int) i); return true; }
        }
        return false;
    }
    if (t == "off" && (s.offAtMin || s.offAtMax)) { norm = s.offAtMin ? 0.f : 1.f; return true; }
    char* end = nullptr;
    double v = std::strtod (t.c_str(), &end);
    if (end == t.c_str()) return false;
    const std::string rest (end);
    if (rest.rfind ("k", 0) == 0) v *= 1000.0; // "2.5k", "2.5khz"
    norm = normFromValue (s, (float) v);
    return true;
}

int paramIndex (Type t, const char* name)
{
    const auto& ps = info (t).params;
    for (size_t i = 0; i < ps.size(); ++i) if (std::string (ps[i].name) == name) return (int) i;
    return -1;
}

std::string formatParam (const ParamSpec& s, float norm)
{
    if (s.scale == Scale::Choice) return s.choices.empty() ? std::string() : s.choices[(size_t) choiceIndex (s, norm)];
    if (s.offAtMin && norm < 0.005f) return "Off";
    if (s.offAtMax && norm > 0.995f) return "Off";
    const float v = paramValue (s, norm);
    char b[48];
    const std::string u = s.unit;
    if (u == "Hz")
    {
        if (v >= 1000.f) std::snprintf (b, sizeof b, v >= 10000.f ? "%.1f kHz" : "%.2f kHz", v / 1000.f);
        else std::snprintf (b, sizeof b, v < 100.f ? "%.1f Hz" : "%.0f Hz", v);
    }
    else if (u == "dB") std::snprintf (b, sizeof b, "%+.1f dB", v);
    else if (u == "ms") std::snprintf (b, sizeof b, v < 100.f ? "%.1f ms" : "%.0f ms", v);
    else if (u == "s") std::snprintf (b, sizeof b, "%.2f s", v);
    else if (u == "%") std::snprintf (b, sizeof b, s.bipolar ? "%+.0f%%" : "%.0f%%", v);
    else if (u == "bit") std::snprintf (b, sizeof b, "%.1f bit", v);
    else if (s.bipolar) std::snprintf (b, sizeof b, "%+.0f", v);
    else std::snprintf (b, sizeof b, "%.1f", v);
    return b;
}

std::unique_ptr<Module> create (Type t)
{
    switch (t)
    {
        case Type::Ferric:   return makeFerric();
        case Type::Amp:      return makeAmp();
        case Type::Cabinet:  return makeCabinet();
        case Type::Tape:     return makeTape();
        case Type::Glue:     return makeGlue();
        case Type::Shaper:   return makeShaper();
        case Type::Ladder:   return makeLadder();
        case Type::Eq:       return makeEq();
        case Type::Echo:     return makeEcho();
        case Type::Reverb:   return makeReverb();
        case Type::Spring:   return makeSpring();
        case Type::Chorus:   return makeChorus();
        case Type::Crusher:  return makeCrusher();
        case Type::Utility:  return makeUtility();
        case Type::LxrDist:  return makeLxr (Type::LxrDist);
        case Type::LxrRing:  return makeLxr (Type::LxrRing);
        case Type::LxrComp:  return makeLxr (Type::LxrComp);
        case Type::LxrDelay: return makeLxr (Type::LxrDelay);
        default: return nullptr;
    }
}
} // namespace ferrum::fx
