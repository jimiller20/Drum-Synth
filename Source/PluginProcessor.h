// PluginProcessor.h - JUCE wrapper: host parameters, state, presets, morph, undo, exports.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/Engine.h"
#include "engine/Kits.h"
#include "engine/Randomizer.h"
#include <optional>

class Ferrum8Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int NC = ferrum::kNumChannels;
    static constexpr int NP = ferrum::NUM_PATCH_PARAMS;

    Ferrum8Processor();
    ~Ferrum8Processor() override;

    //== AudioProcessor ==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ferrum 8"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return presetName; }
    void changeProgramName (int, const juce::String& n) override { presetName = n; }
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== editor API (message thread) =============================================================
    ferrum::Engine& getEngine() { return engine; }
    juce::AudioProcessorValueTreeState apvts;

    juce::RangedAudioParameter* patchParam (int ch, int p) const { return patchParams[(size_t) ch][(size_t) p]; }
    static juce::String patchParamId (int ch, int p);
    float getPatchValue (int ch, int p) const;                 // current value, patch-normalised
    void setPatchValue (int ch, int p, float v);                // as if the user turned the knob
    ferrum::Patch getCurrentPatch (int ch) const;
    void setPatch (int ch, const ferrum::Patch& p, bool alsoMorphTarget = true, bool keepRouting = false);
    void setMorphTarget (int ch, const ferrum::Patch& b);       // only end point B
    ferrum::Patch getEndA (int ch) const { const juce::ScopedLock sl (stateLock); return chans[(size_t) ch].a; }
    ferrum::Patch getEndB (int ch) const { const juce::ScopedLock sl (stateLock); return chans[(size_t) ch].b; }
    void copyCurrentToBothEnds();
    // Sound dice on a channel write the morph end the Sound Morph slider is nearest to (A below 50 %,
    // B from 50 %); the other end keeps its sound. A new model goes to both ends, since the other end
    // cannot morph into a different voice model. Returns 0 = A, 1 = B, 2 = both.
    int activeEnd() const { return getMorph() < 0.5f ? 0 : 1; }
    ferrum::Patch getActiveEnd (int ch) const { return activeEnd() == 0 ? getEndA (ch) : getEndB (ch); }
    int setPatchAtActiveEnd (int ch, const ferrum::Patch& p);
    // multi-out: every channel to its own plugin output (Ch 1-8), or every channel back to A
    void routeAllChannels (bool ownOutputs);
    static juce::String endName (int e) { return e == 0 ? "A" : (e == 1 ? "B" : "A + B"); }
    void swapMorphEnds();
    float getMorph() const;

    juce::String getChannelName (int ch) const { return chans[(size_t) ch].name; }
    void setChannelName (int ch, const juce::String& n) { chans[(size_t) ch].name = n; sendUiChange(); }
    juce::File getChannelFile (int ch) const { return chans[(size_t) ch].file; }

    int getSelectedPattern() const;
    void selectPattern (int p);
    void setParamValue (const juce::String& id, float plainValue);

    // presets / patches
    juce::String presetName { "LXR Init Kit" };
    juce::File presetFile;
    bool presetDirty = false;
    void loadKit (const ferrum::KitData& k);
    juce::ValueTree createPresetTree() const;
    void applyPresetTree (const juce::ValueTree& t, bool loadPatches = true, bool loadPatterns = true);
    bool savePreset (const juce::File& f);
    bool loadPreset (const juce::File& f, bool loadPatches = true, bool loadPatterns = true);
    juce::ValueTree createPatchTree (int ch) const;
    void applyPatchTree (int ch, const juce::ValueTree& t);
    bool savePatch (int ch, const juce::File& f);
    bool loadPatch (int ch, const juce::File& f);
    static juce::File userFolder();
    static juce::File presetFolder() { return userFolder().getChildFile ("Presets"); }
    static juce::File patchFolder() { return userFolder().getChildFile ("Patches"); }

    // Sound mutation: every N pattern loops a channel's sound moves a little (each channel has its own
    // rate, depth and mode). Orbit: each mutation starts again from the channel's home sound, so it
    // wanders around it. Drift: each mutation starts from the last one, so the sound travels.
    struct SoundMutation { int every = 0; float depth = 0.3f; int mode = 0; };   // every: loops, 0 = off
    static constexpr int kMutateEvery[6] = { 0, 1, 2, 4, 8, 16 };
    SoundMutation getSoundMutation (int ch) const { return soundMut[(size_t) ch]; }
    void setSoundMutation (int ch, const SoundMutation& m);
    void mutateSoundNow (int ch, float depth = -1.f); // one mutation step right away (depth < 0: the channel's depth)
    void keepMutationHome (int ch);        // the current sound becomes the home Orbit returns to
    void restoreMutationHome (int ch);     // back to the home sound
    uint32_t mutationCount (int ch) const { return mutCount[(size_t) ch]; }

    // Modulation (MOD tab): six modulators with routes to channel and FX parameters. Edit a copy and
    // hand it back with setMods(); call pushUndo() first for undoable edits.
    const std::array<ferrum::mod::ModState, ferrum::mod::kMods>& getMods() const { return modStates; }
    void setMods (const std::array<ferrum::mod::ModState, ferrum::mod::kMods>& m, bool markDirty = true);
    int addModRoute (int modulator, const ferrum::mod::Route& r);   // returns the route slot or -1 when full
    float modDepthOn (const ferrum::mod::Route& target) const;     // summed depth of every route to a target
    float modValue (int m) const { return engine.mods.uiValue[(size_t) m].load(); }

    // FX rack (edited from the FX tab; call pushUndo() before a change, rackChanged() after it)
    ferrum::fx::FxRack& getRack() { return engine.rack; }
    void rackChanged() { presetDirty = true; fxRackEdited(); sendUiChange(); }

    // FX Morph (see FxRack.h): four scenes of the rack's modules in the corners of an XY pad (host
    // parameters "FX Morph X" / "FX Morph Y"). Dry, Out, lane levels and bus outputs are not part
    // of a scene. With Morph on, edits on the FX tab go into the edit scene (the one last stored or
    // recalled), so what you change is what you hear.
    static constexpr int kFxScenes = ferrum::fx::FxRack::kScenes;
    bool fxSceneUsed (int i) const { return fxScenesUsed[(size_t) i]; }
    const ferrum::fx::RackState& getFxScene (int i) const { return fxScenes[(size_t) i]; }
    juce::String fxSceneSummary (int i) const;   // its modules in words
    void storeFxScene (int i);                    // the rack's modules into scene i (it becomes the edit scene)
    void recallFxScene (int i);                   // scene i's modules into the rack (it becomes the edit scene)
    void clearFxScene (int i);
    void setFxMorphOn (bool on);
    bool isFxMorphOn() const { return fxMorphOn; }
    int fxEditScene() const { return fxEdit; }
    void fxRackEdited();                          // the FX tab changed the rack (Morph on: into the edit scene)
    float fxMorphX() const { return pFxX->load(); }
    float fxMorphY() const { return pFxY->load(); }
    void setFxMorphXY (float x, float y);         // from the pad (host parameters, with gestures)
    void fxMorphGesture (bool start);

    // channel / pattern operations
    void moveChannel (int from, int to);
    void transposeChannel (int ch, float semitones);
    ferrum::PatternData getPattern (int p) const { return engine.patterns.get (p); }
    void setPattern (int p, const ferrum::PatternData& d) { engine.patterns.set (p, d); presetDirty = true; sendUiChange(); }
    std::array<ferrum::Flavor, NC> channelRoles() const;

    // undo (snapshots of the preset state, 40 deep like Microtonic)
    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // exports
    bool exportPatternMidi (const juce::File& f, int pattern) const;
    bool exportPatternAudio (const juce::File& f, int pattern, int bits, double sampleRate, int tailMode) const; // 0 none 1 append 2 loop
    juce::File createTempMidiForDrag (int pattern) const;

    // MIDI configuration (stored with the host project, not in presets)
    ferrum::MidiConfig getMidiConfig() const;
    void setMidiConfig (const ferrum::MidiConfig& c);

    ferrum::Randomizer randomizer;

    // ---- randomizing: every roll respects the channel locks, can be undone and is kept in the
    // roll history so an earlier result can be brought back
    ferrum::DiceSettings dice;              // change dice.evolve through setEvolve()
    std::array<bool, NC> locked {};         // locked channels keep their sound and their steps
    // amount 0.01..1: how far each roll goes (the dice buttons pass where they were clicked);
    // 1 = a completely new roll, less moves the current sounds / steps part of the way there
    juce::String rollSounds (float amount = 1.f);              // new sounds, same patterns (returns a status text)
    juce::String rollBeat (float amount = 1.f);                // new steps, same sounds: selected pattern or A-L
    juce::String rollAll (float amount = 1.f);                 // both
    juce::String rollChannelSteps (int ch, float amount = 1.f); // one channel's steps in the selected pattern
    juce::String rollMorphTargets (float amount = 1.f);        // random B ends for every unlocked channel

    // Per-channel step variations: a channel re-rolls its own steps in the playing pattern every N
    // loops (amount = share of the steps that change). Orbit starts from the original steps each time,
    // Drift from the last variation.
    struct StepReroll { int every = 0; float amount = 0.25f; int mode = 0; };   // every: loops, 0 = off
    static constexpr int kRerollEvery[6] = { 0, 1, 2, 4, 8, 16 };
    StepReroll getStepReroll (int ch) const { return stepReroll[(size_t) ch]; }
    void setStepReroll (int ch, const StepReroll& r);
    void restoreStepRerollHome (int ch);    // the channel's original steps in every pattern
    void keepStepRerollHome (int ch);       // the current steps become the original
    bool hasStepRerollHome (int ch) const;
    uint32_t stepRerollCount (int ch) const { return rerollCount[(size_t) ch]; }
    struct Roll { juce::String label, state; juce::Time when; };
    const std::vector<Roll>& getRollHistory() const { return rollHistory; }
    void restoreRoll (int index);
    void setEvolve (int mode);              // 0 off, 1 orbit, 2 drift
    void keepEvolvedAsHome();               // orbit: the current variation becomes the new centre
    juce::ChangeBroadcaster uiChanges; // structure changed (names, presets, patterns): editor repaints
    void sendUiChange() { uiChanges.sendChangeMessage(); }
    int editorChannel = 0;             // remembered with the state
    bool matrixView = false;
    int exprLane = 0;                  // expression lane shown in the step editor
    double lastHostBpm = 120.0;

