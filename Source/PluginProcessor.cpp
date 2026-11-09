// PluginProcessor.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ferrum;

static const char* const kPatternNames[12] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L" };
static const juce::StringArray kRateNames { "1/8", "1/8T", "1/16", "1/16T", "1/32" };

//==============================================================================================
juce::String Ferrum8Processor::patchParamId (int ch, int p) { return "c" + juce::String (ch + 1) + "_" + paramDef (p).id; }

juce::AudioProcessorValueTreeState::ParameterLayout Ferrum8Processor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const KitData kit = initKit();

    for (int ch = 0; ch < NC; ++ch)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("ch" + String (ch + 1), "Channel " + String (ch + 1), " | ");
        for (int p = 0; p < NP; ++p)
        {
            const auto& d = paramDef (p);
            const float def = kit.a[(size_t) ch][(size_t) p];
            const ParameterID id { patchParamId (ch, p), 1 };
            const String name = "Ch" + String (ch + 1) + " " + d.name;
            if (d.numChoices > 0)
            {
                StringArray choices;
                for (int i = 0; i < d.numChoices; ++i) choices.add (d.choices[i]);
                group->addChild (std::make_unique<AudioParameterChoice> (id, name, choices, choiceIndex (def, d.numChoices)));
            }
            else
            {
                auto attrs = AudioParameterFloatAttributes()
                                 .withStringFromValueFunction ([p] (float v, int) { return String (formatValue (p, v)); })
                                 .withValueFromStringFunction ([p] (const String& s) { float o = 0.5f; parseValue (p, s.toStdString(), o); return o; });
                group->addChild (std::make_unique<AudioParameterFloat> (id, name, NormalisableRange<float> (0.f, 1.f), def, attrs));
            }
        }
        layout.add (std::move (group));
    }

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v * 100.f, 0) + "%"; });
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern", StringArray (kPatternNames, 12), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Step Rate", kRateNames, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (0.f, 1.f), 0.f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fillRate", 1 }, "Fill Rate", NormalisableRange<float> (2.f, 8.f), 2.f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2) + "x"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-60.f, 10.f, 0.f, 2.0f), -6.f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf dB") : String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Tempo (standalone)", NormalisableRange<float> (40.f, 300.f, 0.1f), 120.f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " bpm"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "morph", 1 }, "Sound Morph", NormalisableRange<float> (0.f, 1.f), 0.f, pct));
    for (int ch = 0; ch < NC; ++ch)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mute" + String (ch + 1), 1 }, "Mute " + String (ch + 1), false));
    for (int m = 0; m < mod::kMods; ++m) // a modulator set to Macro follows its knob, so the host can automate it
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "macro" + String (m + 1), 1 }, "Macro " + String (m + 1),
                                                           NormalisableRange<float> (0.f, 1.f), 0.f, pct));
    // FX Morph pad: X left (scenes 1, 3) .. right (2, 4), Y top (1, 2) .. bottom (3, 4)
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxMorphX", 1 }, "FX Morph X", NormalisableRange<float> (0.f, 1.f), 0.f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxMorphY", 1 }, "FX Morph Y", NormalisableRange<float> (0.f, 1.f), 0.f, pct));
    return layout;
}

Ferrum8Processor::Ferrum8Processor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output A", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output B", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 1", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 2", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 3", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 4", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 5", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 6", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 7", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Ch 8", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("FX 1", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("FX 2", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int ch = 0; ch < NC; ++ch)
        for (int p = 0; p < NP; ++p)
        {
            const auto id = patchParamId (ch, p);
            patchParams[(size_t) ch][(size_t) p] = apvts.getParameter (id);
            patchRaw[(size_t) ch][(size_t) p] = apvts.getRawParameterValue (id);
            pushed[(size_t) ch][(size_t) p] = -1.f;
        }
    pPlay = apvts.getRawParameterValue ("play");
    pPattern = apvts.getRawParameterValue ("pattern");
    pRate = apvts.getRawParameterValue ("rate");
    pSwing = apvts.getRawParameterValue ("swing");
    pFill = apvts.getRawParameterValue ("fillRate");
    pMaster = apvts.getRawParameterValue ("master");
    for (int m = 0; m < mod::kMods; ++m) pMacro[(size_t) m] = apvts.getRawParameterValue ("macro" + juce::String (m + 1));
    pFxX = apvts.getRawParameterValue ("fxMorphX");
    pFxY = apvts.getRawParameterValue ("fxMorphY");
    for (int m = 0; m < mod::kMods; ++m) modStates[(size_t) m] = mod::ModState {};
    pTempo = apvts.getRawParameterValue ("tempo");
    pMorph = apvts.getRawParameterValue ("morph");
    for (int ch = 0; ch < NC; ++ch) pMute[(size_t) ch] = apvts.getRawParameterValue ("mute" + juce::String (ch + 1));

    loadKit (initKit());
    undoStack.clear();
    presetDirty = false;
    midiIn.reserve (256);
    midiOut.reserve (256);
    startTimerHz (30);
}

Ferrum8Processor::~Ferrum8Processor() { stopTimer(); }

//==============================================================================================
float Ferrum8Processor::paramNormToPatch (int p, float paramNorm)
{
    const int n = paramDef (p).numChoices;
    if (n <= 0) return paramNorm;
    const int idx = juce::jlimit (0, n - 1, juce::roundToInt (paramNorm * (float) (n - 1)));
    return choiceValue (idx, n);
}

float Ferrum8Processor::patchToParamNorm (int p, float v)
{
    const int n = paramDef (p).numChoices;
    if (n <= 0) return v;
    return n > 1 ? (float) choiceIndex (v, n) / (float) (n - 1) : 0.f;
}

float Ferrum8Processor::morphValue (int p, float a, float b, float m)
{
    if (paramDef (p).numChoices > 0) return m < 0.5f ? a : b;
    return a + (b - a) * m;
}

float Ferrum8Processor::getPatchValue (int ch, int p) const
{
    return paramNormToPatch (p, patchParams[(size_t) ch][(size_t) p]->getValue());
}

void Ferrum8Processor::setParamFromPatchValue (int ch, int p, float v)
{
    auto* prm = patchParams[(size_t) ch][(size_t) p];
    const float target = patchToParamNorm (p, v);
    if (std::abs (prm->getValue() - target) > 1.0e-7f) prm->setValueNotifyingHost (target);
}

void Ferrum8Processor::setPatchValue (int ch, int p, float v)
{
    setParamFromPatchValue (ch, p, lxr::clamp01 (v));
    pollMorph(); // fold the change into the morph end points right away
}

Patch Ferrum8Processor::getCurrentPatch (int ch) const
{
    Patch p {};
    for (int i = 0; i < NP; ++i) p[(size_t) i] = getPatchValue (ch, i);
    return p;
}

float Ferrum8Processor::getMorph() const { return pMorph->load(); }

void Ferrum8Processor::setPatch (int ch, const Patch& in, bool alsoMorphTarget, bool keepRouting)
{
    if (! mutating) mutHomeSet[(size_t) ch] = false; // a new sound: mutation's home follows it
    Patch p = in;
    if (keepRouting)
    {
        const Patch cur = getCurrentPatch (ch);
        p[P_OUTPUT] = cur[P_OUTPUT]; p[P_CHOKE] = cur[P_CHOKE];
    }
    const float m = getMorph();
    {
        const juce::ScopedLock sl (stateLock);
        auto& c = chans[(size_t) ch];
        c.a = p;
        if (alsoMorphTarget) c.b = p;
        for (int i = 0; i < NP; ++i)
            if (! paramDef (i).morphable) c.b[(size_t) i] = p[(size_t) i];
    }
    for (int i = 0; i < NP; ++i)
    {
        const auto& c = chans[(size_t) ch];
        const float v = morphValue (i, c.a[(size_t) i], c.b[(size_t) i], m);
        setParamFromPatchValue (ch, i, v);
        lastSeen[(size_t) ch][(size_t) i] = getPatchValue (ch, i);
    }
    presetDirty = true;
    sendUiChange();
}

//==============================================================================================
void Ferrum8Processor::setSoundMutation (int ch, const SoundMutation& m)
{
    auto& cur = soundMut[(size_t) ch];
    if (cur.every == 0 && m.every > 0) { mutHome[(size_t) ch] = getActiveEnd (ch); mutHomeSet[(size_t) ch] = true; mutHomeLoud[(size_t) ch] = 1e9f; mutLoops[(size_t) ch] = 0; }
    cur = m;
    cur.depth = juce::jlimit (0.f, 1.f, cur.depth);
    presetDirty = true;
}

void Ferrum8Processor::mutateSoundNow (int ch, float depth)
{
    const auto& m = soundMut[(size_t) ch];
    if (depth < 0.f) depth = m.depth;
    if (! mutHomeSet[(size_t) ch]) { mutHome[(size_t) ch] = getActiveEnd (ch); mutHomeSet[(size_t) ch] = true; mutHomeLoud[(size_t) ch] = 1e9f; }
    Patch base = m.mode == 0 ? mutHome[(size_t) ch] : getActiveEnd (ch);
    if (choiceIndex (base[P_MODEL], 4) != choiceIndex (getActiveEnd (ch)[P_MODEL], 4)) base = getActiveEnd (ch);
    Patch p = randomizer.mutateSound (base, depth, dice.key);
    // keep the loudness even: more drive (or a brighter filter, a longer decay ...) brings the level
    // down, less brings it back up - measured against the home sound, so it never creeps
    if (mutHomeLoud[(size_t) ch] > 1e8f) mutHomeLoud[(size_t) ch] = Randomizer::hitLoudness (mutHome[(size_t) ch]);
    const float ref = mutHomeLoud[(size_t) ch];
    if (ref > -90.f)
    {
        // the level may go down a long way (heavy drive) but only up to full level
        const float homeLevel = mutHome[(size_t) ch][P_LEVEL];
        Randomizer::matchLoudness (p, ref, std::max (0.02f, homeLevel * 0.2f), 1.f);
    }
    mutating = true;
    setPatchAtActiveEnd (ch, p);
    mutating = false;
    ++mutCount[(size_t) ch];
}

void Ferrum8Processor::keepMutationHome (int ch)
{
    mutHome[(size_t) ch] = getActiveEnd (ch);
    mutHomeSet[(size_t) ch] = true;
    mutHomeLoud[(size_t) ch] = 1e9f;
}

