// FxModule.h - one effect of the modular FX rack. Every module works in place on a stereo
// block (full scale = 1.0) and reads up to kMaxParams normalised parameters (0..1) per block.
// Modules are created and prepared on the message thread, then handed to the audio thread.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace ferrum::fx
{
constexpr int kMaxParams = 8;

enum class Type : int
{
    None = 0,
    Ferric,      // vintage saturation: six preamps, resonant HP / bandpass filter, feedback loop, tilt (after Felt Instruments' Brut)
    Amp,         // guitar / bass amp: preamp stages, tone stack, power amp sag
    Cabinet,     // speaker cabinet + mic
    Tape,        // tape machine: saturation, head bump, wow and flutter, hiss
    Glue,        // analog bus compressor
    Shaper,      // transient shaper
    Ladder,      // 4-pole ladder filter with drive and envelope
    Eq,          // passive-style program EQ (boost / attenuate trick)
    Echo,        // bucket-brigade echo
    Reverb,      // plate / room / hall / ambience
    Spring,      // spring reverb tank
    Chorus,      // BBD chorus / ensemble
    Crusher,     // bit / sample-rate reduction
    Utility,     // gain, pan, width, filters, mono bass
    LxrDist,     // the LXR-02 FX bus distortion (tube / fold / rat)
    LxrRing,     // the LXR-02 ring modulator
    LxrComp,     // the LXR-02 compressor
    LxrDelay,    // the LXR-02 delay (fp16 line, ping-pong)
    Count
};

enum class Scale { Linear, Log, Choice };

struct ParamSpec
{
    const char* name;
    float def;                         // normalised default
    Scale scale = Scale::Linear;
    float lo = 0.f, hi = 1.f;          // display range (Linear / Log)
    const char* unit = "%";            // "%", "dB", "Hz", "ms", "s", "x", "" ...
    std::vector<const char*> choices {}; // Scale::Choice
    bool bipolar = false;              // drawn from the centre
    bool offAtMin = false, offAtMax = false; // e.g. a low cut that is "Off" at the bottom of its range
};

struct ModuleInfo
{
    Type type;
    const char* id;          // stable id for files
    const char* name;        // shown in the rack
    const char* category;    // "Drive", "Tone", "Dynamics", "Space", "Modulation", "Utility", "LXR-02"
    const char* description;
    std::vector<ParamSpec> params;
};

const ModuleInfo& info (Type t);
Type typeFromId (const std::string& id);
float paramValue (const ParamSpec& s, float norm);     // display units
int choiceIndex (const ParamSpec& s, float norm);
float choiceNorm (const ParamSpec& s, int index);
std::string formatParam (const ParamSpec& s, float norm);
float normFromValue (const ParamSpec& s, float value);             // inverse of paramValue
bool parseParam (const ParamSpec& s, const std::string& text, float& norm); // typed text -> normalised
int paramIndex (Type t, const char* name);                          // -1 if the module has no such parameter

class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (double sampleRate) = 0;   // allocate here (message thread, never the audio thread)
    virtual void reset() = 0;
    virtual void process (float* L, float* R, int n, const float* params) = 0;
    void setTempo (double b) { bpm = b > 0 ? b : 120.0; }

protected:
    double sr = 44100.0, bpm = 120.0;
};

std::unique_ptr<Module> create (Type t);
} // namespace ferrum::fx