private:
    void timerCallback() override;
    void pollMorph();
    void setParamFromPatchValue (int ch, int p, float v);
    static float paramNormToPatch (int p, float paramNorm);
    static float patchToParamNorm (int p, float v);
    static float morphValue (int p, float a, float b, float m);
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    ferrum::Engine engine;
    std::array<std::array<juce::RangedAudioParameter*, NP>, NC> patchParams {};
    std::array<std::array<std::atomic<float>*, NP>, NC> patchRaw {};
    std::array<std::array<float, NP>, NC> pushed {};

    struct Channel { ferrum::Patch a {}, b {}; juce::String name; juce::File file; };
    std::array<Channel, NC> chans;
    std::array<std::array<float, NP>, NC> lastSeen {};
    float lastMorph = 0.f;
    juce::CriticalSection stateLock;

    // global parameter raw pointers
    std::atomic<float>* pPlay = nullptr, *pPattern = nullptr, *pRate = nullptr, *pSwing = nullptr, *pFill = nullptr,
                      *pMaster = nullptr, *pTempo = nullptr, *pMorph = nullptr;
    std::array<std::atomic<float>*, NC> pMute {};
    std::array<std::atomic<float>*, ferrum::mod::kMods> pMacro {};
    std::array<ferrum::mod::ModState, ferrum::mod::kMods> modStates {};
    void publishFxMorph();
    std::array<ferrum::fx::RackState, kFxScenes> fxScenes {};
    std::array<bool, kFxScenes> fxScenesUsed {};
    bool fxMorphOn = false;
    int fxEdit = -1;
    std::atomic<float>* pFxX = nullptr, *pFxY = nullptr;

    std::atomic<int> midiLaunch { 0 }, midiPitched { 0 }, midiNoteOff { 0 }, midiTransmit { 1 }, midiBend { 12 };

    std::vector<juce::String> undoStack, redoStack;
    void recordRoll (const juce::String& label);
    void evolveTick();
    void soundMutationTick (int loops);
    std::array<SoundMutation, NC> soundMut {};
    std::array<ferrum::Patch, NC> mutHome {};
    std::array<bool, NC> mutHomeSet {};
    std::array<float, NC> mutHomeLoud {};   // measured loudness of the home sound (LUFS), 1e9 = not measured yet
    std::array<int, NC> mutLoops {};
    std::array<uint32_t, NC> mutCount {};
    void stepRerollTick (int loops);
    int blendSounds (float amount);
    void blendBeat (float amount, bool allPatterns, const std::array<ferrum::Flavor, NC>& roles, const ferrum::ChannelMask& keep);
    std::array<StepReroll, NC> stepReroll {};
    std::array<std::array<std::optional<ferrum::PatternData::Channel>, ferrum::kNumPatterns>, NC> rerollHome {};
    std::array<int, NC> rerollLoops {};
    std::array<uint32_t, NC> rerollCount {};
    bool mutating = false;
    std::vector<Roll> rollHistory;
    std::array<std::optional<ferrum::PatternData>, ferrum::kNumPatterns> evolveHome;
    uint32_t lastPatternStarts = 0;
    int loopsSinceEvolve = 0;
    std::vector<ferrum::MidiIn> midiIn;
    std::vector<ferrum::MidiOut> midiOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ferrum8Processor)
};