void Ferrum8Processor::restoreMutationHome (int ch)
{
    if (! mutHomeSet[(size_t) ch]) return;
    pushUndo();
    mutating = true;
    setPatchAtActiveEnd (ch, mutHome[(size_t) ch]);
    mutating = false;
}

void Ferrum8Processor::soundMutationTick (int loops)
{
    if (engine.uiState.load() != (int) SeqState::Playing) return;
    for (int ch = 0; ch < NC; ++ch)
    {
        const auto& m = soundMut[(size_t) ch];
        if (m.every <= 0) { mutLoops[(size_t) ch] = 0; continue; }
        mutLoops[(size_t) ch] += loops;
        if (mutLoops[(size_t) ch] < m.every) continue;
        mutLoops[(size_t) ch] = 0;
        mutateSoundNow (ch);
    }
}

void Ferrum8Processor::setMods (const std::array<mod::ModState, mod::kMods>& m, bool markDirty)
{
    modStates = m;
    engine.mods.setState (modStates);
    if (markDirty) presetDirty = true;
}

int Ferrum8Processor::addModRoute (int m, const mod::Route& r)
{
    auto ms = modStates;
    auto& s = ms[(size_t) m];
    if (s.type == mod::Type::Off) { const auto keep = s.routes; s = mod::defaultState (mod::Type::LFO); s.routes = keep; }
    int slot = -1;
    for (int i = 0; i < mod::kRoutes; ++i) if (s.routes[(size_t) i].kind != mod::Route::None && s.routes[(size_t) i].sameTarget (r)) slot = i;
    if (slot < 0) for (int i = 0; i < mod::kRoutes; ++i) if (s.routes[(size_t) i].kind == mod::Route::None) { slot = i; break; }
    if (slot < 0) return -1;
    s.routes[(size_t) slot] = r;
    setMods (ms);
    return slot;
}

float Ferrum8Processor::modDepthOn (const mod::Route& target) const
{
    float d = 0.f;
    for (const auto& s : modStates)
    {
        if (s.type == mod::Type::Off) continue;
        for (const auto& r : s.routes) if (r.kind != mod::Route::None && r.sameTarget (target)) d += r.depth;
    }
    return d;
}

//==============================================================================================
// FX Morph
void Ferrum8Processor::publishFxMorph()
{
    engine.rack.setMorphScenes (fxScenesUsed, fxScenes);
    engine.rack.setMorphOn (fxMorphOn);
}

juce::String Ferrum8Processor::fxSceneSummary (int i) const
{
    if (! fxScenesUsed[(size_t) i]) return "empty";
    juce::StringArray buses;
    for (int b = 0; b < fx::kBuses; ++b)
    {
        juce::StringArray names;
        for (const auto& l : fxScenes[(size_t) i].buses[(size_t) b].lanes)
            for (const auto& sl : l.slots) if (sl.type != fx::Type::None) names.add (fx::info (sl.type).name);
        if (! names.isEmpty()) buses.add (juce::String (fx::busName (b)) + ": " + names.joinIntoString (" > "));
    }
    return buses.isEmpty() ? juce::String ("no modules (effects fade out here)") : buses.joinIntoString ("  |  ");
}

void Ferrum8Processor::storeFxScene (int i)
{
    if (i < 0 || i >= kFxScenes) return;
    fxScenes[(size_t) i] = engine.rack.getState();
    fxScenesUsed[(size_t) i] = true;
    fxEdit = i;
    publishFxMorph();
    presetDirty = true;
    sendUiChange();
}

void Ferrum8Processor::recallFxScene (int i)
{
    if (i < 0 || i >= kFxScenes || ! fxScenesUsed[(size_t) i]) return;
    // the scene's modules; Dry, Out, lane levels and outputs stay as they are
    auto st = engine.rack.getState();
    for (int b = 0; b < fx::kBuses; ++b)
        for (int l = 0; l < fx::kLanes; ++l)
            st.buses[(size_t) b].lanes[(size_t) l].slots = fxScenes[(size_t) i].buses[(size_t) b].lanes[(size_t) l].slots;
    engine.rack.setState (st);
    fxEdit = i;
    presetDirty = true;
    sendUiChange();
}

void Ferrum8Processor::clearFxScene (int i)
{
    if (i < 0 || i >= kFxScenes) return;
    fxScenesUsed[(size_t) i] = false;
    fxScenes[(size_t) i] = {};
    if (fxEdit == i) fxEdit = -1;
    publishFxMorph();
    presetDirty = true;
    sendUiChange();
}

void Ferrum8Processor::setFxMorphOn (bool on)
{
    fxMorphOn = on;
    if (on && fxEdit < 0) for (int i = 0; i < kFxScenes && fxEdit < 0; ++i) if (fxScenesUsed[(size_t) i]) fxEdit = i;
    publishFxMorph();
    presetDirty = true;
    sendUiChange();
}

void Ferrum8Processor::fxRackEdited()
{
    if (! fxMorphOn || fxEdit < 0 || ! fxScenesUsed[(size_t) fxEdit]) return;
    fxScenes[(size_t) fxEdit] = engine.rack.getState();
    publishFxMorph();
}

void Ferrum8Processor::setFxMorphXY (float x, float y)
{
    setParamValue ("fxMorphX", juce::jlimit (0.f, 1.f, x));
    setParamValue ("fxMorphY", juce::jlimit (0.f, 1.f, y));
}

void Ferrum8Processor::fxMorphGesture (bool start)
{
    for (auto* id : { "fxMorphX", "fxMorphY" })
        if (auto* p = apvts.getParameter (id)) { if (start) p->beginChangeGesture(); else p->endChangeGesture(); }
}

void Ferrum8Processor::routeAllChannels (bool ownOutputs)
{
    pushUndo();
    const int n = paramDef (P_OUTPUT).numChoices;
    for (int ch = 0; ch < NC; ++ch) setPatchValue (ch, P_OUTPUT, choiceValue (ownOutputs ? 4 : 0, n));
    presetDirty = true;
    sendUiChange();
}

int Ferrum8Processor::setPatchAtActiveEnd (int ch, const Patch& newSound)
{
    Patch in = newSound;
    in[P_OUTPUT] = getEndA (ch)[P_OUTPUT]; // dice change the sound, never the routing
    const int end = activeEnd();
    const Patch other = end == 0 ? getEndB (ch) : getEndA (ch);
    if (! mutating) { mutHome[(size_t) ch] = in; mutHomeSet[(size_t) ch] = true; mutHomeLoud[(size_t) ch] = 1e9f; }
    if (choiceIndex (other[P_MODEL], 4) != choiceIndex (in[P_MODEL], 4))
    {
        setPatch (ch, in, true);
        if (! mutating) { mutHome[(size_t) ch] = in; mutHomeSet[(size_t) ch] = true; mutHomeLoud[(size_t) ch] = 1e9f; }
        return 2;
    }
    {
        const juce::ScopedLock sl (stateLock);
        auto& c = chans[(size_t) ch];
        (end == 0 ? c.a : c.b) = in;
        for (int i = 0; i < NP; ++i)
            if (! paramDef (i).morphable) c.a[(size_t) i] = c.b[(size_t) i] = in[(size_t) i];
    }
    const float m = getMorph();
    for (int i = 0; i < NP; ++i)
    {
        const auto& c = chans[(size_t) ch];
        setParamFromPatchValue (ch, i, morphValue (i, c.a[(size_t) i], c.b[(size_t) i], m));
        lastSeen[(size_t) ch][(size_t) i] = getPatchValue (ch, i);
    }
    presetDirty = true;
    sendUiChange();
    return end;
}

void Ferrum8Processor::setMorphTarget (int ch, const Patch& b)
{
    {
        const juce::ScopedLock sl (stateLock);
        auto& c = chans[(size_t) ch];
        for (int i = 0; i < NP; ++i) c.b[(size_t) i] = paramDef (i).morphable ? b[(size_t) i] : c.a[(size_t) i];
    }
    const float m = getMorph();
    for (int i = 0; i < NP; ++i)
    {
        const auto& c = chans[(size_t) ch];
        setParamFromPatchValue (ch, i, morphValue (i, c.a[(size_t) i], c.b[(size_t) i], m));
        lastSeen[(size_t) ch][(size_t) i] = getPatchValue (ch, i);
    }
    presetDirty = true;
    sendUiChange();
}

void Ferrum8Processor::copyCurrentToBothEnds()
{
    for (int ch = 0; ch < NC; ++ch) setPatch (ch, getCurrentPatch (ch), true);
}

void Ferrum8Processor::swapMorphEnds()
{
    {
        const juce::ScopedLock sl (stateLock);
        for (auto& c : chans) std::swap (c.a, c.b);
    }
    const float m = getMorph();
    for (int ch = 0; ch < NC; ++ch)
        for (int i = 0; i < NP; ++i)
        {
            const auto& c = chans[(size_t) ch];
            setParamFromPatchValue (ch, i, morphValue (i, c.a[(size_t) i], c.b[(size_t) i], m));
            lastSeen[(size_t) ch][(size_t) i] = getPatchValue (ch, i);
        }
    sendUiChange();
}

// Polled on the message thread. A parameter that moved (knob, host automation, Edit All) moves
// the morph end points so the current position shows the new value: at the far left only A
// changes, in the middle both change equally. Moving the morph slider re-interpolates.
void Ferrum8Processor::pollMorph()
{
    const juce::ScopedLock sl (stateLock);
    const float m = lastMorph;
    bool changed = false;
    for (int ch = 0; ch < NC; ++ch)
    {
        auto& c = chans[(size_t) ch];
        for (int i = 0; i < NP; ++i)
        {
            const float v = getPatchValue (ch, i);
            float& seen = lastSeen[(size_t) ch][(size_t) i];
            if (std::abs (v - seen) <= 1.0e-6f) continue;
            const auto& d = paramDef (i);
            if (! d.morphable) { c.a[(size_t) i] = c.b[(size_t) i] = v; }
            else if (d.numChoices > 0) { if (m < 0.5f) c.a[(size_t) i] = v; else c.b[(size_t) i] = v; }
            else
            {
                const float delta = v - seen;
                const float w = (1 - m) * (1 - m) + m * m;
                c.a[(size_t) i] = lxr::clamp01 (c.a[(size_t) i] + delta * (1 - m) / w);
                c.b[(size_t) i] = lxr::clamp01 (c.b[(size_t) i] + delta * m / w);
            }
            seen = v;
            changed = true;
        }
    }
    const float newMorph = pMorph->load();
    if (std::abs (newMorph - lastMorph) > 1.0e-6f)
    {
        lastMorph = newMorph;
        for (int ch = 0; ch < NC; ++ch)
            for (int i = 0; i < NP; ++i)
            {
                if (! paramDef (i).morphable) continue;
                const auto& c = chans[(size_t) ch];
                setParamFromPatchValue (ch, i, morphValue (i, c.a[(size_t) i], c.b[(size_t) i], newMorph));
                lastSeen[(size_t) ch][(size_t) i] = getPatchValue (ch, i);
            }
    }
    if (changed) presetDirty = true;
}

