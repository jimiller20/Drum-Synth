// LxrVoice.h - one drum channel running any of the four LXR voice models
// (DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c from the LXR-02 firmware).
// Copyright (C) 2012-2026 Erica Synths; port (C) 2026 Ferrum 8 contributors.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LxrCore.h"
#include "PatchParams.h"

namespace ferrum
{
class Voice
{
public:
    void prepare (double sampleRate, uint32_t seed);
    void reset();

    void setParam (int p, float v);   // base (un-modulated) value, normalised
    float getParam (int p) const { return base[(size_t) p]; }
    Model model() const { return currentModel; }

    // note 60 plays the patch at its own pitch; 'art' shapes this one hit (ghost notes, per-step tone ...)
    void trigger (float velocity01, int note, const Articulation& art = {});
    void retriggerLfo() { lfo.phase = lfo.phaseOffset; }
    void choke();                               // fast fade out (choke groups, note-off mode)

    // renders n samples of mono audio (full scale = 1.0), adding nothing: out is overwritten
    void render (float* out, int n);

    float panLeft() const { return triggeredPanL; }
    float panRight() const { return triggeredPanR; }
    int output() const { return choiceIndex (base[P_OUTPUT], 4); } // 0 A, 1 B, 2 FX 1, 3 FX 2
    bool chokes() const { return choiceIndex (base[P_CHOKE], 2) == 1; }
    int lfoRetrigSource() const { return choiceIndex (base[P_LFO_RETRIG], 9); } // 0 free, 1..8 ch
    bool isSilent() const { return silent; }
    bool isFinished() const { return silent && blockPos >= blockLen; } // silent AND the last control block has played out
    float meterLevel() const { return meter; }

    void setBpm (double b) { if (b != bpm) { bpm = b; lfo.updateInc (bpm); } }
    void setPitchBend (float semitones) { bendFactor = std::pow (2.0f, semitones / 12.0f); }

private:
    void applyParam (int p, float v);
    float eff (int p) const { return lxr::clamp01 (base[(size_t) p] + hitOff[(size_t) p]); } // base + per-hit offset
    void setHitOffset (int p, float off);
    float bip() { return (float) (hitRng.next() >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    void applyModulation();
    void controlBlock();              // the firmware's calc*Async(), once per control block
    void renderBlock (int16_t* buf, int n);
    void setModelState();

    double sr = 44100.0;
    float srRatio = 1.0f;             // 44140 / sr
    int ctrlLen = 64;                 // control block length in host samples

    Patch base {};
    Model currentModel = Model::Drum;

    lxr::Osc osc, modOsc, modOsc2, noiseOsc;
    lxr::ResonantFilter filter;
    uint8_t filterType = lxr::FILTER_LP;
    lxr::DecayEg pitchEg;
    float egPitchModAmount = 0.5f;
    lxr::SlopeEg2 volEg;
    lxr::TransientGen trans;
    lxr::SnapEg snap;
    lxr::VoiceDistortion dist;
    lxr::Lfo lfo;
    lxr::Rng rng, lfoRng;
    float fmModAmount = 0.5f, fmModAmount2 = 0.5f;
    bool mixOscs = false;
    float noiseMix = 0.5f, pwm = 0.f;
    float vol = 0.8f, velo = 1.f, velToVol = 0.f;
    float pan = 63.5f, triggeredPanL = 0.707f, triggeredPanR = 0.707f;
    float decimRate = 1.f, decimCnt = 0.f;
    int16_t decimHold = 0;
    lxr::OnePole volSmooth;
    float bendFactor = 1.0f;
    double bpm = 120.0;

    // envelope gain interpolation across a control block
    double ampIn = 0.0, lastGain = 0.0;

    // modulation (LFO + velocity) in normalised parameter space
    int lfoDest = -1, velDest = -1, prevLfoDest = -1, prevVelDest = -1;
    float lfoAmount = 0.f, velAmount = 0.f, lfoValue = 0.5f;

    // control block FIFO
    int16_t block[lxr::kMaxCtrlLen] {};
    int blockPos = 0, blockLen = 0;
    bool silent = true;

    // per-hit articulation (all neutral by default so the LXR sound is untouched)
    Patch hitOff {};                    // offsets added to the patch for this hit (decay, transient, filter)
    float hitGain = 1.f, hitPitch = 1.f, veloVol = 1.f;
    float shelfG = 1.f, shelfA = 0.25f, shelfZ = 0.f;   // high shelf at ~2.2 kHz (tone / ghost darkening)
    float rampPos = 1.f, rampInc = 0.f;                // softened attack
    float drift[4] {};                                 // humanize random walks
    lxr::Rng hitRng;

    // choke fade
    float chokeGain = 1.f, chokeStep = 0.f;
    float meter = 0.f;
};
} // namespace ferrum
