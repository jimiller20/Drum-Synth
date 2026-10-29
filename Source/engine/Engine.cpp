// Engine.cpp - see Engine.h
// SPDX-License-Identifier: GPL-3.0-only
#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ferrum
{
Engine::Engine()
{
    for (int c = 0; c < kNumChannels; ++c)
    {
        static const Model kit[kNumChannels] = { Model::Drum, Model::Drum, Model::Drum, Model::Snare,
                                                 Model::Cymbal, Model::HiHat, Model::HiHat, Model::Snare };
        const Patch p = defaultPatch (kit[c]);
        for (int i = 0; i < NUM_PATCH_PARAMS; ++i) voices[(size_t) c].setParam (i, p[(size_t) i]);
    }
    pending.reserve (1024);
    seqRng.seed (0x51ED2701u);
    trigs.reserve (512);
    pendingNoteOffs.reserve (256);
}

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (int c = 0; c < kNumChannels; ++c) voices[(size_t) c].prepare (sr, (uint32_t) (c + 1) * 7919u);
    fxBus.prepare (sr);
    fxBus.setSettings (globals.fx);
    const size_t n = (size_t) std::max (maxBlock, 16);
    mono.assign (n, 0.f); fxL.assign (n, 0.f); fxR.assign (n, 0.f);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) v.reset();
    fxBus.reset();
    pending.clear();
    pendingNoteOffs.clear();
    expectedPpq = -1.0;
}

double Engine::stepLengthPpq (StepRate r)
{
    switch (r)
    {
        case StepRate::Eighth:      return 0.5;
        case StepRate::EighthT:     return 1.0 / 3.0;
        case StepRate::SixteenthT:  return 1.0 / 6.0;
        case StepRate::ThirtySecond: return 0.125;
        case StepRate::Sixteenth:
        default:                    return 0.25;
    }
}

// Swing delays the sixteenths that fall between the eighths. The warp is continuous so fills and
// 32nd steps move with it; 100% puts the off-beat sixteenth at 3/4 of the eighth.
double Engine::swingWarp (double ppq, float swing)
{
    if (swing <= 0.f) return ppq;
    const double s = 0.5 * swing;
    const double u = ppq * 4.0;
    const double pair = std::floor (u / 2.0);
    const double r = u - 2.0 * pair;
    const double rw = r < 1.0 ? r * (1.0 + s) : (1.0 + s) + (r - 1.0) * (1.0 - s);
    return (2.0 * pair + rw) / 4.0;
}

void Engine::setGlobals (const Globals& g)
{
    const auto& a = g.fx; const auto& b = globals.fx;
    const bool fxChanged = a.type != b.type || a.dryWet != b.dryWet || a.p[0] != b.p[0] || a.p[1] != b.p[1]
                        || a.p[2] != b.p[2] || a.p[3] != b.p[3] || a.distType != b.distType || a.ratio != b.ratio
                        || a.ringWave != b.ringWave || a.pingPong != b.pingPong;
    globals = g;
    if (fxChanged) fxBus.setSettings (g.fx);
}

void Engine::previewTrigger (int ch, float velocity)
{
    const int w = previewWrite.load();
    const int next = (w + 1) % (int) previewFifo.size();
    if (next == previewRead.load()) return; // full
    previewFifo[(size_t) w] = { ch, velocity };
    previewWrite.store (next);
}

//---------------------------------------------------------------------------------------------
void Engine::resync (double ppq)
{
    const double d = stepLengthPpq (globals.rate);
    int first, last;
    patterns.chainRange (curPattern, first, last);
    double total = 0;
    for (int p = first; p <= last; ++p) total += patterns.length (p) * d;
    if (total <= 0) total = d;
    double pos = std::fmod (ppq, total);
    if (pos < 0) pos += total;
    int p = first;
    double acc = 0;
    while (p < last && pos >= acc + patterns.length (p) * d - 1e-9) { acc += patterns.length (p) * d; ++p; }
    curPattern = p;
    const double inPat = pos - acc;
    anchor = ppq - inPat;
    nextStep = (int) std::ceil (inPat / d - 1e-7);
    nextStepPpq = anchor + nextStep * d;
    pending.clear();
}

void Engine::startAt (double ppq)
{
    resync (ppq);
    for (auto& c : cycles) c = 0;
    if (nextStep > 0) cycles[curPattern] = 1; // started in the middle: this is the first time through
    state = SeqState::Playing;
}