void Ferrum8Processor::timerCallback()
{
    pollMorph();
    evolveTick();
    engine.rack.collectGarbage(); // modules replaced on the audio thread are deleted here
    const int playReq = engine.uiPlayRequest.exchange (0);
    if (playReq != 0) setParamValue ("play", playReq > 0 ? 1.f : 0.f);
    const int patReq = engine.uiPatternRequest.exchange (-1);
    if (patReq >= 0) setParamValue ("pattern", (float) patReq);
}

void Ferrum8Processor::setParamValue (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        const float n = p->convertTo0to1 (plain);
        if (std::abs (p->getValue() - n) > 1.0e-7f) p->setValueNotifyingHost (n);
    }
}

int Ferrum8Processor::getSelectedPattern() const { return juce::jlimit (0, kNumPatterns - 1, (int) pPattern->load()); }
void Ferrum8Processor::selectPattern (int p) { setParamValue ("pattern", (float) juce::jlimit (0, kNumPatterns - 1, p)); }

//==============================================================================================
void Ferrum8Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, std::max (samplesPerBlock, 512));
    for (auto& row : pushed) row.fill (-1.f);
}

bool Ferrum8Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // main output stereo; every other output stereo or switched off by the host
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& b = layouts.outputBuses.getReference (i);
        if (! b.isDisabled() && b != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void Ferrum8Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // patch parameters -> engine (only values that changed)
    for (int ch = 0; ch < NC; ++ch)
        for (int p = 0; p < NP; ++p)
        {
            const int nc = paramDef (p).numChoices;
            const float raw = patchRaw[(size_t) ch][(size_t) p]->load();
            const float v = nc > 0 ? choiceValue (juce::roundToInt (raw), nc) : raw;
            float& last = pushed[(size_t) ch][(size_t) p];
            if (v != last) { engine.setPatchParam (ch, p, v); last = v; }
        }

    Globals g;
    g.playOn = pPlay->load() > 0.5f;
    g.selectedPattern = juce::jlimit (0, kNumPatterns - 1, (int) pPattern->load());
    g.rate = (StepRate) juce::jlimit (0, 4, (int) pRate->load());
    g.swing = pSwing->load();
    g.fillRate = pFill->load();
    const float db = pMaster->load();
    g.master = db <= -59.9f ? 0.f : juce::Decibels::decibelsToGain (db);
    g.tempo = pTempo->load();
    for (int ch = 0; ch < NC; ++ch) g.mutes[ch] = pMute[(size_t) ch]->load() > 0.5f;
    engine.setGlobals (g);
    engine.setMidiConfig (getMidiConfig());
    for (int m = 0; m < mod::kMods; ++m) engine.mods.setMacro (m, pMacro[(size_t) m]->load());
    engine.rack.setMorphXY (pFxX->load(), pFxY->load());

    Transport t;
    if (wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                t.hostHasTransport = true;
                t.hostPlaying = pos->getIsPlaying();
                t.ppq = pos->getPpqPosition().orFallback (0.0);
                t.bpm = pos->getBpm().orFallback (120.0);
                lastHostBpm = t.bpm;
            }
    if (! t.hostHasTransport) { t.bpm = g.tempo; lastHostBpm = g.tempo; }

    midiIn.clear();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.getRawDataSize() >= 2 && msg.getRawDataSize() <= 3)
        {
            const auto* d = msg.getRawData();
            midiIn.push_back ({ meta.samplePosition, d[0], d[1], (uint8_t) (msg.getRawDataSize() > 2 ? d[2] : 0) });
        }
    }
    midi.clear();
    midiOut.clear();

    // outputs: A (main), B, Ch 1-8 (each channel's own output), FX 1 and FX 2 (the buses' own outputs).
    // getBusBuffer() gives an output's channels inside the host's buffer; one the host has switched
    // off has no channels, and the engine then mixes what would go there into A.
    Engine::Outputs out;
    auto pair = [this, &buffer] (int bus, float*& l, float*& r)
    {
        l = r = nullptr;
        if (bus >= getBusCount (false) || ! getBus (false, bus)->isEnabled()) return;
        if (getChannelIndexInProcessBlockBuffer (false, bus, 1) >= buffer.getNumChannels()) return; // host gave fewer channels
        auto b = getBusBuffer (buffer, false, bus);
        if (b.getNumChannels() >= 2) { l = b.getWritePointer (0); r = b.getWritePointer (1); }
    };
    {
        auto mainBus = getBusBuffer (buffer, false, 0);
        out.aL = mainBus.getWritePointer (0);
        out.aR = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : out.aL;
    }
    pair (1, out.bL, out.bR);
    for (int c = 0; c < NC; ++c) pair (2 + c, out.chL[(size_t) c], out.chR[(size_t) c]);
    for (int k = 0; k < 2; ++k) pair (2 + NC + k, out.fxL[(size_t) k], out.fxR[(size_t) k]);
    engine.process (out, n, t, midiIn, &midiOut);

    for (const auto& m : midiOut)
        midi.addEvent (juce::MidiMessage (m.status, m.data1, m.data2), juce::jlimit (0, n - 1, m.offset));
}

//==============================================================================================
MidiConfig Ferrum8Processor::getMidiConfig() const
{
    MidiConfig c;
    c.launch = (PatternLaunch) midiLaunch.load();
    c.pitchedMode = midiPitched.load() != 0;
    c.noteOffStops = midiNoteOff.load() != 0;
    c.transmitPatternNotes = midiTransmit.load() != 0;
    c.pitchBendSemis = midiBend.load();
    return c;
}

void Ferrum8Processor::setMidiConfig (const MidiConfig& c)
{
    midiLaunch = (int) c.launch;
    midiPitched = c.pitchedMode ? 1 : 0;
    midiNoteOff = c.noteOffStops ? 1 : 0;
    midiTransmit = c.transmitPatternNotes ? 1 : 0;
    midiBend = c.pitchBendSemis;
}

//==============================================================================================
static juce::String toHex16 (const std::array<uint16_t, kNumChannels>& a)
{
    juce::String s;
    for (auto v : a) s << juce::String::toHexString ((int) v).paddedLeft ('0', 4);
    return s;
}
static std::array<uint16_t, kNumChannels> fromHex16 (const juce::String& s)
{
    std::array<uint16_t, kNumChannels> a {};
    for (int c = 0; c < kNumChannels && s.length() >= (c + 1) * 4; ++c) a[(size_t) c] = (uint16_t) s.substring (c * 4, c * 4 + 4).getHexValue32();
    return a;
}
// one expression lane of a pattern: 8 channels x 16 steps, one signed byte each, as hex
static juce::String exprToHex (const PatternData& d, int lane)
{
    juce::String s;
    for (int c = 0; c < kNumChannels; ++c)
        for (int st = 0; st < kMaxSteps; ++st)
            s << juce::String::toHexString ((int) (uint8_t) d.expr[(size_t) c][(size_t) lane][(size_t) st]).paddedLeft ('0', 2);
    return s;
}
static void exprFromHex (PatternData& d, int lane, const juce::String& s)
{
    for (int c = 0; c < kNumChannels; ++c)
        for (int st = 0; st < kMaxSteps; ++st)
        {
            const int i = (c * kMaxSteps + st) * 2;
            if (s.length() < i + 2) return;
            d.expr[(size_t) c][(size_t) lane][(size_t) st] = (int8_t) (uint8_t) s.substring (i, i + 2).getHexValue32();
        }
}
//== FX rack <-> ValueTree. Parameters are stored by name (choices by their label), so modules can
// gain parameters later without breaking files.
static juce::String rackKey (const char* paramName)
{
    juce::String k ("p_");
    for (auto c : juce::String (paramName)) if (juce::CharacterFunctions::isLetterOrDigit (c)) k << juce::String::charToString (c);
    return k;
}
static juce::ValueTree rackToTree (const fx::RackState& st)
{
    juce::ValueTree t ("FXRACK");
    t.setProperty ("version", 1, nullptr);
    for (int b = 0; b < fx::kBuses; ++b)
    {
        const auto& bus = st.buses[(size_t) b];
        juce::ValueTree bt ("BUS");
        bt.setProperty ("index", b, nullptr);
        bt.setProperty ("dry", bus.dry, nullptr);
        bt.setProperty ("outDb", bus.outDb, nullptr);
        static const char* outNames[4] = { "A", "B", "Own", "FX 2" };
        bt.setProperty ("output", outNames[juce::jlimit (0, 3, bus.output)], nullptr);
        for (int l = 0; l < fx::kLanes; ++l)
        {
            const auto& lane = bus.lanes[(size_t) l];
            juce::ValueTree lt ("LANE");
            lt.setProperty ("index", l, nullptr);
            lt.setProperty ("level", lane.level, nullptr);
            lt.setProperty ("mute", lane.mute, nullptr);
            for (int s = 0; s < fx::kSlots; ++s)
            {
                const auto& slot = lane.slots[(size_t) s];
                if (slot.type == fx::Type::None) continue;
                const auto& in = fx::info (slot.type);
                juce::ValueTree stt ("SLOT");
                stt.setProperty ("index", s, nullptr);
                stt.setProperty ("type", in.id, nullptr);
                stt.setProperty ("bypass", slot.bypass, nullptr);
                for (size_t i = 0; i < in.params.size(); ++i)
                {
                    const auto& ps = in.params[i];
                    if (ps.scale == fx::Scale::Choice) stt.setProperty (rackKey (ps.name), ps.choices[(size_t) fx::choiceIndex (ps, slot.p[i])], nullptr);
                    else stt.setProperty (rackKey (ps.name), slot.p[i], nullptr);
                }
                lt.appendChild (stt, nullptr);
            }
            bt.appendChild (lt, nullptr);
        }
        t.appendChild (bt, nullptr);
    }
    return t;
}
static fx::RackState treeToRack (const juce::ValueTree& t)
{
    fx::RackState st;
    for (const auto bt : t)
    {
        if (! bt.hasType ("BUS")) continue;
        const int b = (int) bt.getProperty ("index", -1);
        if (b < 0 || b >= fx::kBuses) continue;
        auto& bus = st.buses[(size_t) b];
        bus.dry = juce::jlimit (0.f, 1.f, (float) bt.getProperty ("dry", 0.f));
        bus.outDb = juce::jlimit (-24.f, 12.f, (float) bt.getProperty ("outDb", 0.f));
        {
            const auto o = bt.getProperty ("output", "A").toString();
            bus.output = o == "B" ? fx::kToB : o == "Own" ? fx::kToOwn : (o == "FX 2" && b == 0) ? fx::kToFx2 : fx::kToA;
        }
        for (const auto lt : bt)
        {
            const int l = (int) lt.getProperty ("index", -1);
            if (l < 0 || l >= fx::kLanes) continue;
            auto& lane = bus.lanes[(size_t) l];
            lane.level = juce::jlimit (0.f, 2.f, (float) lt.getProperty ("level", 1.f));
            lane.mute = (bool) lt.getProperty ("mute", false);
            for (const auto stt : lt)
            {
                const int s = (int) stt.getProperty ("index", -1);
                const auto type = fx::typeFromId (stt.getProperty ("type", "").toString().toStdString());
                if (s < 0 || s >= fx::kSlots || type == fx::Type::None) continue;
                auto& slot = lane.slots[(size_t) s];
                const auto& in = fx::info (type);
                slot.type = type;
                slot.bypass = (bool) stt.getProperty ("bypass", false);
                for (size_t i = 0; i < in.params.size(); ++i)
                {
                    const auto& ps = in.params[i];
                    slot.p[i] = ps.def;
                    const auto key = rackKey (ps.name);
                    if (! stt.hasProperty (key)) continue;
                    if (ps.scale == fx::Scale::Choice)
                    {
                        const auto name = stt.getProperty (key).toString();
                        for (size_t k = 0; k < ps.choices.size(); ++k)
                            if (name == ps.choices[k]) slot.p[i] = fx::choiceNorm (ps, (int) k);
                    }
                    else slot.p[i] = juce::jlimit (0.f, 1.f, (float) stt.getProperty (key));
                }
            }
        }
    }
    return st;
}
// the single FX bus of earlier versions, as stored in a preset's GLOBALS
static std::optional<fx::RackState> legacyRack (const juce::ValueTree& glob)
{
    if (! glob.isValid() || ! glob.hasProperty ("fxType")) return std::nullopt;
    FxSettings fs;
    fs.type = (FxType) juce::jlimit (0, 4, (int) glob["fxType"]);
    fs.dryWet = (float) glob.getProperty ("fxMix", 0.5f);
    for (int i = 0; i < 4; ++i) fs.p[i] = (float) glob.getProperty ("fxP" + juce::String (i + 1), 0.5f);
    fs.distType = (DistType) juce::jlimit (0, 2, (int) glob.getProperty ("fxDist", 0));
    fs.ratio = juce::jlimit (0, 5, (int) glob.getProperty ("fxRatio", 2));
    fs.ringWave = (int) glob.getProperty ("fxWave", 3);
    fs.pingPong = (float) glob.getProperty ("fxPingPong", 0.f) > 0.5f;
    return fx::FxRack::fromLegacy (fs, (int) glob.getProperty ("fxOut", 0));
}

