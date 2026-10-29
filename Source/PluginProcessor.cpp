// PluginProcessor.cpp
// SPDX-License-Identifier: GPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ferrum;

static const char* const kPatternNames[12] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L" };
static const juce::StringArray kRateNames { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
static const juce::StringArray kFxTypeNames { "Off", "Distortion", "Ring Mod", "Compressor", "Delay" };
static const juce::StringArray kDistNames { "Tube", "Fold", "Rat" };
static const juce::StringArray kRatioNames { "1:1", "2:1", "3:1", "4:1", "6:1", "8:1" };

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
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxType", 1 }, "FX Type", kFxTypeNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxMix", 1 }, "FX Dry/Wet", NormalisableRange<float> (0.f, 1.f), 0.5f, pct));
    for (int i = 0; i < 4; ++i)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fxP" + String (i + 1), 1 }, "FX Param " + String (i + 1), NormalisableRange<float> (0.f, 1.f), 0.5f, pct));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxDist", 1 }, "FX Distortion Type", kDistNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxRatio", 1 }, "FX Comp Ratio", kRatioNames, 2));
    {
        StringArray w; for (auto* n : kWaveNames) w.add (n);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxWave", 1 }, "FX Ring Wave", w, 3));
    }
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "fxPingPong", 1 }, "FX Ping-Pong", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxOut", 1 }, "FX Output", StringArray { "A", "B" }, 0));
    return layout;
}

Ferrum8Processor::Ferrum8Processor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output A", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output B", juce::AudioChannelSet::stereo(), false)),
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
    pTempo = apvts.getRawParameterValue ("tempo");
    pMorph = apvts.getRawParameterValue ("morph");
    pFxType = apvts.getRawParameterValue ("fxType");
    pFxMix = apvts.getRawParameterValue ("fxMix");
    for (int i = 0; i < 4; ++i) pFxP[(size_t) i] = apvts.getRawParameterValue ("fxP" + juce::String (i + 1));
    pFxDist = apvts.getRawParameterValue ("fxDist");
    pFxRatio = apvts.getRawParameterValue ("fxRatio");
    pFxWave = apvts.getRawParameterValue ("fxWave");
    pFxPing = apvts.getRawParameterValue ("fxPingPong");
    pFxOut = apvts.getRawParameterValue ("fxOut");
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
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.outputBuses.size() > 1)
    {
        const auto& b = layouts.outputBuses.getReference (1);
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
    g.fx.type = (FxType) juce::jlimit (0, 4, (int) pFxType->load());
    g.fx.dryWet = pFxMix->load();
    for (int i = 0; i < 4; ++i) g.fx.p[i] = pFxP[(size_t) i]->load();
    g.fx.distType = (DistType) juce::jlimit (0, 2, (int) pFxDist->load());
    g.fx.ratio = (int) pFxRatio->load();
    g.fx.ringWave = (int) pFxWave->load();
    g.fx.pingPong = pFxPing->load() > 0.5f;
    g.fxOutput = (int) pFxOut->load();
    engine.setGlobals (g);
    engine.setMidiConfig (getMidiConfig());

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

    auto mainBus = getBusBuffer (buffer, false, 0);
    float* aL = mainBus.getWritePointer (0);
    float* aR = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : aL;
    float* bL = nullptr, *bR = nullptr;
    if (getBusCount (false) > 1 && getBus (false, 1)->isEnabled())
    {
        auto aux = getBusBuffer (buffer, false, 1);
        if (aux.getNumChannels() >= 2) { bL = aux.getWritePointer (0); bR = aux.getWritePointer (1); }
    }
    engine.process (aL, aR, bL, bR, n, t, midiIn, &midiOut);

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
    for (auto* id : { "rate", "swing", "fillRate", "morph", "fxType", "fxMix", "fxP1", "fxP2", "fxP3", "fxP4", "fxDist", "fxRatio", "fxWave", "fxPingPong", "fxOut" })
        if (auto* p = apvts.getParameter (id)) glob.setProperty (id, p->convertFrom0to1 (p->getValue()), nullptr);
    t.appendChild (glob, nullptr);
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
    setParamValue ("fxType", (float) (int) k.fx.type);
    setParamValue ("fxMix", k.fx.dryWet);
    for (int i = 0; i < 4; ++i) setParamValue ("fxP" + juce::String (i + 1), k.fx.p[i]);
    setParamValue ("fxDist", (float) (int) k.fx.distType);
    setParamValue ("fxRatio", (float) k.fx.ratio);
    setParamValue ("fxWave", (float) k.fx.ringWave);
    setParamValue ("fxPingPong", k.fx.pingPong ? 1.f : 0.f);
    setParamValue ("fxOut", (float) k.fxOutput);
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

juce::String Ferrum8Processor::rollSounds()
{
    pushUndo();
    int n = 0;
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) continue;
        const Patch cur = getCurrentPatch (ch);
        const Flavor f = Randomizer::guessFlavor (cur); // keep each channel's job so the beat still fits
        setPatch (ch, randomizer.kitPatch (f, cur, dice), true);
        setChannelName (ch, juce::String ("Rnd ") + kFlavorNames[(int) f]);
        ++n;
    }
    presetDirty = true;
    juce::String label = "Sounds";
    if (dice.character != KitCharacter::Auto) label << " - " << kKitCharacterNames[(int) dice.character];
    if (dice.key >= 0) label << " - " << kKeyNames[dice.key] << " minor";
    recordRoll (label);
    sendUiChange();
    return "New sounds on " + juce::String (n) + " channel" + (n == 1 ? "" : "s") + ", patterns kept (Ctrl+Z to undo)";
}