void Engine::scheduleUntil (double ppq1, double ppqPerSecond)
{
    const double d = stepLengthPpq (globals.rate);
    const bool triplet = globals.rate == StepRate::EighthT || globals.rate == StepRate::SixteenthT;
    const float swing = triplet ? 0.f : globals.swing;
    // schedule a little ahead so hits that are pushed early (nudge, flams, humanize) can still happen
    const double until = ppq1 + lookaheadPpq (d, ppqPerSecond);
    int guard = 0;
    while (nextStepPpq < until && (state == SeqState::Playing || state == SeqState::WaitingStop) && guard++ < 4096)
    {
        const int len = patterns.length (curPattern);
        if (nextStep >= len)
        {
            if (state == SeqState::WaitingStop)
            {
                state = SeqState::Stopped;
                pending.push_back ({ nextStepPpq, -2, 0.f, -1, curPattern }); // stop marker
                break;
            }
            if (pendingSwitch) { curPattern = reqPattern; pendingSwitch = false; }
            else
            {
                int first, last;
                patterns.chainRange (curPattern, first, last);
                if (first != last) curPattern = curPattern >= last ? first : curPattern + 1;
            }
            anchor = nextStepPpq;
            nextStep = 0;
            continue;
        }
        const double t0 = nextStepPpq;
        if (nextStep == 0)
        {
            ++cycles[curPattern];
            uiLastStartedPattern.store (curPattern);
            uiPatternStarts.fetch_add (1);
        }
        pending.push_back ({ swingWarp (t0, swing), -1, 0.f, nextStep, curPattern });
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            if (! patterns.trig (curPattern, ch, nextStep)) continue;
            int8_t ex[kNumExpr];
            for (int l = 0; l < kNumExpr; ++l) ex[l] = (int8_t) patterns.expr (curPattern, ch, l, nextStep);
            if (! chancePasses (ex[X_CHANCE], cycles[curPattern], rand01())) continue;

            StepHit hits[kMaxStepHits];
            const int nh = buildStepHits (patterns.acc (curPattern, ch, nextStep), patterns.fill (curPattern, ch, nextStep),
                                          globals.fillRate, ex, d, ppqPerSecond, hits);
            double shift = nudgePpq (ex[X_NUDGE], d);
            const float human = voices[(size_t) ch].getParam (P_HUMANIZE);
            if (human > 0.f) // timing that wanders like a player's, up to +-12 ms
            {
                timeDrift[ch] = 0.6f * timeDrift[ch] + 0.4f * (rand01() * 2.f - 1.f);
                shift += human * timeDrift[ch] * 0.012 * ppqPerSecond * 1.6;
            }
            for (int j = 0; j < nh; ++j)
                pending.push_back ({ swingWarp (t0 + hits[j].offset, swing) + shift, ch, hits[j].vel, nextStep, curPattern, hits[j].art });
        }
        ++nextStep;
        nextStepPpq = anchor + nextStep * d;
    }
}