//== modulation <-> ValueTree. Channel targets are stored by parameter id, FX targets by bus/lane/slot
// and the module parameter's name.
static juce::ValueTree modsToTree (const std::array<mod::ModState, mod::kMods>& ms, const fx::RackState& rack)
{
    juce::ValueTree t ("MODS");
    for (int m = 0; m < mod::kMods; ++m)
    {
        const auto& s = ms[(size_t) m];
        if (s.type == mod::Type::Off) continue;
        juce::ValueTree mt ("MOD");
        mt.setProperty ("index", m, nullptr);
        mt.setProperty ("type", mod::kTypeNames[(int) s.type], nullptr);
        for (int i = 0; i < mod::kKnobs; ++i) mt.setProperty ("k" + juce::String (i), s.p[i], nullptr);
        mt.setProperty ("trigger", s.trigger, nullptr);
        mt.setProperty ("source", s.source, nullptr);
        juce::StringArray steps;
        for (float v : s.steps) steps.add (juce::String (v, 3));
        mt.setProperty ("steps", steps.joinIntoString (","), nullptr);
        for (const auto& r : s.routes)
        {
            if (r.kind == mod::Route::None) continue;
            juce::ValueTree rt ("ROUTE");
            rt.setProperty ("depth", r.depth, nullptr);
            if (r.kind == mod::Route::Channel)
            {
                rt.setProperty ("ch", r.ch, nullptr);
                rt.setProperty ("param", paramDef (r.param).id, nullptr);
            }
            else
            {
                rt.setProperty ("bus", r.bus, nullptr); rt.setProperty ("lane", r.lane, nullptr); rt.setProperty ("slot", r.slot, nullptr);
                rt.setProperty ("idx", r.idx, nullptr);
                const auto type = rack.buses[(size_t) r.bus].lanes[(size_t) r.lane].slots[(size_t) r.slot].type;
                if (type != fx::Type::None && r.idx < (int) fx::info (type).params.size())
                    rt.setProperty ("fxParam", fx::info (type).params[(size_t) r.idx].name, nullptr);
            }
            mt.appendChild (rt, nullptr);
        }
        t.appendChild (mt, nullptr);
    }
    return t;
}

static std::array<mod::ModState, mod::kMods> treeToMods (const juce::ValueTree& t)
{
    std::array<mod::ModState, mod::kMods> ms {};
    for (const auto mt : t)
    {
        const int m = (int) mt.getProperty ("index", -1);
        if (m < 0 || m >= mod::kMods) continue;
        auto type = mod::Type::Off;
        for (int k = 0; k < (int) mod::Type::Count; ++k) if (mt["type"].toString() == mod::kTypeNames[k]) type = (mod::Type) k;
        if (type == mod::Type::Off) continue;
        auto s = mod::defaultState (type);
        for (int i = 0; i < mod::kKnobs; ++i) s.p[i] = juce::jlimit (0.f, 1.f, (float) mt.getProperty ("k" + juce::String (i), s.p[i]));
        s.trigger = juce::jlimit (0, mod::TrigCount - 1, (int) mt.getProperty ("trigger", s.trigger));
        s.source = juce::jlimit (0, mod::PeakCount - 1, (int) mt.getProperty ("source", s.source));
        const auto steps = juce::StringArray::fromTokens (mt["steps"].toString(), ",", "");
        for (int i = 0; i < mod::kSteps && i < steps.size(); ++i) s.steps[(size_t) i] = juce::jlimit (0.f, 1.f, steps[i].getFloatValue());
        int n = 0;
        for (const auto rt : mt)
        {
            if (n >= mod::kRoutes) break;
            mod::Route r;
            r.depth = juce::jlimit (-1.f, 1.f, (float) rt.getProperty ("depth", 0.f));
            if (rt.hasProperty ("param"))
            {
                r.kind = mod::Route::Channel;
                r.ch = juce::jlimit (0, Ferrum8Processor::NC - 1, (int) rt["ch"]);
                r.param = -1;
                for (int p = 0; p < NUM_PATCH_PARAMS; ++p) if (rt["param"].toString() == paramDef (p).id) r.param = p;
                if (r.param < 0) continue;
            }
            else
            {
                r.kind = mod::Route::Fx;
                r.bus = juce::jlimit (0, fx::kBuses - 1, (int) rt["bus"]);
                r.lane = juce::jlimit (0, fx::kLanes - 1, (int) rt["lane"]);
                r.slot = juce::jlimit (0, fx::kSlots - 1, (int) rt["slot"]);
                r.idx = juce::jlimit (0, fx::kMaxParams - 1, (int) rt["idx"]);
            }
            s.routes[(size_t) n++] = r;
        }
        ms[(size_t) m] = s;
    }
    return ms;
}

static juce::ValueTree patchToTree (const juce::Identifier& type, const Patch& p)
{
    juce::ValueTree t (type);
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i)
    {
        const auto& d = paramDef (i);
        if (d.numChoices > 0) t.setProperty (d.id, d.choices[choiceIndex (p[(size_t) i], d.numChoices)], nullptr);
        else t.setProperty (d.id, p[(size_t) i], nullptr);
    }
    return t;
}
static Patch treeToPatch (const juce::ValueTree& t, const Patch& fallback)
{
    Patch p = fallback;
    for (int i = 0; i < NUM_PATCH_PARAMS; ++i)
    {
        const auto& d = paramDef (i);
        if (! t.hasProperty (d.id)) continue;
        const juce::var v = t.getProperty (d.id);
        if (d.numChoices > 0)
        {
            const juce::String s = v.toString();
            for (int k = 0; k < d.numChoices; ++k) if (s == d.choices[k]) p[(size_t) i] = choiceValue (k, d.numChoices);
            if (i == P_OUTPUT && s == "FX") p[(size_t) i] = choiceValue (2, d.numChoices); // the single FX bus of version 1
            if (i == P_OUTPUT && s == "Own") p[(size_t) i] = choiceValue (4, d.numChoices); // early name of "Own Out"
        }
        else p[(size_t) i] = lxr::clamp01 ((float) v);
    }
    return p;
}

juce::ValueTree Ferrum8Processor::createPatchTree (int ch) const
{
    juce::ValueTree t ("FERRUM8_PATCH");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("name", chans[(size_t) ch].name, nullptr);
    const juce::ScopedLock sl (stateLock);
    t.appendChild (patchToTree ("A", chans[(size_t) ch].a), nullptr);
    t.appendChild (patchToTree ("B", chans[(size_t) ch].b), nullptr);
    return t;
}

void Ferrum8Processor::applyPatchTree (int ch, const juce::ValueTree& t)
{
    const Patch cur = getCurrentPatch (ch);
    const auto ta = t.getChildWithName ("A");
    const auto tb = t.getChildWithName ("B");
    Patch a = treeToPatch (ta.isValid() ? ta : t, cur);
    Patch b = tb.isValid() ? treeToPatch (tb, a) : a;
    a[P_OUTPUT] = b[P_OUTPUT] = cur[P_OUTPUT]; // routing and choke belong to the channel slot
    a[P_CHOKE] = b[P_CHOKE] = cur[P_CHOKE];
    setPatch (ch, a, true);
    setMorphTarget (ch, b);
    if (t.hasProperty ("name")) chans[(size_t) ch].name = t["name"].toString();
}

