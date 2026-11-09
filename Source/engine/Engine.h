// Engine.h - the complete instrument without any framework: 8 LXR voices, choke groups, mutes,
// two stereo outputs, the modular FX rack, MIDI input mapping and the Microtonic-style pattern sequencer.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../dsp/LxrVoice.h"
#include "../dsp/FxBus.h"
#include "../fx/FxRack.h"
#include "Modulation.h"
#include "Patterns.h"
#include "Groove.h"
#include <atomic>
#include <vector>

namespace ferrum
{
enum class StepRate : int { Eighth = 0, EighthT, Sixteenth, SixteenthT, ThirtySecond, Count };
enum class PatternLaunch : int { SwitchNext = 0, SwitchDirectly, Retrigger, RetriggerGated };
enum class SeqState : int { Stopped = 0, WaitingStart, Playing, WaitingStop };

struct Transport
{
    bool hostHasTransport = false; // false in the standalone app: play runs the internal clock
    bool hostPlaying = false;
    double ppq = 0.0;              // host song position in quarter notes
    double bpm = 120.0;
};

struct MidiIn { int offset; uint8_t status, data1, data2; };
struct MidiOut { int offset; uint8_t status, data1, data2; };

struct MidiConfig
{
    PatternLaunch launch = PatternLaunch::SwitchNext;
    bool pitchedMode = false;        // MIDI channels 1-8 play the drum channels chromatically
    bool noteOffStops = false;       // note off fades the voice ("Stop voices with MIDI note off")
    bool transmitPatternNotes = true;
    int  pitchBendSemis = 12;        // 0 = off
    int  transmitChannel = 0;        // 0-based
};

struct Globals
{
    bool playOn = false;
    int selectedPattern = 0;
    StepRate rate = StepRate::Sixteenth;
    float swing = 0.f;               // 0..1
    float fillRate = 2.f;            // hits per step, 2..8, fractional allowed
    float master = 1.f;              // linear gain
    double tempo = 120.0;            // used when the host gives none
    bool mutes[kNumChannels] {};
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();

    void setPatchParam (int ch, int p, float v) { voices[(size_t) ch].setParam (p, v); }
    float getPatchParam (int ch, int p) const { return voices[(size_t) ch].getParam (p); }
    void setGlobals (const Globals& g);
    void setMidiConfig (const MidiConfig& c) { midiCfg = c; }

    // Output buffers. A must be valid; any other pair may be null (inactive in the host): what would
    // go there is mixed into A instead. chL/chR are the channels' own outputs ("Own"), fxL/fxR the
    // FX buses' own outputs.
    struct Outputs
    {
        float* aL = nullptr; float* aR = nullptr;
        float* bL = nullptr; float* bR = nullptr;
        std::array<float*, kNumChannels> chL {}, chR {};
        std::array<float*, 2> fxL {}, fxR {};
    };
    void process (const Outputs& out, int n, const Transport& t, const std::vector<MidiIn>& midiIn, std::vector<MidiOut>* midiOut);
    // A and B only (tests, offline rendering)
    void process (float* aL, float* aR, float* bL, float* bR, int n, const Transport& t,
                  const std::vector<MidiIn>& midiIn, std::vector<MidiOut>* midiOut)
    {
        Outputs o; o.aL = aL; o.aR = aR; o.bL = bL; o.bR = bR;
        process (o, n, t, midiIn, midiOut);
    }

    // thread-safe calls from the UI
    void previewTrigger (int ch, float velocity);
    void requestImmediateStart() { immediateStart.store (true); }
    void requestImmediateStop() { immediateStop.store (true); }

    PatternBank patterns;
    fx::FxRack rack;            // FX 1, FX 2 and Master buses (edit from the message thread)
    mod::Matrix mods;           // modulators and their routes (setState from the message thread)

    // state published for the UI
    std::atomic<int> uiPlayingPattern { 0 }, uiStep { -1 }, uiState { 0 }, uiSelectedPattern { 0 };
    std::atomic<uint32_t> uiTriggerCount[kNumChannels] {};
    std::atomic<int> uiPlayRequest { 0 };      // +1 / -1 when MIDI wants the play parameter changed
    std::atomic<int> uiPatternRequest { -1 };  // pattern chosen by a MIDI note
    std::atomic<bool> uiMidiMute[kNumChannels] {};
    std::atomic<uint32_t> uiPatternStarts { 0 };   // counts pattern (re)starts: drives Evolve
    std::atomic<int> uiLastStartedPattern { 0 };

    static double stepLengthPpq (StepRate r);
    static double swingWarp (double ppq, float swing);

private:
    struct Ev { double ppq; int ch; float vel; int step; int pattern; Articulation art {}; };
    struct Trig { int offset; int ch; float vel; int note; bool fromSequencer; bool preview = false; Articulation art {}; };

    void handleMidi (const MidiIn& m, double ppqAtOffset, std::vector<Trig>& trigs);
    void runSequencer (int n, const Transport& t, std::vector<Trig>& trigs);
    void scheduleUntil (double ppq1, double ppqPerSecond);
    void resync (double ppq);
    void startAt (double ppq);
    void doTrigger (const Trig& tr, int sampleIndexInBlock);
    bool isMuted (int ch) const { return globals.mutes[ch] || midiMute[ch]; }

    std::array<Voice, kNumChannels> voices;
    Globals globals;
    MidiConfig midiCfg;
    double sr = 44100.0;

    // sequencer
    SeqState state = SeqState::Stopped;
    bool lastPlayOn = false;
    int lastSelected = 0;
    int curPattern = 0, reqPattern = 0;
    bool pendingSwitch = false;
    double anchor = 0.0, nextStepPpq = 0.0;
    int nextStep = 0;
    bool usingInternal = false;
    double internalPpq = 0.0;
    double expectedPpq = -1.0;
    bool lastHostPlaying = false;
    std::vector<Ev> pending;
    std::atomic<bool> immediateStart { false }, immediateStop { false };
    bool midiArmed = false;
    int cycles[kNumPatterns] {};               // times each pattern started since play (CHANCE conditions)
    float timeDrift[kNumChannels] {};          // humanize: slowly wandering timing per channel
    lxr::Rng seqRng;                           // chance / humanize
    float rand01() { return (float) (seqRng.next() >> 8) * (1.0f / 16777216.0f); }
    int gatedPatternNote = -1;

    // MIDI state
    bool midiMute[kNumChannels] {};
    bool muteKeyHeld[kNumChannels] {};
    bool sustain = false;
    std::vector<std::pair<int, int>> pendingNoteOffs; // samples left, note

    // UI trigger FIFO (single producer: message thread)
    struct Preview { int ch; float vel; };
    std::array<Preview, 64> previewFifo {};
    std::atomic<int> previewWrite { 0 }, previewRead { 0 };

    // modulation
    std::array<float, kNumChannels> chLevel {};                          // peak of each channel in the last control block
    float mainLevel = 0.f;                                              // peak of output A in the last audio block
    std::array<std::array<float, NUM_PATCH_PARAMS>, kNumChannels> modApplied {};
    bool modWasOn = false;
    std::vector<std::array<float, mod::kMods>> modVals;                 // modulator outputs per control block (FX routes)

    // scratch
    std::vector<float> mono, busL[2], busR[2];
    std::vector<Trig> trigs;
    long long sampleClock = 0;
    long long lastChokeTriggerSample = -1;
    int lastChokeTriggerChannel = -1;
};
} // namespace ferrum