void Engine::runSequencer (int n, const Transport& t, std::vector<Trig>& out)
{
    const double bpm = (t.hostHasTransport && t.bpm > 0) ? t.bpm : globals.tempo;
    const double ppqPerSample = bpm / 60.0 / sr;
    const bool hostClock = t.hostHasTransport && t.hostPlaying;

    // pattern selection from the parameter / UI
    if (globals.selectedPattern != lastSelected)
    {
        lastSelected = globals.selectedPattern;
        reqPattern = lastSelected;
        if (state == SeqState::Stopped || state == SeqState::WaitingStart) curPattern = reqPattern;
        else pendingSwitch = reqPattern != curPattern;
    }

    // clock source
    double ppq0 = 0.0;
    bool clockRunning = false;
    if (hostClock)
    {
        ppq0 = t.ppq;
        clockRunning = true;
        if (usingInternal) { usingInternal = false; if (state == SeqState::Playing || state == SeqState::WaitingStop) resync (ppq0); }
        else if (! lastHostPlaying && (state == SeqState::WaitingStart || state == SeqState::Playing)) startAt (ppq0);
        else if ((state == SeqState::Playing || state == SeqState::WaitingStop) && expectedPpq >= 0.0
                 && std::fabs (ppq0 - expectedPpq) > 0.002)
            resync (ppq0); // loop or jump in the host
    }
    else
    {
        if (! usingInternal && t.hostHasTransport && lastHostPlaying
            && (state == SeqState::Playing || state == SeqState::WaitingStop))
        {
            state = globals.playOn ? SeqState::WaitingStart : SeqState::Stopped; // host stopped
            pending.clear();
        }
        if (usingInternal) { ppq0 = internalPpq; clockRunning = true; }
    }
    lastHostPlaying = t.hostPlaying;

    // play / stop parameter edges
    if (globals.playOn != lastPlayOn)
    {
        lastPlayOn = globals.playOn;
        if (globals.playOn)
        {
            if (state == SeqState::WaitingStop) state = SeqState::Playing;
            else if (state == SeqState::Stopped)
            {
                if (! t.hostHasTransport) // standalone: just go
                {
                    usingInternal = true; internalPpq = 0; ppq0 = 0; clockRunning = true;
                    startAt (0);
                }
                else state = SeqState::WaitingStart;
            }
        }
        else
        {
            if (state == SeqState::Playing) state = SeqState::WaitingStop;
            else if (state == SeqState::WaitingStart) state = SeqState::Stopped;
        }
    }
    if (immediateStart.exchange (false))
    {
        if (hostClock) startAt (ppq0);
        else { usingInternal = true; internalPpq = 0; ppq0 = 0; clockRunning = true; startAt (0); }
        if (! globals.playOn) uiPlayRequest.store (1);
    }
    if (immediateStop.exchange (false))
    {
        state = SeqState::Stopped; usingInternal = false; pending.clear();
        if (globals.playOn) uiPlayRequest.store (-1);
    }

    if (clockRunning)
    {
        const double ppq1 = ppq0 + n * ppqPerSample;
        if (state == SeqState::WaitingStart && hostClock)
        {
            const double L = patterns.length (curPattern) * stepLengthPpq (globals.rate);
            const double b = std::ceil (ppq0 / L - 1e-9) * L;
            if (b < ppq1) startAt (b);
        }
        if (state == SeqState::Playing || state == SeqState::WaitingStop) scheduleUntil (ppq1, bpm / 60.0);

        std::stable_sort (pending.begin(), pending.end(), [] (const Ev& a, const Ev& b) { return a.ppq < b.ppq; });
        size_t used = 0;
        for (; used < pending.size() && pending[used].ppq < ppq1; ++used)
        {
            const Ev& e = pending[used];
            const int off = std::clamp ((int) std::floor ((e.ppq - ppq0) / ppqPerSample), 0, n - 1);
            if (e.ch == -1) { uiStep.store (e.step); uiPlayingPattern.store (e.pattern); }
            else if (e.ch == -2) { uiStep.store (-1); }
            else out.push_back ({ off, e.ch, e.vel, 60, true, false, e.art });
        }
        pending.erase (pending.begin(), pending.begin() + (long) used);
        if (usingInternal) internalPpq = ppq1;
        expectedPpq = hostClock ? ppq1 : -1.0;
    }
    else expectedPpq = -1.0;

    if (state == SeqState::Stopped || state == SeqState::WaitingStart) uiStep.store (-1);
    uiState.store ((int) state);
    uiSelectedPattern.store (reqPattern);
    if (state != SeqState::Playing && state != SeqState::WaitingStop) uiPlayingPattern.store (curPattern);
}