juce::ValueTree Ferrum8Processor::createPresetTree() const
{
    juce::ValueTree t ("FERRUM8_PRESET");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("name", presetName, nullptr);
    juce::ValueTree channels ("CHANNELS");
    for (int ch = 0; ch < NC; ++ch)
    {
        auto c = createPatchTree (ch);
        c.setProperty ("index", ch, nullptr);
        const auto& m = soundMut[(size_t) ch];
        if (m.every > 0) // sound mutation settings belong to the channel slot
        {
            c.setProperty ("mutEvery", m.every, nullptr);
            c.setProperty ("mutDepth", m.depth, nullptr);
            c.setProperty ("mutMode", m.mode == 1 ? "Drift" : "Orbit", nullptr);
        }
        const auto& rr = stepReroll[(size_t) ch];
        if (rr.every > 0) // per-channel step variations
        {
            c.setProperty ("rerollEvery", rr.every, nullptr);
            c.setProperty ("rerollAmount", rr.amount, nullptr);
            c.setProperty ("rerollMode", rr.mode == 1 ? "Drift" : "Orbit", nullptr);
        }
        channels.appendChild (c, nullptr);
    }
    t.appendChild (channels, nullptr);
    juce::ValueTree pats ("PATTERNS");
    for (int p = 0; p < kNumPatterns; ++p)
    {
        const auto d = engine.patterns.get (p);
        juce::ValueTree pt ("P");
        pt.setProperty ("index", p, nullptr);
        pt.setProperty ("length", d.length, nullptr);
        pt.setProperty ("chain", d.chainNext, nullptr);
        pt.setProperty ("trig", toHex16 (d.trig), nullptr);
        pt.setProperty ("acc", toHex16 (d.acc), nullptr);
        pt.setProperty ("fill", toHex16 (d.fill), nullptr);
        for (int l = 0; l < kNumExpr; ++l) // expression lanes are only written when used
        {
            bool used = false;
            for (int c = 0; c < kNumChannels; ++c) for (auto v : d.expr[(size_t) c][(size_t) l]) used |= v != 0;
            if (used) pt.setProperty (juce::String ("x_") + exprInfo (l).id, exprToHex (d, l), nullptr);
        }
        pats.appendChild (pt, nullptr);
    }
    t.appendChild (pats, nullptr);
    juce::ValueTree glob ("GLOBALS");
    for (auto* id : { "rate", "swing", "fillRate", "morph" })
        if (auto* p = apvts.getParameter (id)) glob.setProperty (id, p->convertFrom0to1 (p->getValue()), nullptr);
    t.appendChild (glob, nullptr);
    t.appendChild (rackToTree (engine.rack.getState()), nullptr);
    {
        juce::ValueTree fm ("FXMORPH");
        fm.setProperty ("on", fxMorphOn, nullptr);
        fm.setProperty ("edit", fxEdit, nullptr);
        for (int i = 0; i < kFxScenes; ++i)
            if (fxScenesUsed[(size_t) i])
            {
                auto sc = rackToTree (fxScenes[(size_t) i]);
                sc.setProperty ("scene", i, nullptr);
                fm.appendChild (sc, nullptr);
            }
        t.appendChild (fm, nullptr);
    }
    t.appendChild (modsToTree (modStates, engine.rack.getState()), nullptr);
    return t;
}

void Ferrum8Processor::applyPresetTree (const juce::ValueTree& t, bool loadPatches, bool loadPatterns)
{
    const auto glob = t.getChildWithName ("GLOBALS");
    if (glob.isValid())
    {
        for (int i = 0; i < glob.getNumProperties(); ++i)
        {
            const auto name = glob.getPropertyName (i);
            if (! loadPatches && name.toString() == "morph") continue;
            setParamValue (name.toString(), (float) glob.getProperty (name));
        }
        lastMorph = pMorph->load();
    }
    if (loadPatches)
    {
        const auto modTree = t.getChildWithName ("MODS");
        setMods (modTree.isValid() ? treeToMods (modTree) : std::array<mod::ModState, mod::kMods> {}, false);
    }
    if (loadPatches)
    {
        const auto rackTree = t.getChildWithName ("FXRACK");
        if (rackTree.isValid()) engine.rack.setState (treeToRack (rackTree));
        else if (auto legacy = legacyRack (glob)) engine.rack.setState (*legacy);
        // FX Morph scenes (none in older presets)
        fxScenes = {};
        fxScenesUsed = {};
        const auto fm = t.getChildWithName ("FXMORPH");
        for (int i = 0; fm.isValid() && i < fm.getNumChildren(); ++i)
        {
            const auto sc = fm.getChild (i);
            const int idx = (int) sc.getProperty ("scene", -1);
            if (idx < 0 || idx >= kFxScenes) continue;
            fxScenes[(size_t) idx] = treeToRack (sc);
            fxScenesUsed[(size_t) idx] = true;
        }
        fxMorphOn = fm.isValid() && (bool) fm.getProperty ("on", false);
        fxEdit = fm.isValid() ? juce::jlimit (-1, kFxScenes - 1, (int) fm.getProperty ("edit", -1)) : -1;
        if (fxEdit >= 0 && ! fxScenesUsed[(size_t) fxEdit]) fxEdit = -1;
        publishFxMorph();
    }
    if (loadPatches)
    {
        const auto channels = t.getChildWithName ("CHANNELS");
        for (int i = 0; i < channels.getNumChildren(); ++i)
        {
            const auto c = channels.getChild (i);
            const int ch = (int) c.getProperty ("index", i);
            if (ch < 0 || ch >= NC) continue;
            const Patch cur = getCurrentPatch (ch);
            const auto ta = c.getChildWithName ("A"), tb = c.getChildWithName ("B");
            const Patch a = treeToPatch (ta, cur);
            setPatch (ch, a, true);
            if (tb.isValid()) setMorphTarget (ch, treeToPatch (tb, a));
            chans[(size_t) ch].name = c.getProperty ("name", "").toString();
            chans[(size_t) ch].file = juce::File();
            SoundMutation m;
            m.every = juce::jlimit (0, 64, (int) c.getProperty ("mutEvery", 0));
            m.depth = juce::jlimit (0.f, 1.f, (float) c.getProperty ("mutDepth", 0.3f));
            m.mode = c.getProperty ("mutMode", "Orbit").toString() == "Drift" ? 1 : 0;
            soundMut[(size_t) ch] = m;
            mutHomeSet[(size_t) ch] = false;
            mutLoops[(size_t) ch] = 0;
            StepReroll rr;
            rr.every = juce::jlimit (0, 64, (int) c.getProperty ("rerollEvery", 0));
            rr.amount = juce::jlimit (0.01f, 1.f, (float) c.getProperty ("rerollAmount", 0.25f));
            rr.mode = c.getProperty ("rerollMode", "Orbit").toString() == "Drift" ? 1 : 0;
            stepReroll[(size_t) ch] = rr;
            rerollLoops[(size_t) ch] = 0;
            for (auto& h : rerollHome[(size_t) ch]) h.reset();
        }
    }
    if (loadPatterns)
    {
        const auto pats = t.getChildWithName ("PATTERNS");
        for (int i = 0; i < pats.getNumChildren(); ++i)
        {
            const auto pt = pats.getChild (i);
            const int p = (int) pt.getProperty ("index", i);
            if (p < 0 || p >= kNumPatterns) continue;
            PatternData d;
            d.length = (int) pt.getProperty ("length", 16);
            d.chainNext = (bool) pt.getProperty ("chain", false);
            d.trig = fromHex16 (pt["trig"].toString());
            d.acc = fromHex16 (pt["acc"].toString());
            d.fill = fromHex16 (pt["fill"].toString());
            for (int l = 0; l < kNumExpr; ++l)
                exprFromHex (d, l, pt.getProperty (juce::String ("x_") + exprInfo (l).id, "").toString());
            engine.patterns.set (p, d, true);
        }
    }
    if (loadPatches || t.hasProperty ("name")) presetName = t.getProperty ("name", presetName).toString();
    presetDirty = false;
    sendUiChange();
}

void Ferrum8Processor::loadKit (const KitData& k)
{
    fxScenes = {}; fxScenesUsed = {}; fxMorphOn = false; fxEdit = -1; // ... or FX Morph
    publishFxMorph();
    soundMut = {}; // kits start without sound mutation, step variations or modulation
    stepReroll = {};
    for (auto& h : rerollHome) h = {};
    setMods ({}, false);
    setParamValue ("morph", k.morph);
    lastMorph = k.morph;
    for (int ch = 0; ch < NC; ++ch)
    {
        setPatch (ch, k.a[(size_t) ch], true);
        setMorphTarget (ch, k.b[(size_t) ch]);
        chans[(size_t) ch].name = k.channelNames[(size_t) ch];
        chans[(size_t) ch].file = juce::File();
    }
    for (int p = 0; p < kNumPatterns; ++p) engine.patterns.set (p, k.patterns[(size_t) p], true);
    setParamValue ("rate", (float) (int) k.rate);
    setParamValue ("swing", k.swing);
    setParamValue ("fillRate", k.fillRate);
    engine.rack.setState (k.rack);
    presetName = k.name;
    presetFile = juce::File();
    presetDirty = false;
    sendUiChange();
}

juce::File Ferrum8Processor::userFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ferrum 8");
    f.getChildFile ("Presets").createDirectory();
    f.getChildFile ("Patches").createDirectory();
    return f;
}

bool Ferrum8Processor::savePreset (const juce::File& f)
{
    presetName = f.getFileNameWithoutExtension();
    auto xml = createPresetTree().createXml();
    if (xml == nullptr || ! xml->writeTo (f)) return false;
    presetFile = f;
    presetDirty = false;
    sendUiChange();
    return true;
}

bool Ferrum8Processor::loadPreset (const juce::File& f, bool loadPatches, bool loadPatterns)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("FERRUM8_PRESET")) return false;
    pushUndo();
    applyPresetTree (t, loadPatches, loadPatterns);
    if (loadPatches) { presetFile = f; presetName = f.getFileNameWithoutExtension(); }
    sendUiChange();
    return true;
}