juce::String Ferrum8Processor::rollBeat()
{
    pushUndo();
    const auto roles = channelRoles();
    ChannelMask keep {};
    for (int c = 0; c < NC; ++c) keep[(size_t) c] = locked[(size_t) c];
    juce::String what;
    if (dice.allPatterns)
    {
        std::array<PatternData, kNumPatterns> pats;
        for (int p = 0; p < kNumPatterns; ++p) pats[(size_t) p] = engine.patterns.get (p);
        randomizer.generateSet (pats, roles, dice, keep);
        for (int p = 0; p < kNumPatterns; ++p) engine.patterns.set (p, pats[(size_t) p]);
        what = "A-L";
    }
    else
    {
        const int sel = getSelectedPattern();
        PatternData d = engine.patterns.get (sel);
        randomizer.generateBeat (d, roles, dice, keep);
        engine.patterns.set (sel, d);
        what = kPatternNames[sel];
    }
    evolveHome = {};
    presetDirty = true;
    recordRoll (juce::String ("Beat - ") + kBeatStyleNames[(int) dice.style] + " (" + what + ")");
    sendUiChange();
    return juce::String ("New ") + kBeatStyleNames[(int) dice.style] + " beat in " + (dice.allPatterns ? "patterns A-L" : "pattern " + what)
           + ", sounds kept (Ctrl+Z to undo)";
}

juce::String Ferrum8Processor::rollAll()
{
    pushUndo();
    auto& r = randomizer;
    const Flavor second[3] = { Flavor::Tom, Flavor::Perc, Flavor::Zap };
    std::array<Flavor, NC> roles = { Flavor::Kick, second[r.irange (0, 2)], Flavor::Perc, Flavor::Snare,
                                     Flavor::Cymbal, Flavor::ClosedHat, Flavor::OpenHat, Flavor::Clap };
    ChannelMask keep {};
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) { keep[(size_t) ch] = true; roles[(size_t) ch] = Randomizer::guessFlavor (getCurrentPatch (ch)); continue; }
        Patch cur = getCurrentPatch (ch);
        cur[P_CHOKE] = choiceValue ((ch == 5 || ch == 6) ? 1 : 0, 2);
        cur[P_OUTPUT] = choiceValue (0, 3);
        setPatch (ch, r.kitPatch (roles[(size_t) ch], cur, dice), true);
        setChannelName (ch, juce::String ("Rnd ") + kFlavorNames[(int) roles[(size_t) ch]]);
    }
    std::array<PatternData, kNumPatterns> pats;
    for (int p = 0; p < kNumPatterns; ++p) pats[(size_t) p] = engine.patterns.get (p);
    r.generateSet (pats, roles, dice, keep);
    for (int p = 0; p < kNumPatterns; ++p) engine.patterns.set (p, pats[(size_t) p]);
    evolveHome = {};
    presetName = "Random Kit";
    presetFile = juce::File();
    presetDirty = true;
    selectPattern (0);
    recordRoll (juce::String ("All - ") + kBeatStyleNames[(int) dice.style]);
    sendUiChange();
    return juce::String ("New kit + ") + kBeatStyleNames[(int) dice.style] + " patterns A-L (A main, B variation, C fill, D busier, E breakdown, F build ... ) - Ctrl+Z to undo";
}

juce::String Ferrum8Processor::rollChannelSteps (int ch)
{
    pushUndo();
    ChannelMask keep {};
    for (int c = 0; c < NC; ++c) keep[(size_t) c] = c != ch;
    const int sel = getSelectedPattern();
    PatternData d = engine.patterns.get (sel);
    randomizer.generateBeat (d, channelRoles(), dice, keep, false);
    engine.patterns.set (sel, d);
    presetDirty = true;
    sendUiChange();
    return "New steps for channel " + juce::String (ch + 1) + " (" + kBeatStyleNames[(int) dice.style] + ")";
}

juce::String Ferrum8Processor::rollMorphTargets()
{
    pushUndo();
    for (int ch = 0; ch < NC; ++ch)
    {
        if (locked[(size_t) ch]) continue;
        const Patch cur = getCurrentPatch (ch);
        Patch b = randomizer.kitPatch (Randomizer::guessFlavor (cur), cur, dice);
        b[P_MODEL] = cur[P_MODEL];
        setMorphTarget (ch, b);
    }
    sendUiChange();
    return "New morph targets (B) for every unlocked channel - move Sound Morph to explore";
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
    g.fx.type = (FxType) (int) pFxType->load();
    g.fx.dryWet = pFxMix->load();
    for (int i = 0; i < 4; ++i) g.fx.p[i] = pFxP[(size_t) i]->load();
    g.fx.distType = (DistType) (int) pFxDist->load();
    g.fx.ratio = (int) pFxRatio->load();
    g.fx.ringWave = (int) pFxWave->load();
    g.fx.pingPong = pFxPing->load() > 0.5f;
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