//---------------------------------------------------------------------------------------------
void Engine::handleMidi (const MidiIn& m, double ppqAt, std::vector<Trig>& out)
{
    const int type = m.status & 0xF0, chan = m.status & 0x0F;
    const bool pitchedVoiceChannel = midiCfg.pitchedMode && chan < kNumChannels;
    if (type == 0x90 && m.data2 > 0)
    {
        if (pitchedVoiceChannel) { out.push_back ({ m.offset, chan, m.data2 / 127.f, m.data1, false }); return; }
        const int note = m.data1;
        if (note >= 36 && note <= 43) out.push_back ({ m.offset, note - 36, m.data2 / 127.f, 60, false });
        else if (note >= 48 && note <= 55) { muteKeyHeld[note - 48] = true; midiMute[note - 48] = true; }
        else if (note >= 60 && note <= 71)
        {
            const int p = note - 60;
            uiPatternRequest.store (p);
            lastSelected = p; // the parameter will follow; don't treat it as a second change
            reqPattern = p;
            const bool running = state == SeqState::Playing || state == SeqState::WaitingStop;
            switch (midiCfg.launch)
            {
                case PatternLaunch::SwitchNext:
                    if (running) pendingSwitch = p != curPattern; else curPattern = p;
                    break;
                case PatternLaunch::SwitchDirectly:
                    curPattern = p; pendingSwitch = false;
                    break;
                case PatternLaunch::Retrigger:
                case PatternLaunch::RetriggerGated:
                    curPattern = p; pendingSwitch = false;
                    anchor = ppqAt; nextStep = 0; nextStepPpq = anchor; pending.clear();
                    gatedPatternNote = note;
                    if (! running) { state = SeqState::Playing; if (! globals.playOn) uiPlayRequest.store (1); }
                    break;
            }
            if (state == SeqState::Stopped) { state = SeqState::WaitingStart; if (! globals.playOn) uiPlayRequest.store (1); }
            else if (state == SeqState::WaitingStart && midiCfg.launch == PatternLaunch::SwitchNext) immediateStart.store (true);
        }
        else if (note == 72)
        {
            if (state == SeqState::WaitingStop || state == SeqState::WaitingStart) { state = SeqState::Stopped; pending.clear(); usingInternal = false; }
            else if (state == SeqState::Playing) state = SeqState::WaitingStop;
            if (globals.playOn) uiPlayRequest.store (-1);
        }
    }
    else if (type == 0x80 || (type == 0x90 && m.data2 == 0))
    {
        if (pitchedVoiceChannel) { if (midiCfg.noteOffStops) voices[(size_t) chan].choke(); return; }
        const int note = m.data1;
        if (note >= 48 && note <= 55) { muteKeyHeld[note - 48] = false; if (! sustain) midiMute[note - 48] = false; }
        else if (note >= 36 && note <= 43 && midiCfg.noteOffStops) voices[(size_t) (note - 36)].choke();
        else if (note == gatedPatternNote && midiCfg.launch == PatternLaunch::RetriggerGated)
        {
            state = SeqState::Stopped; pending.clear(); gatedPatternNote = -1;
            if (globals.playOn) uiPlayRequest.store (-1);
        }
    }
    else if (type == 0xB0 && m.data1 == 64)
    {
        sustain = m.data2 >= 64;
        if (! sustain) for (int c = 0; c < kNumChannels; ++c) if (! muteKeyHeld[c]) midiMute[c] = false;
    }
    else if (type == 0xE0)
    {
        const int v = ((int) m.data2 << 7 | m.data1) - 8192;
        const float semis = (float) v / 8192.f * (float) midiCfg.pitchBendSemis;
        if (pitchedVoiceChannel) voices[(size_t) chan].setPitchBend (semis);
        else for (auto& vc : voices) vc.setPitchBend (semis);
    }
}

void Engine::doTrigger (const Trig& tr, int sampleIndex)
{
    Voice& v = voices[(size_t) tr.ch];
    const long long when = sampleClock + sampleIndex;
    if (v.chokes())
    {
        // lower channel numbers win when two choke channels fire on the same sample
        if (lastChokeTriggerSample == when && lastChokeTriggerChannel >= 0 && lastChokeTriggerChannel < tr.ch) return;
        for (int c = 0; c < kNumChannels; ++c)
            if (c != tr.ch && voices[(size_t) c].chokes()) voices[(size_t) c].choke();
        lastChokeTriggerSample = when;
        lastChokeTriggerChannel = tr.ch;
    }
    for (auto& o : voices) if (o.lfoRetrigSource() == tr.ch + 1) o.retriggerLfo();
    v.trigger (tr.vel, tr.note, tr.art);
    uiTriggerCount[tr.ch].fetch_add (1);
}