bool Ferrum8Processor::savePatch (int ch, const juce::File& f)
{
    chans[(size_t) ch].name = f.getFileNameWithoutExtension();
    auto xml = createPatchTree (ch).createXml();
    if (xml == nullptr || ! xml->writeTo (f)) return false;
    chans[(size_t) ch].file = f;
    sendUiChange();
    return true;
}

bool Ferrum8Processor::loadPatch (int ch, const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.hasType ("FERRUM8_PATCH")) return false;
    pushUndo();
    applyPatchTree (ch, t);
    chans[(size_t) ch].name = f.getFileNameWithoutExtension();
    chans[(size_t) ch].file = f;
    sendUiChange();
    return true;
}

//==============================================================================================
void Ferrum8Processor::moveChannel (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= NC || to >= NC) return;
    pushUndo();
    std::array<Patch, NC> a, b;
    std::array<juce::String, NC> names;
    std::array<bool, NC> mutes;
    for (int c = 0; c < NC; ++c)
    {
        a[(size_t) c] = getEndA (c); b[(size_t) c] = getEndB (c); names[(size_t) c] = chans[(size_t) c].name;
        mutes[(size_t) c] = pMute[(size_t) c]->load() > 0.5f;
    }
    std::vector<int> order;
    for (int c = 0; c < NC; ++c) order.push_back (c);
    order.erase (order.begin() + from);
    order.insert (order.begin() + to, from);
    std::array<PatternData, kNumPatterns> pats;
    for (int p = 0; p < kNumPatterns; ++p) pats[(size_t) p] = engine.patterns.get (p);
    for (int c = 0; c < NC; ++c)
    {
        const int src = order[(size_t) c];
        setPatch (c, a[(size_t) src], true);
        setMorphTarget (c, b[(size_t) src]);
        chans[(size_t) c].name = names[(size_t) src];
        setParamValue ("mute" + juce::String (c + 1), mutes[(size_t) src] ? 1.f : 0.f);
    }
    {   // a channel's mutation and step-variation settings travel with it
        const auto mut = soundMut; const auto rr = stepReroll;
        for (int c = 0; c < NC; ++c)
        {
            soundMut[(size_t) c] = mut[(size_t) order[(size_t) c]];
            stepReroll[(size_t) c] = rr[(size_t) order[(size_t) c]];
            mutHomeSet[(size_t) c] = false; mutLoops[(size_t) c] = 0; rerollLoops[(size_t) c] = 0;
            for (auto& h : rerollHome[(size_t) c]) h.reset();
        }
    }
    for (int p = 0; p < kNumPatterns; ++p)
    {
        PatternData d = pats[(size_t) p];
        for (int c = 0; c < NC; ++c)
        {
            d.setChannel (c, pats[(size_t) p].channel (order[(size_t) c]));
        }
        engine.patterns.set (p, d);
    }
    editorChannel = to;
    sendUiChange();
}

void Ferrum8Processor::transposeChannel (int ch, float semis)
{
    const float d = semis / 127.f;
    Patch a = getEndA (ch), b = getEndB (ch);
    for (int id : { (int) P_OSC_COARSE, (int) P_MOD_COARSE, (int) P_MOD2_COARSE })
    {
        a[(size_t) id] = lxr::clamp01 (a[(size_t) id] + d);
        b[(size_t) id] = lxr::clamp01 (b[(size_t) id] + d);
    }
    setPatch (ch, a, true);
    setMorphTarget (ch, b);
}

std::array<Flavor, Ferrum8Processor::NC> Ferrum8Processor::channelRoles() const
{
    std::array<Flavor, NC> r {};
    for (int c = 0; c < NC; ++c) r[(size_t) c] = Randomizer::guessFlavor (getCurrentPatch (c));
    return r;
}

//==============================================================================================
void Ferrum8Processor::pushUndo()
{
    if (auto xml = createPresetTree().createXml())
    {
        undoStack.push_back (xml->toString());
        if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
}

void Ferrum8Processor::undo()
{
    if (undoStack.empty()) return;
    if (auto xml = createPresetTree().createXml()) redoStack.push_back (xml->toString());
    auto x = juce::XmlDocument::parse (undoStack.back());
    undoStack.pop_back();
    if (x != nullptr) applyPresetTree (juce::ValueTree::fromXml (*x));
}

void Ferrum8Processor::redo()
{
    if (redoStack.empty()) return;
    if (auto xml = createPresetTree().createXml()) undoStack.push_back (xml->toString());
    auto x = juce::XmlDocument::parse (redoStack.back());
    redoStack.pop_back();
    if (x != nullptr) applyPresetTree (juce::ValueTree::fromXml (*x));
}

//==============================================================================================
void Ferrum8Processor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("FERRUM8_STATE");
    state.setProperty ("version", 1, nullptr);
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (createPresetTree(), nullptr);
    juce::ValueTree m ("MIDICONFIG");
    m.setProperty ("launch", midiLaunch.load(), nullptr);
    m.setProperty ("pitched", midiPitched.load(), nullptr);
    m.setProperty ("noteOffStops", midiNoteOff.load(), nullptr);
    m.setProperty ("transmit", midiTransmit.load(), nullptr);
    m.setProperty ("bend", midiBend.load(), nullptr);
    state.appendChild (m, nullptr);
    juce::ValueTree ui ("UI");
    ui.setProperty ("channel", editorChannel, nullptr);
    ui.setProperty ("matrix", matrixView, nullptr);
    ui.setProperty ("exprLane", exprLane, nullptr);
    ui.setProperty ("presetFile", presetFile.getFullPathName(), nullptr);
    state.appendChild (ui, nullptr);
    juce::ValueTree dt ("DICE");
    dt.setProperty ("style", (int) dice.style, nullptr);
    dt.setProperty ("density", dice.density, nullptr);
    dt.setProperty ("complexity", dice.complexity, nullptr);
    dt.setProperty ("expression", dice.expression, nullptr);
    dt.setProperty ("mutation", dice.mutation, nullptr);
    dt.setProperty ("character", (int) dice.character, nullptr);
    dt.setProperty ("genre", dice.genre, nullptr);
    dt.setProperty ("key", dice.key, nullptr);
    dt.setProperty ("evolveEvery", dice.evolveEvery, nullptr);
    dt.setProperty ("allPatterns", dice.allPatterns, nullptr);
    juce::String lockBits;
    for (bool l : locked) lockBits << (l ? "1" : "0");
    dt.setProperty ("locks", lockBits, nullptr);
    state.appendChild (dt, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void Ferrum8Processor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("FERRUM8_STATE")) return;
    const auto params = state.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
    lastMorph = pMorph->load();
    const auto preset = state.getChildWithName ("FERRUM8_PRESET");
    if (preset.isValid())
    {
        // restore the morph end points, keeping the exact parameter values the host saved
        std::array<Patch, NC> current;
        for (int c = 0; c < NC; ++c) current[(size_t) c] = getCurrentPatch (c);
        applyPresetTree (preset);
        for (int c = 0; c < NC; ++c)
            for (int i = 0; i < NP; ++i)
            {
                setParamFromPatchValue (c, i, current[(size_t) c][(size_t) i]);
                lastSeen[(size_t) c][(size_t) i] = getPatchValue (c, i);
            }
    }
    const auto m = state.getChildWithName ("MIDICONFIG");
    if (m.isValid())
    {
        midiLaunch = (int) m.getProperty ("launch", 0);
        midiPitched = (int) m.getProperty ("pitched", 0);
        midiNoteOff = (int) m.getProperty ("noteOffStops", 0);
        midiTransmit = (int) m.getProperty ("transmit", 1);
        midiBend = (int) m.getProperty ("bend", 12);
    }
    const auto ui = state.getChildWithName ("UI");
    if (ui.isValid())
    {
        editorChannel = juce::jlimit (0, NC - 1, (int) ui.getProperty ("channel", 0));
        matrixView = (bool) ui.getProperty ("matrix", false);
        exprLane = juce::jlimit (0, kNumExpr - 1, (int) ui.getProperty ("exprLane", 0));
        presetFile = juce::File (ui.getProperty ("presetFile", "").toString());
    }
    const auto dt = state.getChildWithName ("DICE");
    if (dt.isValid())
    {
        dice.style = (BeatStyle) juce::jlimit (0, (int) BeatStyle::Count - 1, (int) dt.getProperty ("style", 0));
        dice.density = (float) dt.getProperty ("density", 0.5f);
        dice.complexity = (float) dt.getProperty ("complexity", 0.5f);
        dice.expression = (float) dt.getProperty ("expression", 0.5f);
        dice.mutation = (float) dt.getProperty ("mutation", 0.35f);
        dice.genre = juce::jlimit (-1, (int) Genre::Count - 1, (int) dt.getProperty ("genre", -1));
        dice.character = (KitCharacter) juce::jlimit (0, (int) KitCharacter::Count - 1, (int) dt.getProperty ("character", 0));
        dice.key = juce::jlimit (-1, 11, (int) dt.getProperty ("key", -1));
        dice.evolveEvery = juce::jlimit (1, 16, (int) dt.getProperty ("evolveEvery", 2));
        dice.allPatterns = (bool) dt.getProperty ("allPatterns", false);
        const auto lockBits = dt.getProperty ("locks", "").toString();
        for (int c = 0; c < NC; ++c) locked[(size_t) c] = lockBits.length() > c && lockBits[c] == '1';
    }
    undoStack.clear(); redoStack.clear();
    presetDirty = false;
    sendUiChange();
}

//==============================================================================================
// Randomizing
void Ferrum8Processor::recordRoll (const juce::String& label)
{
    if (auto xml = createPresetTree().createXml())
    {
        rollHistory.insert (rollHistory.begin(), Roll { label, xml->toString(), juce::Time::getCurrentTime() });
        if (rollHistory.size() > 16) rollHistory.pop_back();
    }
}

void Ferrum8Processor::restoreRoll (int index)
{
    if (index < 0 || index >= (int) rollHistory.size()) return;
    auto xml = juce::XmlDocument::parse (rollHistory[(size_t) index].state);
    if (xml == nullptr) return;
    pushUndo();
    applyPresetTree (juce::ValueTree::fromXml (*xml));
    presetDirty = true;
    sendUiChange();
}

static juce::String amountNote (float amount)
{
    return amount >= 0.98f ? juce::String() : " " + juce::String (juce::roundToInt (amount * 100.f)) + " %";
}

// partial sound dice: every unlocked channel's sound moves 'amount' of the way to a new one of the same model
int Ferrum8Processor::blendSounds (float amount)
{
    int n = 0;
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) continue;
        const Patch cur = getActiveEnd (ch);
        Patch target = randomizer.kitPatch (Randomizer::guessFlavor (cur), cur, dice);
        if (target[P_MODEL] != cur[P_MODEL])
        {
            target = randomizer.patchForModel (cur, randomizer.genreFor (dice));
            randomizer.applyCharacter (target, dice.character);
        }
        Patch p = randomizer.blendToward (cur, target, amount);
        if (dice.key >= 0) Randomizer::tuneToKey (p, dice.key);
        setPatchAtActiveEnd (ch, p);
        ++n;
    }
    return n;
}

// partial beat dice: the selected pattern (or A-L) takes part of a freshly rolled beat
void Ferrum8Processor::blendBeat (float amount, bool allPatterns, const std::array<Flavor, NC>& roles, const ChannelMask& keep)
{
    if (allPatterns)
    {
        std::array<PatternData, kNumPatterns> cur, fresh;
        for (int p = 0; p < kNumPatterns; ++p) cur[(size_t) p] = fresh[(size_t) p] = engine.patterns.get (p);
        randomizer.generateSet (fresh, roles, dice, keep);
        for (int p = 0; p < kNumPatterns; ++p)
        {
            fresh[(size_t) p].length = cur[(size_t) p].length;
            randomizer.blendSteps (cur[(size_t) p], fresh[(size_t) p], amount, keep);
            engine.patterns.set (p, cur[(size_t) p]);
        }
    }
    else
    {
        const int sel = getSelectedPattern();
        PatternData d = engine.patterns.get (sel), fresh = d;
        randomizer.generateBeat (fresh, roles, dice, keep, false);
        randomizer.blendSteps (d, fresh, amount, keep);
        engine.patterns.set (sel, d);
    }
}

juce::String Ferrum8Processor::rollSounds (float amount)
{
    pushUndo();
    int n = 0;
    if (amount < 0.98f) n = blendSounds (amount);
    else
        for (int ch = 0; ch < NC; ++ch)
        {
            if (locked[(size_t) ch]) continue;
            const Patch cur = getActiveEnd (ch);
            const Flavor f = Randomizer::guessFlavor (cur); // keep each channel's job so the beat still fits
            setPatchAtActiveEnd (ch, randomizer.kitPatch (f, cur, dice));
            setChannelName (ch, juce::String ("Rnd ") + kFlavorNames[(int) f]);
            ++n;
        }
    presetDirty = true;
    juce::String label = "Sounds" + amountNote (amount) + " (" + endName (activeEnd()) + ")";
    {
        const Genre g = (Genre) dice.genre;
        if (dice.genre > 0) label << " - " << kGenreNames[(int) g];
    }
    if (dice.character != KitCharacter::Auto) label << " - " << kKitCharacterNames[(int) dice.character];
    if (dice.key >= 0) label << " - " << kKeyNames[dice.key] << " minor";
    recordRoll (label);
    sendUiChange();
    if (amount < 0.98f)
        return "Sounds moved" + amountNote (amount) + " towards new ones on morph end " + endName (activeEnd()) + " of " + juce::String (n)
               + " channel" + (n == 1 ? "" : "s") + " (click further right for bigger changes, Ctrl+Z to undo)";
    return "New sounds on morph end " + endName (activeEnd()) + " of " + juce::String (n) + " channel" + (n == 1 ? "" : "s")
           + ", patterns kept (Ctrl+Z to undo)";
}

juce::String Ferrum8Processor::rollBeat (float amount)
{
    pushUndo();
    const auto roles = channelRoles();
    ChannelMask keep {};
    for (int c = 0; c < NC; ++c) keep[(size_t) c] = locked[(size_t) c];
    const juce::String what = dice.allPatterns ? juce::String ("A-L") : juce::String (kPatternNames[getSelectedPattern()]);
    if (amount < 0.98f) blendBeat (amount, dice.allPatterns, roles, keep);
    else if (dice.allPatterns)
    {
        std::array<PatternData, kNumPatterns> pats;
        for (int p = 0; p < kNumPatterns; ++p) pats[(size_t) p] = engine.patterns.get (p);
        randomizer.generateSet (pats, roles, dice, keep);
        for (int p = 0; p < kNumPatterns; ++p) engine.patterns.set (p, pats[(size_t) p]);
    }
    else
    {
        const int sel = getSelectedPattern();
        PatternData d = engine.patterns.get (sel);
        randomizer.generateBeat (d, roles, dice, keep);
        engine.patterns.set (sel, d);
    }
    evolveHome = {};
    presetDirty = true;
    recordRoll (juce::String ("Beat") + amountNote (amount) + " - " + kBeatStyleNames[(int) dice.style] + " (" + what + ")");
    sendUiChange();
    if (amount < 0.98f)
        return "Beat changed" + amountNote (amount) + " towards a new " + kBeatStyleNames[(int) dice.style] + " beat in "
               + (dice.allPatterns ? "patterns A-L" : "pattern " + what) + " (click further right for bigger changes, Ctrl+Z to undo)";
    return juce::String ("New ") + kBeatStyleNames[(int) dice.style] + " beat in " + (dice.allPatterns ? "patterns A-L" : "pattern " + what)
           + ", sounds kept (Ctrl+Z to undo)";
}

juce::String Ferrum8Processor::rollAll (float amount)
{
    pushUndo();
    auto& r = randomizer;
    if (amount < 0.98f)
    {
        // part of the way: the sounds keep their models and names, A-L take part of a new set
        blendSounds (amount);
        ChannelMask keep {};
        for (int c = 0; c < NC; ++c) keep[(size_t) c] = locked[(size_t) c];
        blendBeat (amount, true, channelRoles(), keep);
        evolveHome = {};
        presetDirty = true;
        recordRoll (juce::String ("All") + amountNote (amount) + " - " + kBeatStyleNames[(int) dice.style]);
        sendUiChange();
        return "Sounds and patterns A-L moved" + amountNote (amount) + " towards a new kit (click further right for bigger changes, Ctrl+Z to undo)";
    }
    const Flavor second[3] = { Flavor::Tom, Flavor::Perc, Flavor::Zap };
    std::array<Flavor, NC> roles = { Flavor::Kick, second[r.irange (0, 2)], Flavor::Perc, Flavor::Snare,
                                     Flavor::Cymbal, Flavor::ClosedHat, Flavor::OpenHat, Flavor::Clap };
    ChannelMask keep {};
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) { keep[(size_t) ch] = true; roles[(size_t) ch] = Randomizer::guessFlavor (getCurrentPatch (ch)); continue; }
        Patch cur = getActiveEnd (ch);
        cur[P_CHOKE] = choiceValue ((ch == 5 || ch == 6) ? 1 : 0, 2);
        // the channel's Output (A, B, FX 1, FX 2, own output) is routing, not sound: the dice never change it
        setPatchAtActiveEnd (ch, r.kitPatch (roles[(size_t) ch], cur, dice));
        setChannelName (ch, juce::String ("Rnd ") + kFlavorNames[(int) roles[(size_t) ch]]);
    }
    std::array<PatternData, kNumPatterns> pats;
    for (int p = 0; p < kNumPatterns; ++p) pats[(size_t) p] = engine.patterns.get (p);
    r.generateSet (pats, roles, dice, keep);
    for (int p = 0; p < kNumPatterns; ++p) engine.patterns.set (p, pats[(size_t) p]);
    evolveHome = {};
    for (auto& h : rerollHome) h = {};
    presetName = "Random Kit";
    presetFile = juce::File();
    presetDirty = true;
    selectPattern (0);
    recordRoll (juce::String ("All - ") + kBeatStyleNames[(int) dice.style]);
    sendUiChange();
    return juce::String ("New kit + ") + kBeatStyleNames[(int) dice.style] + " patterns A-L (A main, B variation, C fill, D busier, E breakdown, F build ... ) - Ctrl+Z to undo";
}

juce::String Ferrum8Processor::rollChannelSteps (int ch, float amount)
{
    pushUndo();
    ChannelMask keep {};
    for (int c = 0; c < NC; ++c) keep[(size_t) c] = c != ch;
    const int sel = getSelectedPattern();
    PatternData d = engine.patterns.get (sel);
    if (amount < 0.98f)
    {
        PatternData fresh = d;
        randomizer.generateBeat (fresh, channelRoles(), dice, keep, false);
        randomizer.blendSteps (d, fresh, amount, keep);
    }
    else randomizer.generateBeat (d, channelRoles(), dice, keep, false);
    engine.patterns.set (sel, d);
    rerollHome[(size_t) ch][(size_t) sel].reset();   // a hand-rolled channel is the new original
    presetDirty = true;
    sendUiChange();
    return "New steps" + amountNote (amount) + " for channel " + juce::String (ch + 1) + " (" + kBeatStyleNames[(int) dice.style] + ")";
}

juce::String Ferrum8Processor::rollMorphTargets (float amount)
{
    pushUndo();
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) continue;
        const Patch cur = getCurrentPatch (ch);
        Patch b = randomizer.kitPatch (Randomizer::guessFlavor (cur), cur, dice);
        b[P_MODEL] = cur[P_MODEL];
        if (amount < 0.98f)
        {
            Patch oldB = getEndB (ch);
            oldB[P_MODEL] = cur[P_MODEL];
            b = randomizer.blendToward (oldB, b, amount);
        }
        setMorphTarget (ch, b);
    }
    sendUiChange();
    return "New morph targets (B)" + amountNote (amount) + " for every unlocked channel - move Sound Morph to explore";
}

//==============================================================================================
// Per-channel step variations
void Ferrum8Processor::setStepReroll (int ch, const StepReroll& r)
{
    auto& cur = stepReroll[(size_t) ch];
    const bool leavingOrbit = cur.every > 0 && cur.mode == 0 && (r.every == 0 || r.mode != 0);
    if (leavingOrbit) restoreStepRerollHome (ch);         // like Evolve: Orbit off brings the original back
    if (r.every == 0 || r.mode != 0) for (auto& h : rerollHome[(size_t) ch]) h.reset();
    cur = r;
    cur.amount = juce::jlimit (0.01f, 1.f, cur.amount);
    rerollLoops[(size_t) ch] = 0;
    presetDirty = true;
    sendUiChange();
}

bool Ferrum8Processor::hasStepRerollHome (int ch) const
{
    for (const auto& h : rerollHome[(size_t) ch]) if (h) return true;
    return false;
}