//---------------------------------------------------------------------------------------------
void Engine::process (float* aL, float* aR, float* bL, float* bR, int n, const Transport& t,
                      const std::vector<MidiIn>& midiIn, std::vector<MidiOut>* midiOut)
{
    if ((int) mono.size() < n) { mono.resize ((size_t) n); fxL.resize ((size_t) n); fxR.resize ((size_t) n); }
    const double bpm = (t.hostHasTransport && t.bpm > 0) ? t.bpm : globals.tempo;
    for (auto& v : voices) v.setBpm (bpm);
    for (int c = 0; c < kNumChannels; ++c) uiMidiMute[c].store (midiMute[c]);

    trigs.clear();
    // UI previews (bypass mutes, like clicking a channel button)
    while (previewRead.load() != previewWrite.load())
    {
        const int r = previewRead.load();
        const Preview p = previewFifo[(size_t) r];
        previewRead.store ((r + 1) % (int) previewFifo.size());
        if (p.ch >= 0 && p.ch < kNumChannels) trigs.push_back ({ 0, p.ch, p.vel, 60, false, true });
    }
    // MIDI input
    const double ppqPerSample = bpm / 60.0 / sr;
    const double basePpq = usingInternal ? internalPpq : t.ppq;
    for (const auto& m : midiIn)
    {
        const size_t before = trigs.size();
        handleMidi (m, basePpq + std::clamp (m.offset, 0, n - 1) * ppqPerSample, trigs);
        for (size_t i = before; i < trigs.size(); ++i) trigs[i].offset = std::clamp (trigs[i].offset, 0, n - 1);
    }
    runSequencer (n, t, trigs);

    std::stable_sort (trigs.begin(), trigs.end(), [] (const Trig& a, const Trig& b)
                      { return a.offset != b.offset ? a.offset < b.offset : a.ch < b.ch; });

    for (int i = 0; i < n; ++i) { aL[i] = aR[i] = 0.f; fxL[(size_t) i] = fxR[(size_t) i] = 0.f; }
    if (bL != nullptr) for (int i = 0; i < n; ++i) { bL[i] = bR[i] = 0.f; }

    auto renderSegment = [&] (int pos, int len)
    {
        if (len <= 0) return;
        for (int c = 0; c < kNumChannels; ++c)
        {
            Voice& v = voices[(size_t) c];
            if (v.isFinished()) continue; // only skip once the tail has fully played (the hardware never cuts it)
            v.render (mono.data(), len);
            const float pl = v.panLeft(), pr = v.panRight();
            const int o = v.output();
            if (o == 2) // FX bus: 16-bit saturating sum, like the hardware's fxBuffer
            {
                for (int i = 0; i < len; ++i)
                {
                    const float s = mono[(size_t) i] * 32768.f;
                    float& l = fxL[(size_t) (pos + i)]; float& r = fxR[(size_t) (pos + i)];
                    l = std::clamp (l + std::trunc (s * pl), -32768.f, 32767.f);
                    r = std::clamp (r + std::trunc (s * pr), -32768.f, 32767.f);
                }
            }
            else
            {
                float* L = (o == 1 && bL != nullptr) ? bL : aL;
                float* R = (o == 1 && bR != nullptr) ? bR : aR;
                for (int i = 0; i < len; ++i) { L[pos + i] += mono[(size_t) i] * pl; R[pos + i] += mono[(size_t) i] * pr; }
            }
        }
    };

    int pos = 0;
    const double stepSamples = stepLengthPpq (globals.rate) / ppqPerSample;
    for (size_t k = 0; k < trigs.size(); ++k)
    {
        const Trig& tr = trigs[k];
        if (tr.offset > pos) { renderSegment (pos, tr.offset - pos); pos = tr.offset; }
        if (tr.ch < 0 || tr.ch >= kNumChannels) continue;
        if (! tr.preview && isMuted (tr.ch)) continue; // muted channels ignore pattern and MIDI triggers
        doTrigger (tr, tr.offset);
        if (tr.fromSequencer && midiCfg.transmitPatternNotes && midiOut != nullptr)
        {
            const uint8_t st = (uint8_t) (0x90 | (midiCfg.transmitChannel & 0x0F));
            const int vel = std::clamp ((int) std::lround (tr.vel * ghostVelocityScale (tr.art.ghost) * 127.f), 1, 127);
            midiOut->push_back ({ tr.offset, st, (uint8_t) (36 + tr.ch), (uint8_t) vel });
            const int offAfter = std::max (1, (int) std::min (stepSamples * 0.5, 0.05 * sr));
            pendingNoteOffs.push_back ({ tr.offset + offAfter, 36 + tr.ch });
        }
    }
    renderSegment (pos, n - pos);

    // FX bus
    if (globals.fx.type != FxType::Off || std::any_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.output() == 2; }))
    {
        float* L = (globals.fxOutput == 1 && bL != nullptr) ? bL : aL;
        float* R = (globals.fxOutput == 1 && bR != nullptr) ? bR : aR;
        fxBus.process (fxL.data(), fxR.data(), L, R, n);
    }

    const float g = globals.master;
    for (int i = 0; i < n; ++i) { aL[i] *= g; aR[i] *= g; }
    if (bL != nullptr) for (int i = 0; i < n; ++i) { bL[i] *= g; bR[i] *= g; }

    // pattern note offs
    if (midiOut != nullptr)
    {
        for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
        {
            if (it->first < n)
            {
                midiOut->push_back ({ std::max (0, it->first), (uint8_t) (0x80 | (midiCfg.transmitChannel & 0x0F)), (uint8_t) it->second, 0 });
                it = pendingNoteOffs.erase (it);
            }
            else { it->first -= n; ++it; }
        }
    }
    else pendingNoteOffs.clear();

    sampleClock += n;
}
} // namespace ferrum