void Ferrum8Processor::restoreStepRerollHome (int ch)
{
    for (int p = 0; p < kNumPatterns; ++p)
    {
        auto& h = rerollHome[(size_t) ch][(size_t) p];
        if (! h) continue;
        PatternData d = engine.patterns.get (p);
        d.setChannel (ch, *h);
        engine.patterns.set (p, d);
        h.reset();
    }
    sendUiChange();
}

void Ferrum8Processor::keepStepRerollHome (int ch)
{
    for (auto& h : rerollHome[(size_t) ch]) h.reset();
}

void Ferrum8Processor::stepRerollTick (int loops)
{
    if (engine.uiState.load() != (int) SeqState::Playing) { rerollLoops = {}; return; }
    const int p = engine.uiLastStartedPattern.load();
    if (p < 0 || p >= kNumPatterns) return;
    std::optional<std::array<Flavor, NC>> roles;
    for (int ch = 0; ch < NC; ++ch)
    {
        const auto& r = stepReroll[(size_t) ch];
        if (r.every <= 0 || locked[(size_t) ch]) { rerollLoops[(size_t) ch] = 0; continue; }
        rerollLoops[(size_t) ch] += loops;
        if (rerollLoops[(size_t) ch] < r.every) continue;
        rerollLoops[(size_t) ch] = 0;
        PatternData d = engine.patterns.get (p);
        auto& home = rerollHome[(size_t) ch][(size_t) p];
        if (r.mode == 0)
        {
            if (! home) home = d.channel (ch);
            d.setChannel (ch, *home);
        }
        if (! roles) roles = channelRoles();
        ChannelMask keep {};
        for (int c = 0; c < NC; ++c) keep[(size_t) c] = c != ch;
        PatternData fresh = d;
        randomizer.generateBeat (fresh, *roles, dice, keep, false);
        randomizer.blendSteps (d, fresh, r.amount, keep);
        engine.patterns.set (p, d);
        ++rerollCount[(size_t) ch];
        presetDirty = true;
    }
}

void Ferrum8Processor::setEvolve (int mode)
{
    mode = juce::jlimit (0, 2, mode);
    if (mode == dice.evolve) return;
    if (dice.evolve == 1) // leaving orbit: go back to where it started
        for (int p = 0; p < kNumPatterns; ++p)
            if (evolveHome[(size_t) p]) engine.patterns.set (p, *evolveHome[(size_t) p]);
    evolveHome = {};
    dice.evolve = mode;
    loopsSinceEvolve = 0;
    lastPatternStarts = engine.uiPatternStarts.load();
    sendUiChange();
}

void Ferrum8Processor::keepEvolvedAsHome()
{
    for (int p = 0; p < kNumPatterns; ++p)
        if (evolveHome[(size_t) p]) evolveHome[(size_t) p] = engine.patterns.get (p);
}

void Ferrum8Processor::evolveTick()
{
    const uint32_t starts = engine.uiPatternStarts.load();
    if (starts == lastPatternStarts) return;
    const int loops = (int) (starts - lastPatternStarts);
    lastPatternStarts = starts;
    soundMutationTick (loops);
    stepRerollTick (loops);
    if (dice.evolve == 0 || engine.uiState.load() != (int) SeqState::Playing) { loopsSinceEvolve = 0; return; }
    loopsSinceEvolve += loops;
    if (loopsSinceEvolve < juce::jmax (1, dice.evolveEvery)) return;
    loopsSinceEvolve = 0;
    const int p = engine.uiLastStartedPattern.load();
    if (! evolveHome[(size_t) p]) evolveHome[(size_t) p] = engine.patterns.get (p);
    PatternData d = dice.evolve == 1 ? *evolveHome[(size_t) p] : engine.patterns.get (p);
    ChannelMask keep {};
    for (int c = 0; c < NC; ++c) keep[(size_t) c] = locked[(size_t) c];
    randomizer.mutate (d, channelRoles(), dice.evolve == 1 ? dice.mutation : dice.mutation * 0.5f, keep);
    engine.patterns.set (p, d);
    presetDirty = true;
}

//==============================================================================================
bool Ferrum8Processor::exportPatternMidi (const juce::File& f, int pattern) const
{
    const auto& pb = engine.patterns;
    int first, last;
    pb.chainRange (pattern, first, last);
    const double d = Engine::stepLengthPpq ((StepRate) (int) pRate->load());
    const bool triplet = (int) pRate->load() == 1 || (int) pRate->load() == 3;
    const float swing = triplet ? 0.f : pSwing->load();
    const float fillRate = pFill->load();
    constexpr int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, presetName), 0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / lastHostBpm)), 0);
    const double ppqPerSecond = lastHostBpm / 60.0;
    double start = 0;
    for (int p = first; p <= last; ++p)
    {
        const int len = pb.length (p);
        for (int s = 0; s < len; ++s)
            for (int ch = 0; ch < NC; ++ch)
            {
                if (! pb.trig (p, ch, s)) continue;
                int8_t ex[kNumExpr];
                for (int l = 0; l < kNumExpr; ++l) ex[l] = (int8_t) pb.expr (p, ch, l, s);
                if (! chancePasses (ex[X_CHANCE], 1, 0.f)) continue; // a file has no dice: probability steps are kept
                StepHit hits[kMaxStepHits];
                const int nh = buildStepHits (pb.acc (p, ch, s), pb.fill (p, ch, s), fillRate, ex, d, ppqPerSecond, hits);
                const double t0 = start + s * d;
                for (int j = 0; j < nh; ++j)
                {
                    const double on = std::max (0.0, Engine::swingWarp (t0 + hits[j].offset, swing) + nudgePpq (ex[X_NUDGE], d));
                    const double off = on + std::min (d / std::max (1, nh), 0.125) * 0.9;
                    const int vel = juce::jlimit (1, 127, juce::roundToInt (hits[j].vel * ghostVelocityScale (hits[j].art.ghost) * 127.f));
                    seq.addEvent (juce::MidiMessage::noteOn (10, 36 + ch, (juce::uint8) vel), on * tpq);
                    seq.addEvent (juce::MidiMessage::noteOff (10, 36 + ch), off * tpq);
                }
            }
        start += len * d;
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), start * tpq);
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    f.deleteFile();
    juce::FileOutputStream out (f);
    return out.openedOk() && mf.writeTo (out);
}

juce::File Ferrum8Processor::createTempMidiForDrag (int pattern) const
{
    auto name = juce::File::createLegalFileName (presetName + " " + kPatternNames[pattern]);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Ferrum 8").getChildFile (name + ".mid");
    f.getParentDirectory().createDirectory();
    exportPatternMidi (f, pattern);
    return f;
}

bool Ferrum8Processor::exportPatternAudio (const juce::File& f, int pattern, int bits, double sr, int tailMode) const
{
    auto e = std::make_unique<Engine>();
    const int block = 512;
    e->prepare (sr, block);
    e->rack.setState (engine.rack.getState());
    e->rack.setMorphScenes (fxScenesUsed, fxScenes);
    e->rack.setMorphOn (fxMorphOn);
    e->rack.setMorphXY (pFxX->load(), pFxY->load());
    e->rack.prepare (sr, block);   // takes the morph instances over
    e->mods.setState (modStates);
    for (int m = 0; m < mod::kMods; ++m) e->mods.setMacro (m, pMacro[(size_t) m]->load());
    for (int c = 0; c < NC; ++c)
        for (int i = 0; i < NP; ++i) e->setPatchParam (c, i, getPatchValue (c, i));
    for (int p = 0; p < kNumPatterns; ++p) e->patterns.set (p, engine.patterns.get (p), true);
    Globals g;
    g.playOn = true;
    g.selectedPattern = pattern;
    g.rate = (StepRate) (int) pRate->load();
    g.swing = pSwing->load();
    g.fillRate = pFill->load();
    g.master = pMaster->load() <= -59.9f ? 0.f : juce::Decibels::decibelsToGain (pMaster->load());
    for (int c = 0; c < NC; ++c) g.mutes[c] = pMute[(size_t) c]->load() > 0.5f;
    e->setGlobals (g);
    MidiConfig mc = getMidiConfig(); mc.transmitPatternNotes = false;
    e->setMidiConfig (mc);

    int first, last;
    engine.patterns.chainRange (pattern, first, last);
    int steps = 0;
    for (int p = first; p <= last; ++p) steps += engine.patterns.length (p);
    const double bpm = lastHostBpm > 0 ? lastHostBpm : 120.0;
    const double lengthPpq = steps * Engine::stepLengthPpq (g.rate);
    const int loopSamples = (int) std::ceil (lengthPpq * 60.0 / bpm * sr);
    const int maxTail = (int) (sr * 12.0);

    juce::AudioBuffer<float> buf (2, loopSamples + maxTail);
    buf.clear();
    Transport t; t.hostHasTransport = true; t.hostPlaying = true; t.ppq = 0; t.bpm = bpm;
    std::vector<MidiIn> none;
    std::vector<float> l (block), r (block);
    int pos = 0, lastLoud = 0;
    const int total = loopSamples + (tailMode == 0 ? 0 : maxTail);
    while (pos < total)
    {
        const int n = std::min (block, total - pos);
        if (pos >= loopSamples) { Globals g2 = g; g2.playOn = false; e->setGlobals (g2); e->requestImmediateStop(); }
        e->process (l.data(), r.data(), nullptr, nullptr, n, t, none, nullptr);
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, pos + i, l[(size_t) i]);
            buf.setSample (1, pos + i, r[(size_t) i]);
            if (std::abs (l[(size_t) i]) > 3.0e-5f || std::abs (r[(size_t) i]) > 3.0e-5f) lastLoud = pos + i;
        }
        t.ppq += n * bpm / 60.0 / sr;
        pos += n;
        if (pos > loopSamples + (int) (sr * 0.1) && pos - lastLoud > (int) (sr * 0.25)) break; // tail has died away
    }
    int outLen = loopSamples;
    if (tailMode == 1) outLen = std::max (loopSamples, lastLoud + 1);
    if (tailMode == 2)
        for (int i = loopSamples; i < pos; ++i)
            for (int c = 0; c < 2; ++c) buf.addSample (c, (i - loopSamples) % loopSamples, buf.getSample (c, i));

    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) return false;
    juce::WavAudioFormat wav;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (bits)
                    .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                  : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor (stream, opts);
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buf, 0, outLen);
}

//==============================================================================================
juce::AudioProcessorEditor* Ferrum8Processor::createEditor() { return new Ferrum8Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Ferrum8Processor(); }
